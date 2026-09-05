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

import roboslop.assets.texture;
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
// mutated only from the render thread. Entries live until replaced via
// replaceProgram or until the cache is destroyed, which happens before
// bgfx::shutdown() in App's destruction order. There is no
// invalidation by file change: a caller that recompiles a shader hands
// the new Program to replaceProgram explicitly.
export class AssetCache {
  public:
    explicit AssetCache(std::filesystem::path assetRoot) noexcept
        : assetRoot(std::move(assetRoot)) {}

    AssetCache(const AssetCache&) = delete;
    auto operator=(const AssetCache&) -> AssetCache& = delete;

    AssetCache(AssetCache&& other) noexcept
        : assetRoot(std::move(other.assetRoot)), programs(std::move(other.programs)),
          textures(std::move(other.textures)), samplers(std::move(other.samplers)),
          uniforms(std::move(other.uniforms)) {}

    auto operator=(AssetCache&& other) noexcept -> AssetCache& {
        if (this != &other) {
            destroySamplers();
            assetRoot = std::move(other.assetRoot);
            programs = std::move(other.programs);
            textures = std::move(other.textures);
            samplers = std::move(other.samplers);
            uniforms = std::move(other.uniforms);
        }
        return *this;
    }

    ~AssetCache() {
        destroySamplers();
    }

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

    // Swap the Program stored under (vsName, fsName) for `next`, or
    // insert it when the key is new. Returns the new non-owning handle.
    // The previous Program is destroyed here; bgfx defers the actual
    // release until the current frame has been rendered, so draws
    // already submitted with the old handle stay valid. Handles copied
    // into components are NOT updated — call rebindProgram (in
    // roboslop.render.frontend) with the old and new handles.
    [[nodiscard]] auto
    replaceProgram(std::string_view vsName, std::string_view fsName, Program next)
        -> ProgramHandle {
        Key key{std::string{vsName}, std::string{fsName}};
        const auto handle = next.bgfxHandle();
        if (auto it = programs.find(key); it != programs.end()) {
            it->second = std::move(next);
        } else {
            programs.emplace(std::move(key), std::move(next));
        }
        return ProgramHandle{handle};
    }

    // Cached texture load. The path is interpreted relative to
    // assetRoot. Caller receives a non-owning bgfx handle; the cache
    // destroys the underlying Texture before bgfx::shutdown.
    [[nodiscard]] auto texture(std::string_view path) -> Result<bgfx::TextureHandle> {
        std::string key{path};
        if (auto it = textures.find(key); it != textures.end()) {
            return it->second.bgfxHandle();
        }
        auto loaded = loadTexture2D(assetRoot / std::filesystem::path{path});
        if (!loaded) {
            return std::unexpected(loaded.error());
        }
        const auto handle = loaded->bgfxHandle();
        textures.emplace(std::move(key), std::move(*loaded));
        return handle;
    }

    // Cached sampler uniform (e.g. s_albedo, s_normal). bgfx requires
    // sampler uniforms to outlive every draw that binds them; routing
    // their lifetime through the AssetCache means they die before
    // bgfx::shutdown via App's destruction order.
    [[nodiscard]] auto sampler(std::string_view name) -> bgfx::UniformHandle {
        std::string key{name};
        if (auto it = samplers.find(key); it != samplers.end()) {
            return it->second;
        }
        const auto h = bgfx::createUniform(key.c_str(), bgfx::UniformType::Sampler);
        samplers.emplace(std::move(key), h);
        return h;
    }

    // Cached value uniform (vec4 / mat4 / ...). Shares the sampler
    // map's destruction schedule. Caller picks the bgfx type at first
    // request; mismatched re-requests return the existing handle.
    [[nodiscard]] auto uniform(std::string_view name, bgfx::UniformType::Enum type)
        -> bgfx::UniformHandle {
        std::string key{name};
        if (auto it = uniforms.find(key); it != uniforms.end()) {
            return it->second;
        }
        const auto h = bgfx::createUniform(key.c_str(), type);
        uniforms.emplace(std::move(key), h);
        return h;
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

    auto destroyUniformMap(std::unordered_map<std::string, bgfx::UniformHandle>& m) noexcept
        -> void {
        for (auto& [_, handle] : m) {
            if (bgfx::isValid(handle)) {
                bgfx::destroy(handle);
                handle = bgfx::UniformHandle{bgfx::kInvalidHandle};
            }
        }
        m.clear();
    }

    auto destroySamplers() noexcept -> void {
        destroyUniformMap(samplers);
        destroyUniformMap(uniforms);
    }

    std::filesystem::path assetRoot;
    std::unordered_map<Key, Program, KeyHash> programs;
    std::unordered_map<std::string, Texture> textures;
    std::unordered_map<std::string, bgfx::UniformHandle> samplers;
    std::unordered_map<std::string, bgfx::UniformHandle> uniforms;
};

} // namespace roboslop
