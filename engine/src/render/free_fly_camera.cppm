module;

#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <algorithm>

export module roboslop.render.free_fly_camera;

import roboslop.ecs;
import roboslop.platform.input;
import roboslop.scene.transform;

namespace roboslop {

// ECS component for a debug fly-camera. Yaw and pitch are accumulated in
// the controller (Euler) rather than read back from Transform.rotation;
// reconstructing them from a quaternion is lossy near the poles and would
// fight the pitch clamp.
//
// Convention (right-handed, +Y up, -Z forward):
//   rotation = angleAxis(yaw, +Y) * angleAxis(pitch, +X)
//   forward  = rotation * (0, 0, -1)
//   right    = rotation * (1, 0, 0)
//   world-up = (0, 1, 0)   — used for Space/Ctrl ascent regardless of pitch
//
// Mouse delta uses GLFW screen conventions (x grows right, y grows down).
// Yaw decreases for rightward mouse motion (camera turns right);
// pitch decreases for downward mouse motion (camera looks down).
export struct FreeFlyCamera {
    float yawRadians = 0.0F;
    float pitchRadians = 0.0F;
    float moveSpeed = 5.0F;          // m/s in any direction
    float boostMultiplier = 4.0F;    // applied while boost is held
    float lookSensitivity = 0.0025F; // radians per pixel of mouse delta
};

// Stateless input bundle for the pure tick. The system below packs an
// Input into one of these, then calls tickFreeFlyCamera — that gives
// tests a way to drive controller math without GLFW.
export struct FreeFlyTickInput {
    glm::vec2 mouseDelta{0.0F, 0.0F};
    bool forward = false;
    bool back = false;
    bool left = false;
    bool right = false;
    bool up = false;
    bool down = false;
    bool boost = false;
    bool active = false; // RMB held — false means "ignore this tick"
};

// Pitch clamp: just inside ±90° so the forward vector never aligns with
// world-up (which would gimbal-lock yaw input).
inline constexpr float kFreeFlyPitchLimit = 1.55334F; // ~89° in radians

// Applies one tick of input to (controller, transform). Pure: no World,
// no Input, no globals. Returns void; mutates ctrl.yaw/pitch and
// xf.position/rotation. Does nothing when !in.active so the camera
// doesn't drift while the cursor is free.
export auto tickFreeFlyCamera(
    FreeFlyCamera& ctrl, Transform& xf, const FreeFlyTickInput& in, double dt
) noexcept -> void {
    if (!in.active) {
        return;
    }

    ctrl.yawRadians -= in.mouseDelta.x * ctrl.lookSensitivity;
    ctrl.pitchRadians -= in.mouseDelta.y * ctrl.lookSensitivity;
    ctrl.pitchRadians = std::clamp(ctrl.pitchRadians, -kFreeFlyPitchLimit, kFreeFlyPitchLimit);

    const glm::quat yaw = glm::angleAxis(ctrl.yawRadians, glm::vec3(0.0F, 1.0F, 0.0F));
    const glm::quat pitch = glm::angleAxis(ctrl.pitchRadians, glm::vec3(1.0F, 0.0F, 0.0F));
    xf.rotation = yaw * pitch;

    const glm::vec3 forward = xf.rotation * glm::vec3(0.0F, 0.0F, -1.0F);
    const glm::vec3 right = xf.rotation * glm::vec3(1.0F, 0.0F, 0.0F);
    constexpr glm::vec3 worldUp{0.0F, 1.0F, 0.0F};

    glm::vec3 move{0.0F};
    if (in.forward) {
        move += forward;
    }
    if (in.back) {
        move -= forward;
    }
    if (in.right) {
        move += right;
    }
    if (in.left) {
        move -= right;
    }
    if (in.up) {
        move += worldUp;
    }
    if (in.down) {
        move -= worldUp;
    }

    if (glm::dot(move, move) > 0.0F) {
        const float speed = ctrl.moveSpeed * (in.boost ? ctrl.boostMultiplier : 1.0F);
        xf.position += glm::normalize(move) * speed * static_cast<float>(dt);
    }
}

// Cursor-capture bookkeeping for the system below, parked in the
// world context. `pressBlocked` remembers that the current right-button
// hold started somewhere capture was not allowed (over a dev-UI
// window), so moving off the window mid-hold does not start a fly.
export struct FreeFlyCursorState {
    bool pressBlocked = false;
};

// ECS system. Reads the current Input snapshot, packs a FreeFlyTickInput,
// and applies it to every (FreeFlyCamera, Transform) entity.
//
// The right mouse button drives cursor capture as a *level*, not an
// edge: while it is held the cursor is captured and the controller
// active, when it is up the cursor is free. Levels survive frames in
// which no fixed step runs (a 60 Hz tick under a 60 Hz vsync regularly
// produces 0 or 2 steps per frame), which is what edge-based capture
// got wrong — a missed release edge left the cursor hidden and locked.
//
// `allowCapture` is the app's veto for the frame the hold starts:
// pass false while a dev-UI window wants the mouse, so a right-click
// on a panel is a UI click rather than the start of a fly.
export auto updateFreeFlyCameras(World& world, Input& input, double dt, bool allowCapture = true)
    -> void {
    auto& state = world.registry().ctx().emplace<FreeFlyCursorState>();
    const bool held = input.mouseButton(MouseButton::Right);
    if (!held) {
        input.setCursorCaptured(false);
        state.pressBlocked = false;
    } else if (!input.cursorCaptured()) {
        if (state.pressBlocked || !allowCapture) {
            state.pressBlocked = true;
        } else {
            input.setCursorCaptured(true);
        }
    }
    const bool active = held && input.cursorCaptured();

    const FreeFlyTickInput tick{
        .mouseDelta = input.mouseDelta(),
        .forward = input.keyDown(Key::W),
        .back = input.keyDown(Key::S),
        .left = input.keyDown(Key::A),
        .right = input.keyDown(Key::D),
        .up = input.keyDown(Key::Space),
        .down = input.keyDown(Key::LeftCtrl),
        .boost = input.keyDown(Key::LeftShift),
        .active = active,
    };

    world.forEach<FreeFlyCamera, Transform>([&](auto& ctrl, auto& xf) {
        tickFreeFlyCamera(ctrl, xf, tick, dt);
    });
}

} // namespace roboslop
