module;

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/EActivation.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>
#include <entt/entt.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

export module roboslop.physics;

import roboslop.ecs;
import roboslop.physics.components;
import roboslop.sched;
import roboslop.scene.transform;

namespace roboslop {

// ObjectLayer constants: 16-bit narrow categorisation used for object-
// vs-object collision filtering. Two layers cover MVP scope; widening
// (e.g. trigger / ragdoll / projectile) is one entry plus a row in the
// pair filter.
export inline constexpr JPH::ObjectLayer ObjectLayerNonMoving = 0;
export inline constexpr JPH::ObjectLayer ObjectLayerMoving = 1;
export inline constexpr JPH::ObjectLayer ObjectLayerCount = 2;

// BroadPhaseLayer constants: coarser categorisation used by the
// broadphase spatial partition. Static bodies live in their own BP
// layer so moving bodies never test against statics they can't collide
// with.
namespace bp {
inline constexpr JPH::BroadPhaseLayer NonMoving{0};
inline constexpr JPH::BroadPhaseLayer Moving{1};
inline constexpr JPH::uint NumLayers = 2;
} // namespace bp

namespace detail {

// Jolt's three filter interfaces are pure-virtual; we satisfy them
// with single-instance concrete classes hidden in namespace detail.
// Inheriting from a third-party interface is the unavoidable case
// where inheritance is the right tool — engine-facing surface stays
// free-function-shaped above this layer.
class BroadPhaseLayerImpl final : public JPH::BroadPhaseLayerInterface {
  public:
    BroadPhaseLayerImpl() {
        objectToBroadPhase[ObjectLayerNonMoving] = bp::NonMoving;
        objectToBroadPhase[ObjectLayerMoving] = bp::Moving;
    }

    auto GetNumBroadPhaseLayers() const -> JPH::uint override {
        return bp::NumLayers;
    }

    auto GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const -> JPH::BroadPhaseLayer override {
        return objectToBroadPhase[inLayer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    auto GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const -> const char* override {
        if (inLayer == bp::NonMoving) {
            return "NonMoving";
        }
        if (inLayer == bp::Moving) {
            return "Moving";
        }
        return "?";
    }
#endif

  private:
    JPH::BroadPhaseLayer objectToBroadPhase[ObjectLayerCount]{};
};

class ObjectVsBroadPhaseFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter {
  public:
    auto ShouldCollide(JPH::ObjectLayer a, JPH::BroadPhaseLayer b) const -> bool override {
        switch (a) {
        case ObjectLayerNonMoving:
            return b == bp::Moving;
        case ObjectLayerMoving:
            return true;
        default:
            return false;
        }
    }
};

class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter {
  public:
    auto ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const -> bool override {
        switch (a) {
        case ObjectLayerNonMoving:
            return b == ObjectLayerMoving;
        case ObjectLayerMoving:
            return true;
        default:
            return false;
        }
    }
};

} // namespace detail

// Owns the per-process Jolt globals (Factory, type registration) plus
// the PhysicsSystem and its support objects. Single-instance per
// process — matches RenderContext's policy. Move-only; the moved-from
// instance becomes a no-op shell so App::make()'s in-place return path
// still works.
export class JoltWorld {
  public:
    [[nodiscard]] static auto make() -> JoltWorld {
        JPH::RegisterDefaultAllocator();
        if (JPH::Factory::sInstance == nullptr) {
            JPH::Factory::sInstance = new JPH::Factory();
            JPH::RegisterTypes();
        }
        return JoltWorld{};
    }

    JoltWorld(const JoltWorld&) = delete;
    auto operator=(const JoltWorld&) -> JoltWorld& = delete;

    JoltWorld(JoltWorld&& o) noexcept
        : alive(std::exchange(o.alive, false)), tempAlloc(std::move(o.tempAlloc)),
          jobSystem(std::move(o.jobSystem)), bpLayer(std::move(o.bpLayer)),
          objVsBp(std::move(o.objVsBp)), layerPair(std::move(o.layerPair)),
          system(std::move(o.system)) {}

    auto operator=(JoltWorld&& o) noexcept -> JoltWorld& {
        if (this != &o) {
            shutdown();
            alive = std::exchange(o.alive, false);
            tempAlloc = std::move(o.tempAlloc);
            jobSystem = std::move(o.jobSystem);
            bpLayer = std::move(o.bpLayer);
            objVsBp = std::move(o.objVsBp);
            layerPair = std::move(o.layerPair);
            system = std::move(o.system);
        }
        return *this;
    }

    ~JoltWorld() {
        shutdown();
    }

    // Advance the simulation by dt. Collision sub-steps = 1 at MVP;
    // raise when penetration artefacts surface.
    auto step(float dt) -> void {
        system->Update(dt, /*inCollisionSteps=*/1, tempAlloc.get(), jobSystem.get());
    }

    [[nodiscard]] auto bodyInterface() noexcept -> JPH::BodyInterface& {
        return system->GetBodyInterface();
    }

    [[nodiscard]] auto physicsSystem() noexcept -> JPH::PhysicsSystem& {
        return *system;
    }

  private:
    JoltWorld() : alive(true) {
        // 4 MiB scratch space; Jolt's HelloWorld uses 10 MiB. Retune
        // when broadphase complains.
        tempAlloc = std::make_unique<JPH::TempAllocatorImpl>(4U * 1024U * 1024U);
        jobSystem = std::make_unique<JPH::JobSystemSingleThreaded>(JPH::cMaxPhysicsJobs);
        bpLayer = std::make_unique<detail::BroadPhaseLayerImpl>();
        objVsBp = std::make_unique<detail::ObjectVsBroadPhaseFilterImpl>();
        layerPair = std::make_unique<detail::ObjectLayerPairFilterImpl>();

        system = std::make_unique<JPH::PhysicsSystem>();
        system->Init(
            /*inMaxBodies=*/1024,
            /*inNumBodyMutexes=*/0, // 0 = auto-detect
            /*inMaxBodyPairs=*/1024,
            /*inMaxContactConstraints=*/1024,
            *bpLayer,
            *objVsBp,
            *layerPair
        );
        // Default gravity is (0, -9.81, 0), matching roboslop's +Y up
        // convention.
    }

    auto shutdown() noexcept -> void {
        if (!alive) {
            return;
        }
        // PhysicsSystem must die before UnregisterTypes — its bodies
        // hold Refs to shapes that depend on Factory type info.
        system.reset();
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
        alive = false;
    }

    bool alive = false;
    std::unique_ptr<JPH::TempAllocatorImpl> tempAlloc;
    std::unique_ptr<JPH::JobSystemSingleThreaded> jobSystem;
    std::unique_ptr<detail::BroadPhaseLayerImpl> bpLayer;
    std::unique_ptr<detail::ObjectVsBroadPhaseFilterImpl> objVsBp;
    std::unique_ptr<detail::ObjectLayerPairFilterImpl> layerPair;
    std::unique_ptr<JPH::PhysicsSystem> system;
};

// Build a Jolt Shape from the engine's BodyShape variant. Returns a
// Jolt-managed Ref; callers feed it straight into BodyCreationSettings.
export [[nodiscard]] auto makeJoltShape(const BodyShape& shape) -> JPH::Ref<JPH::Shape> {
    return std::visit(
        [](const auto& s) -> JPH::Ref<JPH::Shape> {
            using T = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<T, SphereShape>) {
                return new JPH::SphereShape(s.radius);
            } else if constexpr (std::is_same_v<T, BoxShape>) {
                JPH::Ref<JPH::Shape> box = new JPH::BoxShape(
                    JPH::Vec3(s.halfExtents.x, s.halfExtents.y, s.halfExtents.z),
                    0.1F * std::min({s.halfExtents.x, s.halfExtents.y, s.halfExtents.z})
                );
                if (s.center == glm::vec3{0.0F}) {
                    return box;
                }
                return new JPH::RotatedTranslatedShape(
                    JPH::Vec3(s.center.x, s.center.y, s.center.z), JPH::Quat::sIdentity(), box
                );
            } else {
                return nullptr;
            }
        },
        shape
    );
}

// Engine-side installation: register the JoltWorld instance into the
// ECS context so physics systems can reach it from SystemCtx.world
// without scheduler.sched needing a typed dependency on physics.
export auto installJoltWorld(World& world, JoltWorld& jw) -> void {
    world.registry().ctx().emplace<JoltWorld*>(&jw);
}

namespace detail {

[[nodiscard]] inline auto joltWorldFrom(World& world) -> JoltWorld& {
    return *world.registry().ctx().get<JoltWorld*>();
}

} // namespace detail

// Called between fixed steps, before deleting/replacing a scene entity.
export auto releasePhysicsBody(World& world, Entity entity) -> void {
    if (const auto* body = world.tryGet<RigidBody>(entity)) {
        auto& bi = detail::joltWorldFrom(world).bodyInterface();
        bi.RemoveBody(body->id);
        bi.DestroyBody(body->id);
        world.remove<RigidBody>(entity);
        world.remove<PrevTransform>(entity);
    }
    world.remove<BodyDesc>(entity);
}

// physicsSpawn: walk every entity that carries a BodyDesc + Transform
// but no RigidBody yet, create the Jolt body, attach RigidBody +
// PrevTransform, remove BodyDesc. Two-pass so we don't mutate the
// registry while iterating it.
export auto physicsSpawn(SystemCtx& c) -> void {
    auto& reg = c.world->registry();
    auto& bi = detail::joltWorldFrom(*c.world).bodyInterface();

    std::vector<entt::entity> toSpawn;
    for (const auto e : reg.view<const BodyDesc, const Transform>()) {
        if (reg.all_of<RigidBody>(e)) {
            continue;
        }
        toSpawn.push_back(e);
    }

    for (const auto e : toSpawn) {
        const auto& desc = reg.get<const BodyDesc>(e);
        const auto& t = reg.get<const Transform>(e);

        const JPH::Ref<JPH::Shape> shape = makeJoltShape(desc.shape);
        const JPH::EMotionType motion = desc.motion == BodyMotion::Static
                                            ? JPH::EMotionType::Static
                                            : JPH::EMotionType::Dynamic;
        const JPH::ObjectLayer layer =
            desc.motion == BodyMotion::Static ? ObjectLayerNonMoving : ObjectLayerMoving;

        JPH::BodyCreationSettings settings(
            shape,
            JPH::RVec3(t.position.x, t.position.y, t.position.z),
            JPH::Quat(t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w),
            motion,
            layer
        );
        settings.mFriction = desc.friction;
        settings.mRestitution = desc.restitution;

        const JPH::BodyID id = bi.CreateAndAddBody(settings, JPH::EActivation::Activate);
        reg.emplace<RigidBody>(e, RigidBody{.id = id});
        reg.emplace<PrevTransform>(
            e, PrevTransform{.position = t.position, .rotation = t.rotation, .scale = t.scale}
        );
        reg.remove<BodyDesc>(e);
    }
}

// physicsStep: advance the simulation by the fixed sub-step dt.
export auto physicsStep(SystemCtx& c) -> void {
    detail::joltWorldFrom(*c.world).step(static_cast<float>(c.dt));
}

// syncPhysicsToTransform: every dynamic body's pose is read back into
// the ECS Transform; the previous Transform is captured in
// PrevTransform first so the render frontend can lerp by alpha.
export auto syncPhysicsToTransform(SystemCtx& c) -> void {
    auto& reg = c.world->registry();
    const auto& bi = detail::joltWorldFrom(*c.world).bodyInterface();

    auto view = reg.view<const RigidBody, Transform, PrevTransform>();
    for (const auto e : view) {
        const auto& rb = reg.get<const RigidBody>(e);
        auto& t = reg.get<Transform>(e);
        auto& pt = reg.get<PrevTransform>(e);

        pt.position = t.position;
        pt.rotation = t.rotation;
        pt.scale = t.scale;

        const JPH::RVec3 pos = bi.GetCenterOfMassPosition(rb.id);
        const JPH::Quat rot = bi.GetRotation(rb.id);

        t.position = glm::vec3(pos.GetX(), pos.GetY(), pos.GetZ());
        t.rotation = glm::quat(rot.GetW(), rot.GetX(), rot.GetY(), rot.GetZ());
    }
}

// Registers the canonical Jolt-physics system pipeline into the
// supplied fixed-update SystemGraph. Game code calls this once from
// onBuildGraphs; the engine owns the JoltWorld and threads the
// pointer through SystemCtx. The three systems serialise via writes
// to "physicsBodies", "physicsState", and "transforms"; they run
// after any game-side input systems that also write "transforms"
// (because input systems are added first).
export auto registerPhysicsSystems(SystemGraph& graph) -> void {
    graph.add({
        .name = "physicsSpawn",
        .reads = {"transforms"},
        .writes = {"physicsBodies"},
        .run = physicsSpawn,
    });
    graph.add({
        .name = "physicsStep",
        .reads = {"physicsBodies"},
        .writes = {"physicsState"},
        .run = physicsStep,
    });
    graph.add({
        .name = "syncPhysicsToTransform",
        .reads = {"physicsState"},
        .writes = {"transforms", "prevTransforms"},
        .run = syncPhysicsToTransform,
    });
}

} // namespace roboslop
