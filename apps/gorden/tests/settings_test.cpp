import gorden.settings;

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>

TEST_CASE("settings round-trip through JSON", "[gorden][settings]") {
    gorden::GordenSettings s;
    s.playerName = "Anna";
    s.robotName = "Gorden";
    s.stackWidth = 512.0F;
    s.windows["robot"] = {.visible = false, .height = 200.0F, .collapsed = true};
    s.windows["terminal"] = {.visible = true, .height = 420.0F, .collapsed = false};

    const auto back = gorden::fromJson(gorden::toJson(s));
    REQUIRE(back.playerName == "Anna");
    REQUIRE(back.robotName == "Gorden");
    REQUIRE(back.stackWidth == 512.0F);
    REQUIRE(back.windows.at("robot") == gorden::WindowLayout{false, 200.0F, true});
    REQUIRE(back.windows.at("terminal") == gorden::WindowLayout{true, 420.0F, false});
}

TEST_CASE("fromJson accepts the pre-stack visibility-only window format", "[gorden][settings]") {
    const auto s =
        gorden::fromJson(nlohmann::json::parse(R"({"windows": {"robot": false, "x": 1}})"));
    REQUIRE(s.windows.at("robot").visible == false);
    REQUIRE(s.windows.at("robot").height == 300.0F);
    REQUIRE_FALSE(s.windows.contains("x"));
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
