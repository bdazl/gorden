import roboslop.core.version;

#include <spdlog/spdlog.h>

#include <print>

auto main() -> int {
    std::println("gorden, on roboslop {}", roboslop::version());
    spdlog::info("hello from spdlog");
    return 0;
}
