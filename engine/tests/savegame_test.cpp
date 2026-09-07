import roboslop.core.file;
import roboslop.scene.savegame;

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <string>

namespace {

[[nodiscard]] auto sample() -> roboslop::SaveGame {
    roboslop::SaveGame save{.scene = "scenes/room.json"};
    save.objects.push_back({.id = "crate"});
    save.objects[0].transform.position = {1.0F, 0.0F, -2.0F};
    save.app = {{"memory", {{"episodes", nlohmann::json::array()}}}};
    return save;
}

} // namespace

TEST_CASE("A save round trips through JSON with its app payload intact", "[scene][savegame]") {
    const auto save = sample();
    const auto json = roboslop::saveGameToJson(save);
    REQUIRE(json["version"] == 1);
    auto decoded = roboslop::saveGameFromJson(json);
    REQUIRE(decoded);
    REQUIRE(roboslop::saveGameToJson(*decoded) == json);
    REQUIRE(decoded->app["memory"]["episodes"].is_array());
}

TEST_CASE("Saving and reloading a save file preserves it", "[scene][savegame]") {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto dir =
        std::filesystem::temp_directory_path() / ("roboslop-savegame-" + std::to_string(unique));
    const auto file = dir / "default.json";
    const auto save = sample();
    REQUIRE(roboslop::saveSaveGame(file, save));
    const auto reopened = roboslop::loadSaveGame(file);
    REQUIRE(reopened);
    REQUIRE(roboslop::saveGameToJson(*reopened) == roboslop::saveGameToJson(save));
    REQUIRE_FALSE(std::filesystem::exists(file.string() + ".tmp"));
    std::filesystem::remove(file);
    std::filesystem::remove(dir);
}

TEST_CASE("A save is rejected before it reaches disk", "[scene][savegame]") {
    auto save = sample();
    save.scene = "../outside/room.json";
    REQUIRE_FALSE(roboslop::validateSaveGame(save));
    save.scene = "scenes/room.json";
    save.objects.push_back({.id = "crate"});
    REQUIRE_FALSE(roboslop::validateSaveGame(save)); // duplicate ID
    save.objects.pop_back();
    save.objects[0].transform.scale.x = 0.0F;
    REQUIRE_FALSE(roboslop::validateSaveGame(save));
    save.objects[0].transform.scale.x = 1.0F;
    save.app = nlohmann::json::array();
    REQUIRE_FALSE(roboslop::validateSaveGame(save));
}

TEST_CASE("An unsupported version and a missing file are distinct failures", "[scene][savegame]") {
    auto json = roboslop::saveGameToJson(sample());
    json["version"] = 2;
    REQUIRE_FALSE(roboslop::saveGameFromJson(json));

    const auto missing = std::filesystem::temp_directory_path() / "roboslop-savegame-missing.json";
    std::filesystem::remove(missing);
    const auto loaded = roboslop::loadSaveGame(missing);
    REQUIRE_FALSE(loaded);
    REQUIRE(loaded.error().category == "roboslop.core.file");
    REQUIRE(loaded.error().code == static_cast<int>(roboslop::FileError::Missing));
}
