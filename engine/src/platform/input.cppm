module;

#include <GLFW/glfw3.h>
#include <glm/vec2.hpp>

#include <array>
#include <cstddef>

export module roboslop.platform.input;

import roboslop.platform.window;

namespace roboslop {

// Enumerates only the keys consumers actually wire today. Adding a new key
// is one entry here plus one case in translateKey() and bumping KeyCount.
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
inline constexpr std::size_t KeyCount = 14;
inline constexpr std::size_t MouseButtonCount = 3;
} // namespace detail

// Pure value-type snapshot of one frame's input. No GLFW calls happen
// here — beginFrame() in Input populates two of these per frame. Tests
// build snapshots by hand and feed them to the free helpers below.
//
// cursorPosValid is false on the first frame after construction (no
// prior pose to diff against) and after a cursor-mode transition (the
// OS-driven cursor jump must not register as motion).
export struct InputSnapshot {
    std::array<bool, detail::KeyCount> keys{};
    std::array<bool, detail::MouseButtonCount> mouseButtons{};
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

export [[nodiscard]] auto mouseButtonReleasedEdge(
    const InputSnapshot& prev, const InputSnapshot& curr, MouseButton b
) noexcept -> bool {
    const auto i = static_cast<std::size_t>(b);
    return prev.mouseButtons[i] && !curr.mouseButtons[i];
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
    Input() noexcept = default;

    Input(const Input&) = delete;
    auto operator=(const Input&) -> Input& = delete;
    Input(Input&&) noexcept = default;
    auto operator=(Input&&) noexcept -> Input& = default;
    ~Input() = default;

    // Takes the frame's snapshot from the caller rather than polling
    // GLFW itself: the window may only be touched from the thread that
    // owns it, while Input is read by systems on scheduler workers.
    auto beginFrame(const InputSnapshot& snapshot) -> void {
        prev = curr;
        curr = snapshot;

        if (resetDeltaNextFrame) {
            cachedDelta = {0.0F, 0.0F};
            resetDeltaNextFrame = false;
        } else {
            cachedDelta = computeMouseDelta(prev, curr);
        }
    }

    [[nodiscard]] auto keyDown(Key k) const noexcept -> bool {
        return curr.keys[static_cast<std::size_t>(k)];
    }

    [[nodiscard]] auto keyPressed(Key k) const noexcept -> bool {
        return keyPressedEdge(prev, curr, k);
    }

    [[nodiscard]] auto keyReleased(Key k) const noexcept -> bool {
        return keyReleasedEdge(prev, curr, k);
    }

    [[nodiscard]] auto mouseButton(MouseButton b) const noexcept -> bool {
        return curr.mouseButtons[static_cast<std::size_t>(b)];
    }

    [[nodiscard]] auto mouseButtonPressed(MouseButton b) const noexcept -> bool {
        return mouseButtonPressedEdge(prev, curr, b);
    }

    [[nodiscard]] auto mouseButtonReleased(MouseButton b) const noexcept -> bool {
        return mouseButtonReleasedEdge(prev, curr, b);
    }

    [[nodiscard]] auto mouseDelta() const noexcept -> glm::vec2 {
        return cachedDelta;
    }

    // Records a request; the window is not touched here. Free-fly camera
    // control runs as a fixed system on a scheduler worker, so this is
    // called off the window's thread — applyCursorRequest() below does
    // the GLFW work where it is legal. Idempotent: asking for the
    // current state changes nothing, so callers can express capture as a
    // level rather than an edge.
    auto setCursorCaptured(bool captured) noexcept -> void {
        cursorCapturedFlag = captured;
    }

    // Reports the requested state immediately, before it has been
    // applied, so a system that asks for capture and then reads the flag
    // in the same tick sees its own request.
    [[nodiscard]] auto cursorCaptured() const noexcept -> bool {
        return cursorCapturedFlag;
    }

    // Applies a pending cursor-mode change. Must run on the thread that
    // owns the window; the App loop calls it once per frame after the
    // fixed systems. resetDeltaNextFrame stops the OS-driven cursor jump
    // from registering as motion on the following frame.
    auto applyCursorRequest(const Window& window) -> void {
        if (cursorCapturedFlag == cursorCapturedApplied) {
            return;
        }
        cursorCapturedApplied = cursorCapturedFlag;
        GLFWwindow* handle = window.glfwHandle();
        glfwSetInputMode(
            handle, GLFW_CURSOR, cursorCapturedApplied ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL
        );
        if (glfwRawMouseMotionSupported() == GLFW_TRUE) {
            glfwSetInputMode(
                handle, GLFW_RAW_MOUSE_MOTION, cursorCapturedApplied ? GLFW_TRUE : GLFW_FALSE
            );
        }
        resetDeltaNextFrame = true;
    }

  private:
    InputSnapshot prev;
    InputSnapshot curr;
    glm::vec2 cachedDelta{0.0F, 0.0F};
    bool resetDeltaNextFrame = false;
    bool cursorCapturedFlag = false;
    bool cursorCapturedApplied = false;
};

// Reads the window's current keyboard, mouse-button and cursor state.
// GLFW requires this on the thread that owns the window, which is why it
// is a free function next to Window rather than a method on Input.
export [[nodiscard]] auto capturePlatformInput(const Window& window) -> InputSnapshot {
    GLFWwindow* handle = window.glfwHandle();
    InputSnapshot out;
    for (std::size_t i = 0; i < detail::KeyCount; ++i) {
        out.keys[i] = glfwGetKey(handle, translateKey(static_cast<Key>(i))) == GLFW_PRESS;
    }
    for (std::size_t i = 0; i < detail::MouseButtonCount; ++i) {
        out.mouseButtons[i] =
            glfwGetMouseButton(handle, translateMouseButton(static_cast<MouseButton>(i))) ==
            GLFW_PRESS;
    }
    double cx = 0.0;
    double cy = 0.0;
    glfwGetCursorPos(handle, &cx, &cy);
    out.cursorPos = {cx, cy};
    out.cursorPosValid = true;
    return out;
}

} // namespace roboslop
