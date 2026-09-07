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

// What bgfx reports about the last frame it finished. That frame trails
// the CPU frame the caller just submitted, so these are comparable to
// each other rather than to the caller's own wall-clock timings.
// waitSubmit/waitRender are how long each side of bgfx's submit/render
// split waited for the other — zero on both when bgfx is single-threaded.
export struct GpuFrameStats {
    double gpuMs = 0.0;
    double waitSubmitMs = 0.0;
    double waitRenderMs = 0.0;
    std::uint32_t drawCalls = 0;
    std::uint16_t backbufferWidth = 0;
    std::uint16_t backbufferHeight = 0;
};

export struct RenderConfig {
    std::uint32_t clearColor = 0x303060ffU;
    std::uint32_t resetFlags = BGFX_RESET_VSYNC;
};

// Benchmarks measure how fast the engine can produce frames, which vsync
// hides by pacing them to the display. Spelled here because BGFX_RESET_*
// lives behind this module.
export [[nodiscard]] auto withoutVsync(RenderConfig cfg) noexcept -> RenderConfig {
    cfg.resetFlags &= ~static_cast<std::uint32_t>(BGFX_RESET_VSYNC);
    return cfg;
}

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

        // NVIDIA's Wayland WSI loses the VkSurfaceKHR when GLFW commits the
        // wl_surface from the main thread — which it does on every configure
        // event, so a resize, a fullscreen toggle or a plain focus change is
        // enough — while bgfx's render thread is presenting. Calling
        // renderFrame() before init puts bgfx in single-threaded mode, so
        // every Vulkan call runs on the thread that owns the window and the
        // race cannot happen. Other platforms keep the render thread.
        if (handles.platform == NativePlatform::Wayland) {
            bgfx::renderFrame();
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
        : alive(std::exchange(other.alive, false)), width(other.width), height(other.height),
          cfg(other.cfg) {}

    auto operator=(RenderContext&& other) noexcept -> RenderContext& {
        if (this != &other) {
            shutdown();
            alive = std::exchange(other.alive, false);
            width = other.width;
            height = other.height;
            cfg = other.cfg;
        }
        return *this;
    }

    ~RenderContext() {
        shutdown();
    }

    static auto beginFrame() noexcept -> void {
        // touch ensures view 0 is submitted even when nothing draws, so the
        // clear actually paints the backbuffer.
        bgfx::touch(0);
    }

    static auto endFrame() noexcept -> void {
        bgfx::frame();
    }

    // Whether bgfx runs its own render thread. Fixed for the life of the
    // context; reported in the performance overlay so a measurement can
    // be attributed to a threading model.
    [[nodiscard]] static auto multiThreaded() noexcept -> bool {
        const bgfx::Caps* caps = bgfx::getCaps();
        return caps != nullptr && (caps->supported & BGFX_CAPS_RENDERER_MULTITHREADED) != 0;
    }

    // Call after endFrame(). Converts bgfx's raw timer ticks to
    // milliseconds; a zero timer frequency (no GPU timer support) yields
    // zero rather than a division by zero.
    [[nodiscard]] static auto gpuStats() noexcept -> GpuFrameStats {
        const bgfx::Stats* s = bgfx::getStats();
        if (s == nullptr) {
            return {};
        }
        const auto toMs = [](std::int64_t ticks, std::int64_t freq) {
            return freq == 0 ? 0.0
                             : (static_cast<double>(ticks) * 1000.0) / static_cast<double>(freq);
        };
        return {
            .gpuMs = toMs(s->gpuTimeEnd - s->gpuTimeBegin, s->gpuTimerFreq),
            .waitSubmitMs = toMs(s->waitSubmit, s->cpuTimerFreq),
            .waitRenderMs = toMs(s->waitRender, s->cpuTimerFreq),
            .drawCalls = s->numDraw,
            .backbufferWidth = s->width,
            .backbufferHeight = s->height,
        };
    }

    [[nodiscard]] auto framebufferWidth() const noexcept -> int {
        return width;
    }

    [[nodiscard]] auto framebufferHeight() const noexcept -> int {
        return height;
    }

    auto resize(int newWidth, int newHeight) noexcept -> void {
        if (newWidth == width && newHeight == height) {
            return;
        }
        width = newWidth;
        height = newHeight;
        bgfx::reset(
            static_cast<std::uint32_t>(newWidth),
            static_cast<std::uint32_t>(newHeight),
            cfg.resetFlags
        );
        bgfx::setViewRect(
            0, 0, 0, static_cast<std::uint16_t>(newWidth), static_cast<std::uint16_t>(newHeight)
        );
    }

  private:
    RenderContext(int w, int h, RenderConfig cfg) noexcept
        : alive(true), width(w), height(h), cfg(cfg) {}

    auto shutdown() noexcept -> void {
        if (alive) {
            bgfx::shutdown();
            alive = false;
        }
    }

    bool alive = false;
    int width = 0;
    int height = 0;
    RenderConfig cfg{};
};

} // namespace roboslop
