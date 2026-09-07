module;

#include <nlohmann/json.hpp>

#include <expected>
#include <filesystem>
#include <set>
#include <string>
#include <utility>
#include <vector>

export module roboslop.scene.savegame;
import roboslop.core.error;
import roboslop.core.json_file;
import roboslop.scene.document;
import roboslop.scene.transform;

namespace roboslop {

// One scene object as it stood when the game was saved. Only the
// transform: everything else about the object still comes from the
// scene file, which the save refers to rather than copies.
export struct SavedObject {
    std::string id;
    Transform transform{};
};

// A saved run of an application. The engine owns the frame — which
// scene, where its objects ended up — and the application owns `app`,
// an opaque JSON payload for whatever else it needs to restore (Gorden
// puts the robot, the player and the agent's memory there). The engine
// never looks inside it, so apps evolve their own state without
// bumping this format's version.
export struct SaveGame {
    std::string scene; // scene file, relative to the asset root
    std::vector<SavedObject> objects{};
    nlohmann::json app = nlohmann::json::object();
};

export [[nodiscard]] auto saveError(std::string context) -> Error {
    return {
        .category = "roboslop.scene.savegame",
        .code = 1,
        .message = "invalid save",
        .context = std::move(context)
    };
}

export [[nodiscard]] auto validateSaveGame(const SaveGame& save) -> Result<void> {
    if (!assetPathValid(save.scene)) {
        return std::unexpected(saveError("scene must be a relative path without .."));
    }
    if (save.objects.size() > 2000) {
        return std::unexpected(saveError("limit: 2000 objects"));
    }
    if (!save.app.is_object()) {
        return std::unexpected(saveError("app payload must be a JSON object"));
    }
    std::set<std::string> ids;
    for (const auto& object : save.objects) {
        if (object.id.empty() || !ids.insert(object.id).second ||
            !transformValid(object.transform)) {
            return std::unexpected(saveError("object " + object.id + ": check ID and transform"));
        }
    }
    return {};
}

export [[nodiscard]] auto saveGameToJson(const SaveGame& save) -> nlohmann::json {
    nlohmann::json objects = nlohmann::json::array();
    for (const auto& o : save.objects) {
        objects.push_back({{"id", o.id}, {"transform", transformToJson(o.transform)}});
    }
    return {{"version", 1}, {"scene", save.scene}, {"objects", objects}, {"app", save.app}};
}

export [[nodiscard]] auto saveGameFromJson(const nlohmann::json& json) -> Result<SaveGame> {
    // nlohmann's checked conversion throws; translate at this input boundary.
    try {
        if (json.at("version") != 1 || !json.at("objects").is_array()) {
            return std::unexpected(saveError("unsupported version or invalid collections"));
        }
        SaveGame save{
            .scene = json.at("scene").get<std::string>(),
            .app = json.value("app", nlohmann::json::object()),
        };
        for (const auto& o : json.at("objects")) {
            save.objects.push_back({
                .id = o.at("id").get<std::string>(),
                .transform = transformFromJson(o.at("transform")),
            });
        }
        if (auto valid = validateSaveGame(save); !valid) {
            return std::unexpected(valid.error());
        }
        return save;
    } catch (const nlohmann::json::exception& e) {
        return std::unexpected(saveError(e.what()));
    }
}

export [[nodiscard]] auto loadSaveGame(const std::filesystem::path& path) -> Result<SaveGame> {
    auto json = readJsonFile(path);
    if (!json) {
        return std::unexpected(json.error());
    }
    return saveGameFromJson(*json);
}

export [[nodiscard]] auto saveSaveGame(const std::filesystem::path& path, const SaveGame& save)
    -> Result<void> {
    if (auto valid = validateSaveGame(save); !valid) {
        return valid;
    }
    return writeJsonFile(path, saveGameToJson(save));
}

} // namespace roboslop
