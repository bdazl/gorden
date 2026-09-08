module;

#include <Jolt/Jolt.h>

#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <entt/entt.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>

export module gorden.player;

import roboslop.ecs;
import roboslop.physics;
import roboslop.platform.input;
import roboslop.scene.transform;

namespace gorden {

export struct PlayerInput {
    glm::vec2 move{0.0F};      // right, forward
    glm::vec2 mouseLook{0.0F}; // pixels, consumed once
    glm::vec2 stickLook{0.0F}; // angular velocity, applied each tick
};

export struct OrbitCamera {
    float yaw = 0.0F;
    float pitch = -0.3F;
    float distance = 4.0F;
};

// UI state is copied after drawing, then only read by fixed systems.
export struct PlayerControls {
    bool uiMouse = false;
    bool uiKeyboard = false;
    bool lookBlocked = false;
};

export [[nodiscard]] auto readPlayerInput(roboslop::Input& input, PlayerControls& controls)
    -> PlayerInput {
    PlayerInput out;
    const auto delta = input.takeLookDelta();
    const auto& pad = input.gamepad();
    const bool held = input.mouseButton(roboslop::MouseButton::Right);
    const bool cancelled = input.keyDown(roboslop::Key::Escape) || (pad.connected && pad.buttonB);
    if (!held) {
        controls.lookBlocked = false;
    }
    if (!input.focused() || controls.uiKeyboard || controls.uiMouse || cancelled) {
        controls.lookBlocked = held;
    }
    const bool capture = held && !controls.lookBlocked && input.focused();
    const bool wasCaptured = input.cursorCaptured();
    input.setCursorCaptured(capture);
    if (capture && wasCaptured) {
        out.mouseLook = delta;
    }
    if (!input.focused() || controls.uiKeyboard || cancelled) {
        return out;
    }
    using roboslop::Key;
    out.move = {
        static_cast<float>(input.keyDown(Key::D)) - static_cast<float>(input.keyDown(Key::A)),
        static_cast<float>(input.keyDown(Key::W)) - static_cast<float>(input.keyDown(Key::S)),
    };
    if (pad.connected) {
        const auto stick = roboslop::stickWithDeadzone(pad.leftStick);
        out.move += glm::vec2{stick.x, -stick.y};
        out.stickLook = roboslop::stickWithDeadzone(pad.rightStick);
    }
    const float length = glm::length(out.move);
    if (length > 1.0F) {
        out.move /= length;
    }
    return out;
}

export auto turnCamera(OrbitCamera& camera, const PlayerInput& input, float dt) -> void {
    constexpr float MouseSensitivity = 0.0025F;
    constexpr float StickSpeed = 2.5F;
    camera.yaw = std::remainder(
        camera.yaw - (input.mouseLook.x * MouseSensitivity) - (input.stickLook.x * StickSpeed * dt),
        2.0F * std::numbers::pi_v<float>
    );
    camera.pitch = std::clamp(
        camera.pitch - (input.mouseLook.y * MouseSensitivity) -
            (input.stickLook.y * StickSpeed * dt),
        -1.2F,
        0.65F
    );
}

export [[nodiscard]] auto playerVelocity(const OrbitCamera& camera, glm::vec2 move) -> glm::vec3 {
    const auto yaw = glm::angleAxis(camera.yaw, glm::vec3{0.0F, 1.0F, 0.0F});
    return yaw * glm::vec3{move.x, 0.0F, -move.y} * 4.0F;
}

// The capsule is centred on the player's Transform: radius .35 m,
// total height 1.8 m. Visual scale does not rescale the controller.
// This is app-owned until another application needs character movement.
export struct Player {
    std::unique_ptr<JPH::CharacterVirtual> character;
    std::unique_ptr<JPH::TempAllocatorImpl> scratch;
    glm::vec3 velocity{0.0F};
    bool grounded = false;

    // Drop cached contacts after loading/replacing the physical scene.
    auto reset() -> void {
        character.reset();
        velocity = glm::vec3{0.0F};
        grounded = false;
    }
};

export auto movePlayer(
    Player& player,
    roboslop::Transform& transform,
    roboslop::JoltWorld& physics,
    glm::vec3 desired,
    float dt
) -> void {
    auto& system = physics.physicsSystem();
    if (!player.character) {
        JPH::CharacterVirtualSettings settings;
        settings.mShape = new JPH::CapsuleShape(0.55F, 0.35F);
        settings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), 0.55F);
        settings.mMaxSlopeAngle = std::numbers::pi_v<float> / 4.0F;
        player.character = std::make_unique<JPH::CharacterVirtual>(
            &settings,
            JPH::RVec3{transform.position.x, transform.position.y, transform.position.z},
            JPH::Quat::sIdentity(),
            0,
            &system
        );
        if (!player.scratch) {
            player.scratch = std::make_unique<JPH::TempAllocatorImpl>(1024U * 1024U);
        }
    }
    auto& character = *player.character;
    character.UpdateGroundVelocity();
    JPH::Vec3 velocity = JPH::Vec3::sAxisY() * character.GetLinearVelocity().GetY();
    if (character.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround &&
        velocity.GetY() - character.GetGroundVelocity().GetY() < 0.1F) {
        velocity = character.GetGroundVelocity();
    }
    velocity += JPH::Vec3{desired.x, 0.0F, desired.z} + system.GetGravity() * dt;
    character.SetLinearVelocity(velocity);
    JPH::CharacterVirtual::ExtendedUpdateSettings update;
    update.mWalkStairsStepUp = JPH::Vec3{0.0F, 0.3F, 0.0F};
    character.ExtendedUpdate(
        dt,
        system.GetGravity(),
        update,
        system.GetDefaultBroadPhaseLayerFilter(roboslop::ObjectLayerMoving),
        system.GetDefaultLayerFilter(roboslop::ObjectLayerMoving),
        {},
        {},
        *player.scratch
    );
    const auto position = character.GetPosition();
    const glm::vec3 next{position.GetX(), position.GetY(), position.GetZ()};
    player.velocity = (next - transform.position) / dt;
    transform.position = next;
    player.grounded = character.IsSupported();
    if (glm::dot(desired, desired) > 0.0001F) {
        transform.rotation =
            glm::angleAxis(std::atan2(-desired.x, -desired.z), glm::vec3{0.0F, 1.0F, 0.0F});
    }
}

// A swept sphere keeps the camera and its near plane inside the room.
// The player is virtual (has no rigid body), so it cannot occlude itself.
export auto followPlayer(
    const OrbitCamera& orbit,
    const roboslop::Transform& player,
    roboslop::Transform& camera,
    roboslop::JoltWorld& physics
) -> void {
    camera.rotation = glm::angleAxis(orbit.yaw, glm::vec3{0.0F, 1.0F, 0.0F}) *
                      glm::angleAxis(orbit.pitch, glm::vec3{1.0F, 0.0F, 0.0F});
    const auto target = player.position + glm::vec3{0.0F, 0.55F, 0.0F};
    const auto offset = camera.rotation * glm::vec3{0.0F, 0.0F, orbit.distance};
    const JPH::SphereShape sphere{0.2F};
    const JPH::RShapeCast cast{
        &sphere,
        JPH::Vec3::sReplicate(1.0F),
        JPH::RMat44::sTranslation(JPH::RVec3{target.x, target.y, target.z}),
        JPH::Vec3{offset.x, offset.y, offset.z}
    };
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> hits;
    JPH::ShapeCastSettings settings;
    settings.mReturnDeepestPoint = true;
    physics.physicsSystem().GetNarrowPhaseQuery().CastShape(
        cast, settings, JPH::RVec3::sZero(), hits
    );
    const float fraction = hits.HadHit() ? std::max(0.0F, hits.mHit.mFraction - 0.01F) : 1.0F;
    camera.position = target + offset * fraction;
}

} // namespace gorden
