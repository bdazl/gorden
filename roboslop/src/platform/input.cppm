module;

#include <GLFW/glfw3.h>
#include <glm/vec2.hpp>

#include <array>
#include <cstddef>

export module roboslop.platform.input;

import roboslop.platform.window;

namespace roboslop {

// Enumerates only the keys consumers actually wire today. Adding a new key
// is one entry here plus one case in translateKey() and bumping kKeyCount.
// The integer values are dense from zero so they index directly into the
// snapshot arrays — don't assign explicit numbers.
export enum class Key : int {
    W,
    A,
    S,
    D,
    Q,
    E,
    Space,
    LeftShift,
    LeftCtrl,
    Escape,
    F1,
    F2,
    F3,
    F4,
};

export enum class MouseButton : int {
    Left,
    Right,
    Middle,
};

namespace detail {
inline constexpr std::size_t kKeyCount = 14;
inline constexpr std::size_t kMouseButtonCount = 3;
} // namespace detail

// Pure value-type snapshot of one frame's input. No GLFW calls happen
// here — beginFrame() in Input populates two of these per frame. Tests
// build snapshots by hand and feed them to the free helpers below.
//
// cursorPosValid is false on the first frame after construction (no
// prior pose to diff against) and after a cursor-mode transition (the
// OS-driven cursor jump must not register as motion).
export struct InputSnapshot {
    std::array<bool, detail::kKeyCount> keys{};
    std::array<bool, detail::kMouseButtonCount> mouseButtons{};
    glm::dvec2 cursorPos{0.0, 0.0};
    bool cursorPosValid = false;
};

// Mouse-position delta between two snapshots. Returns zero when either
// side lacks a valid pose — first frame of a session, or first frame
// after a cursor-mode switch.
export [[nodiscard]] auto
computeMouseDelta(const InputSnapshot& prev, const InputSnapshot& curr) noexcept -> glm::vec2 {
    if (!prev.cursorPosValid || !curr.cursorPosValid) {
        return {0.0F, 0.0F};
    }
    return {
        static_cast<float>(curr.cursorPos.x - prev.cursorPos.x),
        static_cast<float>(curr.cursorPos.y - prev.cursorPos.y),
    };
}

// Edge predicates. Pure, snapshot-only — Input's same-named methods
// delegate to these so the edge logic can be unit-tested without GLFW.
export [[nodiscard]] auto
keyPressedEdge(const InputSnapshot& prev, const InputSnapshot& curr, Key k) noexcept -> bool {
    const auto i = static_cast<std::size_t>(k);
    return curr.keys[i] && !prev.keys[i];
}

export [[nodiscard]] auto
keyReleasedEdge(const InputSnapshot& prev, const InputSnapshot& curr, Key k) noexcept -> bool {
    const auto i = static_cast<std::size_t>(k);
    return prev.keys[i] && !curr.keys[i];
}

export [[nodiscard]] auto
mouseButtonPressedEdge(const InputSnapshot& prev, const InputSnapshot& curr, MouseButton b) noexcept
    -> bool {
    const auto i = static_cast<std::size_t>(b);
    return curr.mouseButtons[i] && !prev.mouseButtons[i];
}

namespace {

[[nodiscard]] auto translateKey(Key k) noexcept -> int {
    switch (k) {
    case Key::W:
        return GLFW_KEY_W;
    case Key::A:
        return GLFW_KEY_A;
    case Key::S:
        return GLFW_KEY_S;
    case Key::D:
        return GLFW_KEY_D;
    case Key::Q:
        return GLFW_KEY_Q;
    case Key::E:
        return GLFW_KEY_E;
    case Key::Space:
        return GLFW_KEY_SPACE;
    case Key::LeftShift:
        return GLFW_KEY_LEFT_SHIFT;
    case Key::LeftCtrl:
        return GLFW_KEY_LEFT_CONTROL;
    case Key::Escape:
        return GLFW_KEY_ESCAPE;
    case Key::F1:
        return GLFW_KEY_F1;
    case Key::F2:
        return GLFW_KEY_F2;
    case Key::F3:
        return GLFW_KEY_F3;
    case Key::F4:
        return GLFW_KEY_F4;
    }
    return GLFW_KEY_UNKNOWN;
}

[[nodiscard]] auto translateMouseButton(MouseButton b) noexcept -> int {
    switch (b) {
    case MouseButton::Left:
        return GLFW_MOUSE_BUTTON_LEFT;
    case MouseButton::Right:
        return GLFW_MOUSE_BUTTON_RIGHT;
    case MouseButton::Middle:
        return GLFW_MOUSE_BUTTON_MIDDLE;
    }
    return GLFW_MOUSE_BUTTON_LEFT;
}

} // namespace

// Engine-facing per-frame input poller. Holds the raw GLFWwindow handle
// — stable across Window moves because only the wrapper's pointer is
// exchanged, never the underlying GLFW object — so Input survives any
// App move without re-seating.
//
// beginFrame() runs once per render frame after pollWindowEvents().
// Edge queries (keyPressed / keyReleased / mouseButtonPressed) compare
// the cached pair of snapshots; multiple fixed sub-steps within the
// same render frame see the same edges, which is the correct semantics
// for "this key just transitioned this frame".
//
// Mouse delta is captured once per render frame for the same reason —
// it is an angular quantity (handled as pixels-per-frame), not a
// velocity, so repeating it across sub-steps would mis-integrate motion.
export class Input {
  public:
    explicit Input(const Window& window) noexcept : handle_(window.handle()) {}

    Input(const Input&) = delete;
    auto operator=(const Input&) -> Input& = delete;
    Input(Input&&) noexcept = default;
    auto operator=(Input&&) noexcept -> Input& = default;
    ~Input() = default;

    auto beginFrame() -> void {
        prev_ = curr_;

        for (std::size_t i = 0; i < detail::kKeyCount; ++i) {
            const auto k = static_cast<Key>(i);
            curr_.keys[i] = glfwGetKey(handle_, translateKey(k)) == GLFW_PRESS;
        }
        for (std::size_t i = 0; i < detail::kMouseButtonCount; ++i) {
            const auto b = static_cast<MouseButton>(i);
            curr_.mouseButtons[i] =
                glfwGetMouseButton(handle_, translateMouseButton(b)) == GLFW_PRESS;
        }

        double cx = 0.0;
        double cy = 0.0;
        glfwGetCursorPos(handle_, &cx, &cy);
        curr_.cursorPos = {cx, cy};
        curr_.cursorPosValid = true;

        if (resetDeltaNextFrame_) {
            cachedDelta_ = {0.0F, 0.0F};
            resetDeltaNextFrame_ = false;
        } else {
            cachedDelta_ = computeMouseDelta(prev_, curr_);
        }
    }

    [[nodiscard]] auto keyDown(Key k) const noexcept -> bool {
        return curr_.keys[static_cast<std::size_t>(k)];
    }

    [[nodiscard]] auto keyPressed(Key k) const noexcept -> bool {
        return keyPressedEdge(prev_, curr_, k);
    }

    [[nodiscard]] auto keyReleased(Key k) const noexcept -> bool {
        return keyReleasedEdge(prev_, curr_, k);
    }

    [[nodiscard]] auto mouseButton(MouseButton b) const noexcept -> bool {
        return curr_.mouseButtons[static_cast<std::size_t>(b)];
    }

    [[nodiscard]] auto mouseButtonPressed(MouseButton b) const noexcept -> bool {
        return mouseButtonPressedEdge(prev_, curr_, b);
    }

    [[nodiscard]] auto mouseDelta() const noexcept -> glm::vec2 {
        return cachedDelta_;
    }

    // Drives the cursor through GLFW directly rather than via Window, so
    // Input doesn't need to hold a Window pointer (which would be a
    // dangling pointer after an App move). resetDeltaNextFrame_ stops
    // the OS-driven cursor jump from showing up as motion.
    auto setCursorCaptured(bool captured) -> void {
        glfwSetInputMode(
            handle_, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL
        );
        if (glfwRawMouseMotionSupported() == GLFW_TRUE) {
            glfwSetInputMode(handle_, GLFW_RAW_MOUSE_MOTION, captured ? GLFW_TRUE : GLFW_FALSE);
        }
        resetDeltaNextFrame_ = true;
    }

  private:
    GLFWwindow* handle_ = nullptr;
    InputSnapshot prev_;
    InputSnapshot curr_;
    glm::vec2 cachedDelta_{0.0F, 0.0F};
    bool resetDeltaNextFrame_ = false;
};

} // namespace roboslop
