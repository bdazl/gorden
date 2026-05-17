module;

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

#include <cstdint>
#include <expected>
#include <utility>

export module roboslop.render.context;

import roboslop.core.error;
import roboslop.platform.window;

namespace roboslop {

export enum class RenderError : int {
    InitFailed = 1,
};

export [[nodiscard]] auto toError(RenderError e) -> Error {
    switch (e) {
    case RenderError::InitFailed:
        return {
            .category = "roboslop.render.context",
            .code = static_cast<int>(e),
            .message = "bgfx::init failed",
            .context = {}
        };
    }
    return {
        .category = "roboslop.render.context",
        .code = 0,
        .message = "unknown RenderError",
        .context = {}
    };
}

export struct RenderConfig {
    std::uint32_t clearColor = 0x303060ffU;
    std::uint32_t resetFlags = BGFX_RESET_VSYNC;
};

// Owns the bgfx device. Construction binds to a Window; only one instance may
// exist per process (bgfx is a global singleton). Submits view 0 with a clear
// every frame so a default render produces a visible backbuffer without any
// draw calls.
export class RenderContext {
  public:
    [[nodiscard]] static auto make(const Window& window, RenderConfig cfg = {})
        -> Result<RenderContext> {
        const auto handles = window.nativeHandles();
        const auto [w, h] = window.framebufferSize();

        bgfx::Init init;
        init.type = bgfx::RendererType::Count;
        init.resolution.width = static_cast<std::uint32_t>(w);
        init.resolution.height = static_cast<std::uint32_t>(h);
        init.resolution.reset = cfg.resetFlags;
        init.platformData.nwh = handles.window;
        init.platformData.ndt = handles.display;
        if (handles.platform == NativePlatform::Wayland) {
            init.platformData.type = bgfx::NativeWindowHandleType::Wayland;
        }

        if (!bgfx::init(init)) {
            return std::unexpected(toError(RenderError::InitFailed));
        }
        bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, cfg.clearColor, 1.0F, 0);
        bgfx::setViewRect(0, 0, 0, static_cast<std::uint16_t>(w), static_cast<std::uint16_t>(h));
        return RenderContext{w, h, cfg};
    }

    RenderContext(const RenderContext&) = delete;
    auto operator=(const RenderContext&) -> RenderContext& = delete;

    RenderContext(RenderContext&& other) noexcept
        : alive_(std::exchange(other.alive_, false)), width_(other.width_), height_(other.height_),
          cfg_(other.cfg_) {}

    auto operator=(RenderContext&& other) noexcept -> RenderContext& {
        if (this != &other) {
            shutdown();
            alive_ = std::exchange(other.alive_, false);
            width_ = other.width_;
            height_ = other.height_;
            cfg_ = other.cfg_;
        }
        return *this;
    }

    ~RenderContext() {
        shutdown();
    }

    auto beginFrame() noexcept -> void {
        // touch ensures view 0 is submitted even when nothing draws, so the
        // clear actually paints the backbuffer.
        bgfx::touch(0);
    }

    auto endFrame() noexcept -> void {
        bgfx::frame();
    }

    [[nodiscard]] auto width() const noexcept -> int {
        return width_;
    }

    [[nodiscard]] auto height() const noexcept -> int {
        return height_;
    }

    auto resize(int width, int height) noexcept -> void {
        if (width == width_ && height == height_) {
            return;
        }
        width_ = width;
        height_ = height;
        bgfx::reset(
            static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), cfg_.resetFlags
        );
        bgfx::setViewRect(
            0, 0, 0, static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height)
        );
    }

  private:
    RenderContext(int w, int h, RenderConfig cfg) noexcept
        : alive_(true), width_(w), height_(h), cfg_(cfg) {}

    auto shutdown() noexcept -> void {
        if (alive_) {
            bgfx::shutdown();
            alive_ = false;
        }
    }

    bool alive_ = false;
    int width_ = 0;
    int height_ = 0;
    RenderConfig cfg_{};
};

} // namespace roboslop
