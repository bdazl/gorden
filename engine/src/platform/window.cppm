module;

#include <GLFW/glfw3.h>

#include <atomic>
#include <cstdint>
#include <expected>
#include <functional>
#include <string>
#include <utility>

// The Conan glfw recipe ships an X11-only build, so we only enable X11
// native handles here. The on-Wayland path runs through XWayland, which is
// also bgfx's safest Linux target until we vendor a Wayland-capable glfw
// and validate bgfx's Wayland renderer end-to-end.
#if defined(__linux__)
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#endif

export module roboslop.platform.window;

import roboslop.core.error;

namespace roboslop {

namespace {

std::atomic<int> g_glfwRefcount{0};

auto retainGlfw() -> bool {
    if (g_glfwRefcount.fetch_add(1) == 0) {
        if (glfwInit() == GLFW_FALSE) {
            g_glfwRefcount.fetch_sub(1);
            return false;
        }
    }
    return true;
}

auto releaseGlfw() noexcept -> void {
    if (g_glfwRefcount.fetch_sub(1) == 1) {
        glfwTerminate();
    }
}

} // namespace

export enum class WindowError : int {
    GlfwInitFailed = 1,
    WindowCreateFailed = 2,
};

export [[nodiscard]] auto toError(WindowError e) -> Error {
    switch (e) {
    case WindowError::GlfwInitFailed:
        return {
            .category = "roboslop.platform.window",
            .code = static_cast<int>(e),
            .message = "glfwInit failed",
            .context = {}
        };
    case WindowError::WindowCreateFailed:
        return {
            .category = "roboslop.platform.window",
            .code = static_cast<int>(e),
            .message = "glfwCreateWindow returned nullptr",
            .context = {}
        };
    }
    return {
        .category = "roboslop.platform.window",
        .code = 0,
        .message = "unknown WindowError",
        .context = {}
    };
}

export enum class NativePlatform : int {
    Unknown = 0,
    X11,
    Wayland,
    Win32,
    Cocoa,
};

// Cursor presentation mode. Maps directly to GLFW_CURSOR_NORMAL/HIDDEN/DISABLED.
// Captured locks the cursor to the window centre and feeds unbounded
// virtual motion to the position queries — the model expected by FPS-style
// look-around. Hidden keeps free movement but draws no cursor (rarely used,
// kept for parity with the GLFW surface).
export enum class CursorMode : int {
    Normal = 0,
    Hidden,
    Captured,
};

// Backend-agnostic view of the native window handles a renderer needs to
// attach. display is null on platforms (Win32, Cocoa) that don't expose one.
export struct NativeHandles {
    NativePlatform platform = NativePlatform::Unknown;
    void* display = nullptr;
    void* window = nullptr;
};

export struct WindowConfig {
    std::string title = "roboslop";
    int width = 1280;
    int height = 720;
};

// RAII wrapper around a single GLFWwindow. The first Window in a process
// drives glfwInit(); the last one to die calls glfwTerminate(). Construction
// is only via Window::make() — the engine has no exceptions, so a factory is
// the only way to report init failure.
export class Window {
  public:
    [[nodiscard]] static auto make(const WindowConfig& cfg) -> Result<Window> {
        if (!retainGlfw()) {
            return std::unexpected(toError(WindowError::GlfwInitFailed));
        }
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        auto* handle = glfwCreateWindow(cfg.width, cfg.height, cfg.title.c_str(), nullptr, nullptr);
        if (handle == nullptr) {
            releaseGlfw();
            return std::unexpected(toError(WindowError::WindowCreateFailed));
        }
        return Window{handle};
    }

    Window(const Window&) = delete;
    auto operator=(const Window&) -> Window& = delete;

    Window(Window&& other) noexcept
        : handle(std::exchange(other.handle, nullptr)),
          resizeCallback(std::move(other.resizeCallback)), cursorMode(other.cursorMode) {
        rebindUserPointer();
    }

    auto operator=(Window&& other) noexcept -> Window& {
        if (this != &other) {
            shutdown();
            handle = std::exchange(other.handle, nullptr);
            resizeCallback = std::move(other.resizeCallback);
            cursorMode = other.cursorMode;
            rebindUserPointer();
        }
        return *this;
    }

    ~Window() {
        shutdown();
    }

    [[nodiscard]] auto shouldClose() const -> bool {
        return glfwWindowShouldClose(handle) == GLFW_TRUE;
    }

    auto requestClose() const -> void {
        glfwSetWindowShouldClose(handle, GLFW_TRUE);
    }

    [[nodiscard]] auto framebufferSize() const -> std::pair<int, int> {
        int w = 0;
        int h = 0;
        glfwGetFramebufferSize(handle, &w, &h);
        return {w, h};
    }

    // Raw GLFW handle. Stable for the lifetime of the Window (and across
    // moves — only the wrapper's pointer is exchanged, not the underlying
    // GLFW object). Used by input.cppm to poll keys/mouse without holding
    // a reference to the Window itself.
    [[nodiscard]] auto glfwHandle() const noexcept -> GLFWwindow* {
        return handle;
    }

    auto setCursorMode(CursorMode mode) -> void {
        cursorMode = mode;
        int glfwMode = GLFW_CURSOR_NORMAL;
        switch (mode) {
        case CursorMode::Normal:
            glfwMode = GLFW_CURSOR_NORMAL;
            break;
        case CursorMode::Hidden:
            glfwMode = GLFW_CURSOR_HIDDEN;
            break;
        case CursorMode::Captured:
            glfwMode = GLFW_CURSOR_DISABLED;
            break;
        }
        glfwSetInputMode(handle, GLFW_CURSOR, glfwMode);
        if (glfwRawMouseMotionSupported() == GLFW_TRUE) {
            glfwSetInputMode(
                handle, GLFW_RAW_MOUSE_MOTION, mode == CursorMode::Captured ? GLFW_TRUE : GLFW_FALSE
            );
        }
    }

    // Registers a callback fired when GLFW reports a new framebuffer size.
    // Installs the trampoline on first call; subsequent calls just replace
    // the stored target. A null callback uninstalls.
    auto setResizeCallback(std::function<void(int, int)> cb) -> void {
        resizeCallback = std::move(cb);
        rebindUserPointer();
        if (resizeCallback) {
            glfwSetFramebufferSizeCallback(handle, &Window::framebufferSizeTrampoline);
        } else {
            glfwSetFramebufferSizeCallback(handle, nullptr);
        }
    }

    [[nodiscard]] auto nativeHandles() const -> NativeHandles {
        NativeHandles out;
#if defined(__linux__)
        out.platform = NativePlatform::X11;
        out.display = glfwGetX11Display();
        out.window = reinterpret_cast<void*>(static_cast<std::uintptr_t>(glfwGetX11Window(handle)));
#endif
        return out;
    }

  private:
    explicit Window(GLFWwindow* handle) noexcept : handle(handle) {}

    auto shutdown() noexcept -> void {
        if (handle != nullptr) {
            glfwSetFramebufferSizeCallback(handle, nullptr);
            glfwSetWindowUserPointer(handle, nullptr);
            glfwDestroyWindow(handle);
            handle = nullptr;
            releaseGlfw();
        }
    }

    // Re-publish the current `this` pointer through GLFW's user-pointer slot
    // after construction or move; the trampoline below resolves callbacks
    // through that slot rather than capturing `this` directly.
    auto rebindUserPointer() noexcept -> void {
        if (handle != nullptr) {
            glfwSetWindowUserPointer(handle, this);
        }
    }

    static auto framebufferSizeTrampoline(GLFWwindow* w, int width, int height) -> void {
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(w));
        if (self != nullptr && self->resizeCallback) {
            self->resizeCallback(width, height);
        }
    }

    GLFWwindow* handle = nullptr;
    std::function<void(int, int)> resizeCallback;
    CursorMode cursorMode = CursorMode::Normal;
};

// Drives the process-wide GLFW event queue. Single-window engines call this
// once per frame; the per-instance API stays empty until we need it.
export auto pollWindowEvents() noexcept -> void {
    glfwPollEvents();
}

} // namespace roboslop
