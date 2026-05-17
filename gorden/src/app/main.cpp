import roboslop.app;
import roboslop.platform.window;

#include <print>

auto main() -> int {
    auto app = roboslop::App::make(
        roboslop::AppConfig{
            .window = roboslop::WindowConfig{.title = "gorden", .width = 1280, .height = 720},
            .tickRateHz = 60.0,
            .assetRoot = ".",
            .onSetup = {},
            .onFixedUpdate = {},
            .onRender = {},
        }
    );
    if (!app) {
        std::println(stderr, "app init failed: {}", app.error().message);
        return 1;
    }

    const auto result = app->run();
    if (!result) {
        std::println(stderr, "app exited with error: {}", result.error().message);
        return 1;
    }
    return 0;
}
