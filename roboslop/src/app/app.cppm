module;

#include <spdlog/spdlog.h>

#include <expected>
#include <filesystem>
#include <functional>
#include <utility>

export module roboslop.app;

import roboslop.core.error;
import roboslop.ecs;
import roboslop.platform.input;
import roboslop.platform.window;
import roboslop.render.asset_cache;
import roboslop.render.context;
import roboslop.time.clock;

namespace roboslop {

// Game-supplied hooks. Lambdas (or any other invocable) plug in at three
// well-defined moments; the engine owns the loop ordering.
//
//   onSetup       — called once after window + bgfx init, before the loop.
//                   Receives the World and the App-owned AssetCache so
//                   game code can request programs without managing
//                   handle lifetimes. Returns Result<void> so asset-load
//                   failures abort start-up cleanly.
//   onFixedUpdate — called N times per frame, once per fixed sub-step at
//                   the configured rate (default 60 Hz). Receives a
//                   mutable Input reference; the same per-frame snapshot
//                   is reused across all sub-steps in one render frame.
//   onRender      — called once per frame, between bgfx beginFrame /
//                   endFrame. Receives the interpolation alpha in [0, 1).
export struct AppConfig {
    WindowConfig window;
    double tickRateHz = 60.0;
    std::filesystem::path assetRoot = ".";

    std::function<Result<void>(World&, AssetCache&)> onSetup;
    std::function<void(World&, Input&, double)> onFixedUpdate;
    std::function<void(World&, RenderContext&, double)> onRender;
};

// The engine entry point. A game constructs an App via make(), then calls
// run() to drive the semi-fixed timestep loop until the window closes.
// App owns the window, render context, world, and clock; game callbacks
// in AppConfig are how gameplay code participates.
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
        return App{std::move(*window), std::move(*render), std::move(assets), std::move(cfg)};
    }

    App(const App&) = delete;
    auto operator=(const App&) -> App& = delete;
    App(App&&) noexcept = default;
    auto operator=(App&&) noexcept -> App& = default;
    ~App() = default;

    auto run() -> Result<void> {
        window.setResizeCallback([this](int w, int h) { render.resize(w, h); });

        if (cfg.onSetup) {
            auto setupResult = cfg.onSetup(world, assets);
            if (!setupResult) {
                return std::unexpected(setupResult.error());
            }
        }

        spdlog::info("roboslop: entering main loop");
        clock.reset();

        while (!window.shouldClose()) {
            pollWindowEvents();
            input.beginFrame();

            if (input.keyPressed(Key::Escape)) {
                window.requestClose();
            }

            const double dt = clock.tickFrame();
            const int steps = ticker.advance(dt);
            for (int i = 0; i < steps; ++i) {
                (void)i;
                if (cfg.onFixedUpdate) {
                    cfg.onFixedUpdate(world, input, ticker.fixedDelta());
                }
            }

            render.beginFrame();
            if (cfg.onRender) {
                cfg.onRender(world, render, ticker.alpha());
            }
            render.endFrame();
        }

        spdlog::info("roboslop: main loop exited");
        return {};
    }

  private:
    App(Window window, RenderContext render, AssetCache assets, AppConfig cfg) noexcept
        : window(std::move(window)), render(std::move(render)), input(this->window),
          assets(std::move(assets)), ticker(cfg.tickRateHz), cfg(std::move(cfg)) {}

    // Member order is destruction-critical: assets must outlive any
    // entity that stores its handles (world) and must die before bgfx
    // shuts down (render). Declared order = construction order = reverse
    // destruction order. input stores only the raw GLFWwindow handle,
    // which is stable across Window moves, so it carries no destruction
    // dependency of its own.
    Window window;
    RenderContext render;
    Input input;
    AssetCache assets;
    World world;
    Clock clock;
    FixedTimestep ticker;
    AppConfig cfg;
};

} // namespace roboslop
