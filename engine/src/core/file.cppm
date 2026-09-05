module;

#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <utility>
#include <vector>

export module roboslop.core.file;

import roboslop.core.error;

namespace roboslop {

export enum class FileError : int {
    Missing = 1,
    ReadFailed = 2,
};

export [[nodiscard]] auto toError(FileError e, std::string ctx = {}) -> Error {
    switch (e) {
    case FileError::Missing:
        return {
            .category = "roboslop.core.file",
            .code = static_cast<int>(e),
            .message = "file not found",
            .context = std::move(ctx)
        };
    case FileError::ReadFailed:
        return {
            .category = "roboslop.core.file",
            .code = static_cast<int>(e),
            .message = "failed to read file",
            .context = std::move(ctx)
        };
    }
    return {
        .category = "roboslop.core.file",
        .code = 0,
        .message = "unknown FileError",
        .context = std::move(ctx)
    };
}

// Reads a whole file into memory. Shared by shader loading (compiled
// .bin blobs) and the runtime shader compiler (reading shaderc's
// output back); small enough that nothing fancier is warranted.
export [[nodiscard]] auto readFileBytes(const std::filesystem::path& path)
    -> Result<std::vector<char>> {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return std::unexpected(toError(FileError::Missing, path.string()));
    }
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        return std::unexpected(toError(FileError::Missing, path.string()));
    }
    const auto size = static_cast<std::size_t>(in.tellg());
    in.seekg(0, std::ios::beg);
    std::vector<char> buf(size);
    if (size > 0 && !in.read(buf.data(), static_cast<std::streamsize>(size))) {
        return std::unexpected(toError(FileError::ReadFailed, path.string()));
    }
    return buf;
}

} // namespace roboslop
