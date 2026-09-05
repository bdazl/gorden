module;

#include <nlohmann/json.hpp>

#include <expected>
#include <filesystem>
#include <map>
#include <string>
#include <utility>

export module gorden.settings;

import roboslop.core.error;
import roboslop.core.file;
import roboslop.core.json_file;
import roboslop.core.paths;

namespace gorden {

// Everything the player can change and expects to survive a restart.
// Stored as JSON under the XDG config dir (see settingsPath). Unknown
// keys are ignored on load so older files keep working; missing keys
// keep their defaults.
export struct GordenSettings {
    std::string playerName = "Player";
    std::string robotName = "Gorden";
    std::map<std::string, bool> windows{}; // dev-window id → visible
};

export [[nodiscard]] auto toJson(const GordenSettings& s) -> nlohmann::json {
    nlohmann::json j;
    j["playerName"] = s.playerName;
    j["robotName"] = s.robotName;
    j["windows"] = s.windows;
    return j;
}

export [[nodiscard]] auto fromJson(const nlohmann::json& j) -> GordenSettings {
    GordenSettings s;
    if (!j.is_object()) {
        return s;
    }
    if (const auto it = j.find("playerName"); it != j.end() && it->is_string()) {
        s.playerName = it->get<std::string>();
    }
    if (const auto it = j.find("robotName"); it != j.end() && it->is_string()) {
        s.robotName = it->get<std::string>();
    }
    if (const auto it = j.find("windows"); it != j.end() && it->is_object()) {
        for (const auto& [id, visible] : it->items()) {
            if (visible.is_boolean()) {
                s.windows[id] = visible.get<bool>();
            }
        }
    }
    return s;
}

export [[nodiscard]] auto settingsPath() -> std::filesystem::path {
    return roboslop::configDir() / "gorden.json";
}

// A missing file is not an error: defaults are returned. A corrupt
// file is, so the caller can warn and still run with defaults.
export [[nodiscard]] auto loadSettings(const std::filesystem::path& path)
    -> roboslop::Result<GordenSettings> {
    auto doc = roboslop::readJsonFile(path);
    if (!doc) {
        if (doc.error().code == static_cast<int>(roboslop::FileError::Missing) &&
            doc.error().category == "roboslop.core.file") {
            return GordenSettings{};
        }
        return std::unexpected(doc.error());
    }
    return fromJson(*doc);
}

export [[nodiscard]] auto saveSettings(const std::filesystem::path& path, const GordenSettings& s)
    -> roboslop::Result<void> {
    return roboslop::writeJsonFile(path, toJson(s));
}

} // namespace gorden
