module;

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <expected>
#include <filesystem>
#include <string>
#include <utility>

export module gorden.llm_config;

import roboslop.core.error;
import roboslop.core.file;
import roboslop.core.json_file;
import roboslop.core.paths;

namespace gorden {

// How to reach a real model. Lives in its own file, apart from
// GordenSettings, because the settings document is exposed to the
// robot as /etc/gorden/settings.json and the key must never be. The
// file is read once at startup and never written by the app; empty
// model / baseUrl mean "use the backend default".
export struct LlmConfig {
    std::string apiKey;
    std::string model;
    std::string baseUrl;
};

export [[nodiscard]] auto llmConfigPath() -> std::filesystem::path {
    return roboslop::configDir() / "llm.json";
}

export [[nodiscard]] auto parseLlmConfig(const nlohmann::json& j) -> LlmConfig {
    LlmConfig c;
    if (!j.is_object()) {
        return c;
    }
    const auto str = [&](const char* key, std::string& out) {
        if (const auto it = j.find(key); it != j.end() && it->is_string()) {
            out = it->get<std::string>();
        }
    };
    str("apiKey", c.apiKey);
    str("model", c.model);
    str("baseUrl", c.baseUrl);
    return c;
}

// A missing file is not an error: an empty config is returned and the
// caller falls back to the environment or the scripted demo.
export [[nodiscard]] auto loadLlmConfig(const std::filesystem::path& path)
    -> roboslop::Result<LlmConfig> {
    auto doc = roboslop::readJsonFile(path);
    if (!doc) {
        if (doc.error().code == static_cast<int>(roboslop::FileError::Missing) &&
            doc.error().category == "roboslop.core.file") {
            return LlmConfig{};
        }
        return std::unexpected(doc.error());
    }
    return parseLlmConfig(*doc);
}

// Environment wins over the file so a one-off `OPENAI_API_KEY=... make
// gorden` still works and CI never needs a config file.
export auto applyEnvOverrides(LlmConfig& c) -> void {
    const auto env = [](const char* name, std::string& out) {
        if (const char* v = std::getenv(name); v != nullptr && v[0] != '\0') {
            out = v;
        }
    };
    env("OPENAI_API_KEY", c.apiKey);
    env("GORDEN_MODEL", c.model);
    env("OPENAI_BASE_URL", c.baseUrl);
}

} // namespace gorden
