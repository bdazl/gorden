module;

#include <spdlog/spdlog.h>

#include <cstddef>
#include <expected>
#include <filesystem>
#include <functional>
#include <utility>

export module roboslop.app;

import roboslop.audio;
import roboslop.audio.device;
import roboslop.core.error;
import roboslop.ecs;
import roboslop.physics;
import roboslop.platform.input;
import roboslop.platform.window;
import roboslop.render.asset_cache;
import roboslop.render.context;
import roboslop.render.frontend;
import roboslop.render.graph;
import roboslop.sched;
import roboslop.time.clock;

namespace roboslop {

// Game-supplied hooks.
//
//   onSetup        — once after init, before the loop. Receives the
//                    World and the App-owned AssetCache so game code
//                    can request programs without managing handle
//                    lifetimes.
//   onBuildGraphs  — once after onSetup, before the loop. Receives the
//                    fixed-step SystemGraph, the render-pass
//                    RenderGraph, and the per-frame FrameArena
//                    (captured by reference into pass record
//                    callbacks). The engine compiles the SystemGraph
//                    after this returns; do not call add() once the
//                    main loop has started.
//
// There are no per-frame onFixedUpdate / onRender callbacks any more —
// the same logic lives as named systems / passes in the two graphs.
export struct AppConfig {
    WindowConfig window;
    double tickRateHz = 60.0;
    std::filesystem::path assetRoot = ".";

    // Default 4 MiB — ~32 768 DrawItems at 128 B each, comfortable
    // overhead for any milestone's worst-case frame.
    std::size_t frameArenaBytes = 4U * 1024U * 1024U;

    // 0 selects defaultWorkerCount() (hardware concurrency - 1,
    // clamped [1, 8]).
    unsigned workerThreads = 0;

    std::function<Result<void>(World&, AssetCache&)> onSetup;
    std::function<void(SystemGraph&, RenderGraph&, FrameArena&)> onBuildGraphs;
};

// The engine entry point. A game constructs an App via make(), then calls
// run() to drive the semi-fixed timestep loop until the window closes.
// App owns the window, render context, world, clock, scheduler, system
// and render graphs, and the per-frame arena; the onSetup +
// onBuildGraphs callbacks in AppConfig are how gameplay code
// participates.
export class App {
  public:
    [[nodiscard]] static auto make(AppConfig cfg) -> Result<App> {
        auto window = Window::make(cfg.window);
        if (!window) {
            return std::unexpected(window.error());
        }
        auto render = RenderContext::make(*window);
        if (!render) {
            return std::unexpected(render.error());
        }
        AssetCache assets{cfg.assetRoot};
        const unsigned workers = cfg.workerThreads == 0 ? defaultWorkerCount() : cfg.workerThreads;
        FrameArena arena{cfg.frameArenaBytes};
        Scheduler scheduler{workers};
        JoltWorld physics = JoltWorld::make();
        auto audio = AudioDevice::make();
        if (!audio) {
            return std::unexpected(audio.error());
        }
        return App{
            std::move(*window),
            std::move(*render),
            std::move(assets),
            std::move(physics),
            std::move(*audio),
            std::move(scheduler),
            std::move(arena),
            std::move(cfg)
        };
    }

    App(const App&) = delete;
    auto operator=(const App&) -> App& = delete;
    App(App&&) noexcept = default;
    auto operator=(App&&) noexcept -> App& = default;
    ~App() = default;

    auto run() -> Result<void> {
        window.setResizeCallback([this](int w, int h) { render.resize(w, h); });
        installJoltWorld(world, physics);
        installAudioDevice(world, audio);

        if (cfg.onSetup) {
            auto setupResult = cfg.onSetup(world, assets);
            if (!setupResult) {
                return std::unexpected(setupResult.error());
            }
        }

        if (cfg.onBuildGraphs) {
            cfg.onBuildGraphs(fixedGraph, renderGraph, arena);
        }
        fixedGraph.compile();

        spdlog::info(
            "roboslop: entering main loop (workers={}, fixedSystems={}, passes={})",
            scheduler.workerCount(),
            fixedGraph.size(),
            renderGraph.size()
        );
        clock.reset();

        while (!window.shouldClose()) {
            arena.reset();
            pollWindowEvents();
            input.beginFrame();

            if (input.keyPressed(Key::Escape)) {
                window.requestClose();
            }

            const double dt = clock.tickFrame();
            const int steps = ticker.advance(dt);
            for (int i = 0; i < steps; ++i) {
                (void)i;
                SystemCtx ctx{
                    .world = &world,
                    .input = &input,
                    .dt = ticker.fixedDelta(),
                    .alpha = 0.0,
                    .stage = FrameStage::FixedUpdate,
                };
                scheduler.run(fixedGraph, ctx);
            }

            render.beginFrame();
            renderGraph.execute(world, render);
            render.endFrame();
        }

        spdlog::info("roboslop: main loop exited");
        return {};
    }

  private:
    App(Window window,
        RenderContext render,
        AssetCache assets,
        JoltWorld physics,
        AudioDevice audio,
        Scheduler scheduler,
        FrameArena arena,
        AppConfig cfg) noexcept
        : window(std::move(window)), render(std::move(render)), input(this->window),
          assets(std::move(assets)), physics(std::move(physics)), audio(std::move(audio)),
          ticker(cfg.tickRateHz), scheduler(std::move(scheduler)), arena(std::move(arena)),
          cfg(std::move(cfg)) {}

    // Member order is destruction-critical: assets must outlive any
    // entity that stores its handles (world) and must die before bgfx
    // shuts down (render). Declared order = construction order =
    // reverse destruction order. input stores only the raw GLFWwindow
    // handle, which is stable across Window moves, so it carries no
    // destruction dependency of its own. physics is declared before
    // world so world destructs first — clearing entt's ctx<JoltWorld*>
    // entry before the JoltWorld itself tears down Jolt globals.
    // fixedGraph and renderGraph hold lambdas captured from
    // onBuildGraphs that may reference arena and world by pointer; both
    // die before those targets do.
    Window window;
    RenderContext render;
    Input input;
    AssetCache assets;
    JoltWorld physics;
    AudioDevice audio;
    World world;
    Clock clock;
    FixedTimestep ticker;
    Scheduler scheduler;
    FrameArena arena;
    SystemGraph fixedGraph;
    RenderGraph renderGraph;
    AppConfig cfg;
};

} // namespace roboslop
