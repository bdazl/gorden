module;

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Body/BodyID.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <variant>

export module roboslop.physics.components;

namespace roboslop {

// Shape descriptors are POD; the physics-spawn system reads them once at
// body-create time, copies the geometry into a Jolt Shape, and removes
// the BodyDesc component. Adding new primitives is one variant arm here
// plus one branch in physicsSpawn.
export struct SphereShape {
    float radius = 0.5F;
};

// `center` offsets the box in shape space (before the entity transform)
// so a collider can wrap geometry that is not centred on the origin,
// like an imported model's bounds.
export struct BoxShape {
    glm::vec3 halfExtents{0.5F, 0.5F, 0.5F};
    glm::vec3 center{0.0F, 0.0F, 0.0F};
};

export using BodyShape = std::variant<SphereShape, BoxShape>;

// MVP supports Static (ground, immutable colliders) and Dynamic (everything
// driven by gravity / impulses). Kinematic (engine-moved colliders that
// push dynamics) can join later when it has a caller.
export enum class BodyMotion : std::uint8_t {
    Static,
    Dynamic,
};

// Tag component that physicsSpawn consumes to realise a Jolt body. Once
// the body exists in Jolt, the entity carries a RigidBody and BodyDesc
// is removed.
export struct BodyDesc {
    BodyShape shape;
    BodyMotion motion = BodyMotion::Dynamic;
    float mass = 1.0F;
    float friction = 0.5F;
    float restitution = 0.0F;
};

// Handle to an entity's Jolt body. Lifetime is tied to JoltWorld via the
// BodyInterface; the engine must release the JPH::BodyID before the
// JoltWorld is destroyed (handled in physicsDespawn, M2-onward).
export struct RigidBody {
    JPH::BodyID id;
};

// Last-fixed-step transform, captured by syncPhysicsToTransform so the
// render frontend can lerp PrevTransform → Transform by alpha for
// sub-frame visual smoothing. Initialised at body-spawn to the same TRS
// as the entity's Transform.
export struct PrevTransform {
    glm::vec3 position{0.0F};
    glm::quat rotation{1.0F, 0.0F, 0.0F, 0.0F};
    glm::vec3 scale{1.0F};
};

} // namespace roboslop
