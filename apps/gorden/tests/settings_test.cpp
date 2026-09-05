import gorden.settings;

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>

TEST_CASE("settings round-trip through JSON", "[gorden][settings]") {
    gorden::GordenSettings s;
    s.playerName = "Anna";
    s.robotName = "Gorden";
    s.windows["robot"] = false;
    s.windows["terminal"] = true;

    const auto back = gorden::fromJson(gorden::toJson(s));
    REQUIRE(back.playerName == "Anna");
    REQUIRE(back.robotName == "Gorden");
    REQUIRE(back.windows.at("robot") == false);
    REQUIRE(back.windows.at("terminal") == true);
}

TEST_CASE("fromJson keeps defaults for missing or malformed keys", "[gorden][settings]") {
    const auto s = gorden::fromJson(nlohmann::json::parse(R"({"playerName": 42, "extra": 1})"));
    REQUIRE(s.playerName == "Player");
    REQUIRE(s.robotName == "Gorden");
    REQUIRE(s.windows.empty());
    REQUIRE(gorden::fromJson(nlohmann::json::array()).robotName == "Gorden");
}

TEST_CASE(
    "loadSettings returns defaults for a missing file and saves round-trip", "[gorden][settings]"
) {
    const auto dir = std::filesystem::temp_directory_path() / "roboslop-gorden-settings";
    std::filesystem::remove_all(dir);
    const auto path = dir / "gorden.json";

    const auto missing = gorden::loadSettings(path);
    REQUIRE(missing.has_value());
    REQUIRE(missing->playerName == "Player");

    gorden::GordenSettings s;
    s.playerName = "Jacob";
    REQUIRE(gorden::saveSettings(path, s).has_value());
    const auto loaded = gorden::loadSettings(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->playerName == "Jacob");
    std::filesystem::remove_all(dir);
}
