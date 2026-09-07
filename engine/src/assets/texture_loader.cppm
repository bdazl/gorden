module;

#include <bgfx/bgfx.h>
#include <stb_image.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>

export module roboslop.assets.texture;

import roboslop.core.error;

namespace roboslop {

export enum class TextureError : int {
    FileMissing = 1,
    DecodeFailed = 2,
    InvalidDimensions = 3,
    BgfxCreateFailed = 4,
};

export [[nodiscard]] auto toError(TextureError e, std::string ctx = {}) -> Error {
    switch (e) {
    case TextureError::FileMissing:
        return {
            .category = "roboslop.assets.texture",
            .code = static_cast<int>(e),
            .message = "texture file not found",
            .context = std::move(ctx),
        };
    case TextureError::DecodeFailed:
        return {
            .category = "roboslop.assets.texture",
            .code = static_cast<int>(e),
            .message = "stb_image decode failed",
            .context = std::move(ctx),
        };
    case TextureError::InvalidDimensions:
        return {
            .category = "roboslop.assets.texture",
            .code = static_cast<int>(e),
            .message = "texture dimensions exceed bgfx limits",
            .context = std::move(ctx),
        };
    case TextureError::BgfxCreateFailed:
        return {
            .category = "roboslop.assets.texture",
            .code = static_cast<int>(e),
            .message = "bgfx::createTexture2D returned invalid handle",
            .context = std::move(ctx),
        };
    }
    return {
        .category = "roboslop.assets.texture",
        .code = 0,
        .message = "unknown TextureError",
        .context = std::move(ctx),
    };
}

// RAII handle around bgfx::TextureHandle. Mirrors the Program /
// Texture pattern: move-only, destructor calls bgfx::destroy. The
// AssetCache owns these and hands non-owning handles out to game code.
export class Texture {
  public:
    Texture() = default;

    Texture(const Texture&) = delete;
    auto operator=(const Texture&) -> Texture& = delete;

    Texture(Texture&& other) noexcept : handle(std::exchange(other.handle, Invalid)) {}

    auto operator=(Texture&& other) noexcept -> Texture& {
        if (this != &other) {
            destroy();
            handle = std::exchange(other.handle, Invalid);
        }
        return *this;
    }

    ~Texture() {
        destroy();
    }

    [[nodiscard]] auto bgfxHandle() const noexcept -> bgfx::TextureHandle {
        return handle;
    }

    [[nodiscard]] auto valid() const noexcept -> bool {
        return bgfx::isValid(handle);
    }

  private:
    friend auto loadTexture2D(const std::filesystem::path&) -> Result<Texture>;
    friend auto loadTexture2D(std::span<const std::byte>, std::string_view) -> Result<Texture>;

    explicit Texture(bgfx::TextureHandle h) noexcept : handle(h) {}

    auto destroy() noexcept -> void {
        if (bgfx::isValid(handle)) {
            bgfx::destroy(handle);
            handle = Invalid;
        }
    }

    static constexpr bgfx::TextureHandle Invalid{bgfx::kInvalidHandle};
    bgfx::TextureHandle handle{bgfx::kInvalidHandle};
};

namespace detail {

// Shared tail of both loaders: validate, copy into bgfx memory, free the
// stbi buffer, create the texture. `context` names the source in errors.
static auto uploadPixels(stbi_uc* pixels, int width, int height, const std::string& context)
    -> Result<bgfx::TextureHandle> {
    if (pixels == nullptr) {
        std::string ctx = context;
        if (const char* reason = stbi_failure_reason(); reason != nullptr) {
            ctx += ": ";
            ctx += reason;
        }
        return std::unexpected(toError(TextureError::DecodeFailed, std::move(ctx)));
    }
    if (width <= 0 || height <= 0 || width > 0xFFFF || height > 0xFFFF) {
        stbi_image_free(pixels);
        return std::unexpected(toError(TextureError::InvalidDimensions, context));
    }

    const auto bytes = static_cast<std::uint32_t>(width) * static_cast<std::uint32_t>(height) * 4U;
    const bgfx::Memory* mem = bgfx::copy(pixels, bytes);
    stbi_image_free(pixels);

    const auto handle = bgfx::createTexture2D(
        static_cast<std::uint16_t>(width),
        static_cast<std::uint16_t>(height),
        /*hasMips=*/false,
        /*numLayers=*/1,
        bgfx::TextureFormat::RGBA8,
        BGFX_TEXTURE_NONE,
        mem
    );
    if (!bgfx::isValid(handle)) {
        return std::unexpected(toError(TextureError::BgfxCreateFailed, context));
    }
    return handle;
}

} // namespace detail

// Decode any stb_image-supported format (PNG, JPG, TGA, BMP, ...) into
// a bgfx 2D RGBA8 texture. Pixels are copied into a bgfx::Memory and
// owned by bgfx after createTexture2D returns, so the stbi buffer is
// freed before we leave this function.
export [[nodiscard]] auto loadTexture2D(const std::filesystem::path& path) -> Result<Texture> {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return std::unexpected(toError(TextureError::FileMissing, path.string()));
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load(path.string().c_str(), &width, &height, &channels, /*desired=*/4);
    auto handle = detail::uploadPixels(pixels, width, height, path.string());
    if (!handle) {
        return std::unexpected(handle.error());
    }
    return Texture{*handle};
}

// Same, from an encoded image already in memory: textures packed inside
// a model file (.glb), or generated at runtime. `name` only labels
// errors.
export [[nodiscard]] auto loadTexture2D(std::span<const std::byte> encoded, std::string_view name)
    -> Result<Texture> {
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(
        reinterpret_cast<const stbi_uc*>(encoded.data()),
        static_cast<int>(encoded.size()),
        &width,
        &height,
        &channels,
        /*desired=*/4
    );
    auto handle = detail::uploadPixels(pixels, width, height, std::string{name});
    if (!handle) {
        return std::unexpected(handle.error());
    }
    return Texture{*handle};
}

} // namespace roboslop
