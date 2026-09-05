module;

#include <nlohmann/json.hpp>

#include <expected>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>

export module roboslop.core.json_file;

import roboslop.core.error;
import roboslop.core.file;

namespace roboslop {

export enum class JsonError : int {
    Parse = 1,
    WriteFailed = 2,
};

export [[nodiscard]] auto toError(JsonError e, std::string ctx = {}) -> Error {
    switch (e) {
    case JsonError::Parse:
        return {
            .category = "roboslop.core.json_file",
            .code = static_cast<int>(e),
            .message = "file is not valid JSON",
            .context = std::move(ctx)
        };
    case JsonError::WriteFailed:
        return {
            .category = "roboslop.core.json_file",
            .code = static_cast<int>(e),
            .message = "failed to write JSON file",
            .context = std::move(ctx)
        };
    }
    return {
        .category = "roboslop.core.json_file",
        .code = 0,
        .message = "unknown JsonError",
        .context = std::move(ctx)
    };
}

// A missing file comes back as FileError::Missing (from readFileBytes)
// so callers can treat "no settings yet" differently from "corrupt".
export [[nodiscard]] auto readJsonFile(const std::filesystem::path& path)
    -> Result<nlohmann::json> {
    auto bytes = readFileBytes(path);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    nlohmann::json doc = nlohmann::json::parse(*bytes, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded()) {
        return std::unexpected(toError(JsonError::Parse, path.string()));
    }
    return doc;
}

// Creates parent directories, writes to `<path>.tmp`, then renames over
// the target so a crash mid-write never leaves a half file.
export [[nodiscard]] auto
writeJsonFile(const std::filesystem::path& path, const nlohmann::json& doc) -> Result<void> {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const auto tmp = path.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return std::unexpected(toError(JsonError::WriteFailed, tmp));
        }
        out << doc.dump(2) << '\n';
        if (!out) {
            return std::unexpected(toError(JsonError::WriteFailed, tmp));
        }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        return std::unexpected(
            toError(JsonError::WriteFailed, path.string() + ": " + ec.message())
        );
    }
    return {};
}

} // namespace roboslop
