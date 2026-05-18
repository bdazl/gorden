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

    [[nodiscard]] auto input() noexcept -> Input& {
        return input_;
    }

    App(const App&) = delete;
    auto operator=(const App&) -> App& = delete;
    App(App&&) noexcept = default;
    auto operator=(App&&) noexcept -> App& = default;
    ~App() = default;

    [[nodiscard]] auto world() noexcept -> World& {
        return world_;
    }

    [[nodiscard]] auto assets() noexcept -> AssetCache& {
        return assets_;
    }

    [[nodiscard]] auto config() const noexcept -> const AppConfig& {
        return cfg_;
    }

    auto run() -> Result<void> {
        window_.setResizeCallback([this](int w, int h) { render_.resize(w, h); });

        if (cfg_.onSetup) {
            auto setupResult = cfg_.onSetup(world_, assets_);
            if (!setupResult) {
                return std::unexpected(setupResult.error());
            }
        }

        spdlog::info("roboslop: entering main loop");
        clock_.reset();

        while (!window_.shouldClose()) {
            pollWindowEvents();
            input_.beginFrame();

            if (input_.keyPressed(Key::Escape)) {
                window_.requestClose();
            }

            const double dt = clock_.tickFrame();
            const int steps = ticker_.advance(dt);
            for (int i = 0; i < steps; ++i) {
                (void)i;
                if (cfg_.onFixedUpdate) {
                    cfg_.onFixedUpdate(world_, input_, ticker_.fixedDelta());
                }
            }

            render_.beginFrame();
            if (cfg_.onRender) {
                cfg_.onRender(world_, render_, ticker_.alpha());
            }
            render_.endFrame();
        }

        spdlog::info("roboslop: main loop exited");
        return {};
    }

  private:
    App(Window window, RenderContext render, AssetCache assets, AppConfig cfg) noexcept
        : window_(std::move(window)), render_(std::move(render)), input_(window_),
          assets_(std::move(assets)), ticker_(cfg.tickRateHz), cfg_(std::move(cfg)) {}

    // Member order is destruction-critical: assets_ must outlive any
    // entity that stores its handles (world_) and must die before bgfx
    // shuts down (render_). Declared order = construction order = reverse
    // destruction order. input_ stores only the raw GLFWwindow handle,
    // which is stable across Window moves, so it carries no destruction
    // dependency of its own.
    Window window_;
    RenderContext render_;
    Input input_;
    AssetCache assets_;
    World world_;
    Clock clock_;
    FixedTimestep ticker_;
    AppConfig cfg_;
};

} // namespace roboslop
