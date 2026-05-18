module;

#include <bgfx/bgfx.h>

#include <cstddef>
#include <expected>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

export module roboslop.render.asset_cache;

import roboslop.core.error;
import roboslop.render.shader;

namespace roboslop {

// Non-owning view of a bgfx::ProgramHandle managed by an AssetCache. Safe
// to store in Mesh components or copy freely; the cache outlives the bgfx
// device, so handles stay valid for the lifetime of the App.
export struct ProgramHandle {
    bgfx::ProgramHandle value{bgfx::kInvalidHandle};

    [[nodiscard]] auto valid() const noexcept -> bool {
        return bgfx::isValid(value);
    }
};

// Owns Programs keyed by (vsName, fsName). Single-threaded — read and
// mutated only from the render thread. No hot-reload, no invalidation:
// entries live until the cache is destroyed, which happens before
// bgfx::shutdown() in App's destruction order.
export class AssetCache {
  public:
    explicit AssetCache(std::filesystem::path assetRoot) noexcept
        : assetRoot(std::move(assetRoot)) {}

    AssetCache(const AssetCache&) = delete;
    auto operator=(const AssetCache&) -> AssetCache& = delete;
    AssetCache(AssetCache&&) noexcept = default;
    auto operator=(AssetCache&&) noexcept -> AssetCache& = default;
    ~AssetCache() = default;

    [[nodiscard]] auto program(std::string_view vsName, std::string_view fsName)
        -> Result<ProgramHandle> {
        Key key{std::string{vsName}, std::string{fsName}};
        if (auto it = programs.find(key); it != programs.end()) {
            return ProgramHandle{it->second.bgfxHandle()};
        }
        auto loaded = loadProgram(assetRoot, vsName, fsName);
        if (!loaded) {
            return std::unexpected(loaded.error());
        }
        const auto handle = loaded->bgfxHandle();
        programs.emplace(std::move(key), std::move(*loaded));
        return ProgramHandle{handle};
    }

  private:
    struct Key {
        std::string vs;
        std::string fs;
        auto operator==(const Key&) const -> bool = default;
    };

    struct KeyHash {
        auto operator()(const Key& k) const noexcept -> std::size_t {
            // Boost-style hash combine.
            const std::size_t h1 = std::hash<std::string>{}(k.vs);
            const std::size_t h2 = std::hash<std::string>{}(k.fs);
            return h1 ^ (h2 + 0x9e3779b9U + (h1 << 6U) + (h1 >> 2U));
        }
    };

    std::filesystem::path assetRoot;
    std::unordered_map<Key, Program, KeyHash> programs;
};

} // namespace roboslop
