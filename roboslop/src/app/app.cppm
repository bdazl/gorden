module;

#include <spdlog/spdlog.h>

#include <expected>
#include <utility>

export module roboslop.app;

import roboslop.core.error;
import roboslop.platform.window;
import roboslop.render.context;
import roboslop.time.clock;

namespace roboslop {

export struct AppConfig {
    WindowConfig window;
    double tickRateHz = 60.0;
};

// The engine entry point. A game constructs an App via make(), then calls
// run() to drive the semi-fixed timestep loop until the window closes. App
// owns the window, render context, and clock; subsystems that need to tick
// will hook in through here as they land.
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
        return App{std::move(*window), std::move(*render), cfg.tickRateHz};
    }

    App(const App&) = delete;
    auto operator=(const App&) -> App& = delete;
    App(App&&) noexcept = default;
    auto operator=(App&&) noexcept -> App& = default;
    ~App() = default;

    auto run() -> Result<void> {
        spdlog::info("roboslop: entering main loop");
        clock_.reset();
        auto [previousW, previousH] = window_.framebufferSize();

        while (!window_.shouldClose()) {
            pollWindowEvents();

            if (window_.escapePressed()) {
                window_.requestClose();
            }

            const auto [w, h] = window_.framebufferSize();
            if (w != previousW || h != previousH) {
                render_.resize(w, h);
                previousW = w;
                previousH = h;
            }

            const double dt = clock_.tickFrame();
            const int steps = ticker_.advance(dt);
            for (int i = 0; i < steps; ++i) {
                // Fixed update — empty until the first subsystem (ECS,
                // physics) needs a tick slot. Keeping the slot present
                // means those land without an API rework.
                (void)i;
            }

            render_.beginFrame();
            render_.endFrame();
        }
        spdlog::info("roboslop: main loop exited");
        return {};
    }

  private:
    App(Window window, RenderContext render, double tickRateHz) noexcept
        : window_(std::move(window)), render_(std::move(render)), ticker_(tickRateHz) {}

    Window window_;
    RenderContext render_;
    Clock clock_;
    FixedTimestep ticker_;
};

} // namespace roboslop
