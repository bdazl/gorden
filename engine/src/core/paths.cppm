module;

#include <cstdlib>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

export module roboslop.core.paths;

import roboslop.core.error;

namespace roboslop {

namespace detail {

[[nodiscard]] auto envPath(const char* name) -> std::filesystem::path {
    const char* v = std::getenv(name);
    return (v != nullptr && v[0] != '\0') ? std::filesystem::path{v} : std::filesystem::path{};
}

[[nodiscard]] auto homeDir() -> std::filesystem::path {
    auto home = envPath("HOME");
    if (home.empty()) {
        home = envPath("USERPROFILE");
    }
    return home.empty() ? std::filesystem::current_path() : home;
}

} // namespace detail

// Per-user locations following the XDG base directory spec, all under
// a `roboslop/` subdirectory so every app shares one place. Nothing is
// created here; call ensureDir() before writing.
//
//   configDir(): $XDG_CONFIG_HOME/roboslop or ~/.config/roboslop
//   dataDir():   $XDG_DATA_HOME/roboslop   or ~/.local/share/roboslop
export [[nodiscard]] auto configDir() -> std::filesystem::path {
    auto base = detail::envPath("XDG_CONFIG_HOME");
    if (base.empty()) {
        base = detail::homeDir() / ".config";
    }
    return base / "roboslop";
}

export [[nodiscard]] auto dataDir() -> std::filesystem::path {
    auto base = detail::envPath("XDG_DATA_HOME");
    if (base.empty()) {
        base = detail::homeDir() / ".local" / "share";
    }
    return base / "roboslop";
}

export enum class PathError : int {
    CreateFailed = 1,
};

export [[nodiscard]] auto toError(PathError e, std::string ctx = {}) -> Error {
    switch (e) {
    case PathError::CreateFailed:
        return {
            .category = "roboslop.core.paths",
            .code = static_cast<int>(e),
            .message = "failed to create directory",
            .context = std::move(ctx)
        };
    }
    return {
        .category = "roboslop.core.paths",
        .code = 0,
        .message = "unknown PathError",
        .context = std::move(ctx)
    };
}

// mkdir -p. Succeeds when the directory already exists.
export [[nodiscard]] auto ensureDir(const std::filesystem::path& dir) -> Result<void> {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        return std::unexpected(
            toError(PathError::CreateFailed, dir.string() + ": " + ec.message())
        );
    }
    return {};
}

} // namespace roboslop
