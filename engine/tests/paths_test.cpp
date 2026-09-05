import roboslop.core.paths;

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

// Sets an environment variable for the duration of a test.
struct EnvGuard {
    std::string name;
    std::string previous;
    bool had = false;

    EnvGuard(const char* n, const char* value) : name(n) {
        if (const char* p = std::getenv(n); p != nullptr) {
            had = true;
            previous = p;
        }
        setenv(n, value, 1);
    }

    ~EnvGuard() {
        if (had) {
            setenv(name.c_str(), previous.c_str(), 1);
        } else {
            unsetenv(name.c_str());
        }
    }

    EnvGuard(const EnvGuard&) = delete;
    auto operator=(const EnvGuard&) -> EnvGuard& = delete;
    EnvGuard(EnvGuard&&) = delete;
    auto operator=(EnvGuard&&) -> EnvGuard& = delete;
};

} // namespace

TEST_CASE("configDir and dataDir honour the XDG variables", "[core][paths]") {
    const EnvGuard c("XDG_CONFIG_HOME", "/tmp/xdg-config");
    const EnvGuard d("XDG_DATA_HOME", "/tmp/xdg-data");
    REQUIRE(roboslop::configDir() == std::filesystem::path{"/tmp/xdg-config/roboslop"});
    REQUIRE(roboslop::dataDir() == std::filesystem::path{"/tmp/xdg-data/roboslop"});
}

TEST_CASE("configDir and dataDir fall back to HOME", "[core][paths]") {
    const EnvGuard c("XDG_CONFIG_HOME", "");
    const EnvGuard d("XDG_DATA_HOME", "");
    const EnvGuard h("HOME", "/home/tester");
    REQUIRE(roboslop::configDir() == std::filesystem::path{"/home/tester/.config/roboslop"});
    REQUIRE(roboslop::dataDir() == std::filesystem::path{"/home/tester/.local/share/roboslop"});
}

TEST_CASE("ensureDir creates nested directories and tolerates existing ones", "[core][paths]") {
    const auto dir = std::filesystem::temp_directory_path() / "roboslop-paths-test" / "a" / "b";
    std::filesystem::remove_all(dir.parent_path().parent_path());
    REQUIRE(roboslop::ensureDir(dir).has_value());
    REQUIRE(std::filesystem::is_directory(dir));
    REQUIRE(roboslop::ensureDir(dir).has_value());
    std::filesystem::remove_all(dir.parent_path().parent_path());
}
