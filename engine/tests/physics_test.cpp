import roboslop.ecs;
import roboslop.physics;
import roboslop.physics.components;
import roboslop.scene.transform;
import roboslop.sched;

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/EActivation.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE(
    "Repeated preview bodies are released before restoring authored transforms", "[physics][scene]"
) {
    auto physics = roboslop::JoltWorld::make();
    roboslop::World world;
    roboslop::installJoltWorld(world, physics);
    for (int iteration = 0; iteration < 10; ++iteration) {
        const auto entity = world.create();
        world.emplace<roboslop::Transform>(entity, roboslop::Transform{.position = {0, 5, 0}});
        world.emplace<roboslop::BodyDesc>(
            entity,
            roboslop::BodyDesc{.shape = roboslop::BoxShape{.halfExtents = {0.005F, 0.005F, 0.005F}}}
        );
        roboslop::SystemCtx context{.world = &world, .dt = 1.0 / 60.0};
        roboslop::physicsSpawn(context);
        REQUIRE(physics.physicsSystem().GetNumBodies() == 1);
        for (int step = 0; step < 10; ++step) {
            roboslop::physicsStep(context);
        }
        roboslop::syncPhysicsToTransform(context);
        REQUIRE(world.get<roboslop::Transform>(entity).position.y < 5);
        roboslop::releasePhysicsBody(world, entity);
        REQUIRE_FALSE(world.has<roboslop::RigidBody>(entity));
        REQUIRE(physics.physicsSystem().GetNumBodies() == 0);
        world.destroy(entity);
    }
}

TEST_CASE("JoltWorld constructs and destructs cleanly", "[physics][world]") {
    {
        auto world = roboslop::JoltWorld::make();
        REQUIRE(world.physicsSystem().GetNumBodies() == 0U);
    }
    // Re-creating after destruction should work — the destructor must
    // unregister types and clear Factory::sInstance so the second
    // make() can re-register cleanly.
    {
        auto world = roboslop::JoltWorld::make();
        REQUIRE(world.physicsSystem().GetNumBodies() == 0U);
    }
}

TEST_CASE("makeJoltShape produces a valid sphere shape", "[physics][shape]") {
    auto world = roboslop::JoltWorld::make();
    const auto shape = roboslop::makeJoltShape(roboslop::SphereShape{.radius = 1.5F});
    REQUIRE(shape != nullptr);
    REQUIRE(shape->GetSubType() == JPH::EShapeSubType::Sphere);
}

TEST_CASE("makeJoltShape produces a valid box shape", "[physics][shape]") {
    auto world = roboslop::JoltWorld::make();
    const auto shape =
        roboslop::makeJoltShape(roboslop::BoxShape{.halfExtents = {1.0F, 2.0F, 3.0F}});
    REQUIRE(shape != nullptr);
    REQUIRE(shape->GetSubType() == JPH::EShapeSubType::Box);
}

TEST_CASE("makeJoltShape offsets a box by its centre", "[physics][shape]") {
    auto world = roboslop::JoltWorld::make();
    const auto shape = roboslop::makeJoltShape(
        roboslop::BoxShape{.halfExtents = {1.0F, 2.0F, 3.0F}, .center = {10.0F, 0.0F, -1.0F}}
    );
    REQUIRE(shape != nullptr);
    REQUIRE(shape->GetSubType() == JPH::EShapeSubType::RotatedTranslated);
    // Jolt keeps local bounds relative to the centre of mass; the
    // offset shows up as the centre of mass itself.
    const auto com = shape->GetCenterOfMass();
    REQUIRE(com.GetX() == Catch::Approx(10.0F));
    REQUIRE(com.GetY() == Catch::Approx(0.0F));
    REQUIRE(com.GetZ() == Catch::Approx(-1.0F));
    const auto bounds = shape->GetLocalBounds();
    REQUIRE(bounds.mMax.GetX() - bounds.mMin.GetX() == Catch::Approx(2.0F));
    REQUIRE(bounds.mMax.GetY() - bounds.mMin.GetY() == Catch::Approx(4.0F));
}

TEST_CASE("free-fall position matches analytic curve within Jolt tolerance", "[physics][step]") {
    // Drop a sphere from y=10 with no obstacles; let it fall for 0.5 s
    // at 60 Hz. Analytic position with g=9.81 m/s² is
    //   y(t) = y0 - 0.5 g t²  →  y(0.5) = 10 - 1.22625 = 8.77375.
    // Jolt's symplectic integrator overshoots slightly; ±5% bound
    // covers it with margin.
    auto world = roboslop::JoltWorld::make();
    auto& bi = world.bodyInterface();
    const auto shape = roboslop::makeJoltShape(roboslop::SphereShape{.radius = 0.5F});
    JPH::BodyCreationSettings settings(
        shape,
        JPH::RVec3(0.0F, 10.0F, 0.0F),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Dynamic,
        roboslop::ObjectLayerMoving
    );
    const JPH::BodyID id = bi.CreateAndAddBody(settings, JPH::EActivation::Activate);

    constexpr float kDt = 1.0F / 60.0F;
    constexpr int kSteps = 30; // 0.5 s
    for (int i = 0; i < kSteps; ++i) {
        world.step(kDt);
    }

    const JPH::RVec3 pos = bi.GetCenterOfMassPosition(id);
    const float t = kDt * static_cast<float>(kSteps);
    const float analytic = 10.0F - 0.5F * 9.81F * t * t;
    REQUIRE(pos.GetY() == Catch::Approx(analytic).margin(analytic * 0.05F));
}

TEST_CASE("dynamic body lands on a static ground", "[physics][step]") {
    // Drop a sphere from y=2 onto a static box at y=0; after 2 seconds
    // it must rest above the box's top face. Validates static body
    // setup, layer filtering, and contact resolution end-to-end.
    auto world = roboslop::JoltWorld::make();
    auto& bi = world.bodyInterface();

    const auto groundShape =
        roboslop::makeJoltShape(roboslop::BoxShape{.halfExtents = {5.0F, 0.5F, 5.0F}});
    JPH::BodyCreationSettings groundSettings(
        groundShape,
        JPH::RVec3(0.0F, 0.0F, 0.0F),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Static,
        roboslop::ObjectLayerNonMoving
    );
    (void)bi.CreateAndAddBody(groundSettings, JPH::EActivation::DontActivate);

    const auto ballShape = roboslop::makeJoltShape(roboslop::SphereShape{.radius = 0.5F});
    JPH::BodyCreationSettings ballSettings(
        ballShape,
        JPH::RVec3(0.0F, 2.0F, 0.0F),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Dynamic,
        roboslop::ObjectLayerMoving
    );
    const JPH::BodyID ball = bi.CreateAndAddBody(ballSettings, JPH::EActivation::Activate);

    constexpr float kDt = 1.0F / 60.0F;
    for (int i = 0; i < 120; ++i) { // 2 s
        world.step(kDt);
    }

    const JPH::RVec3 pos = bi.GetCenterOfMassPosition(ball);
    // Ground top is y=0.5; ball centre rests at y≈1.0 (radius=0.5).
    REQUIRE(pos.GetY() > 0.5F);
    REQUIRE(pos.GetY() < 1.2F);
}

TEST_CASE("physicsSpawn turns BodyDesc into RigidBody + PrevTransform", "[physics][system]") {
    auto joltWorld = roboslop::JoltWorld::make();
    roboslop::World world;
    roboslop::installJoltWorld(world, joltWorld);

    const auto e = world.create();
    world.emplace<roboslop::Transform>(e, roboslop::Transform{.position = {0.0F, 5.0F, 0.0F}});
    world.emplace<roboslop::BodyDesc>(
        e,
        roboslop::BodyDesc{
            .shape = roboslop::SphereShape{.radius = 0.5F},
            .motion = roboslop::BodyMotion::Dynamic,
        }
    );

    roboslop::SystemCtx ctx{.world = &world};
    roboslop::physicsSpawn(ctx);

    REQUIRE(world.has<roboslop::RigidBody>(e));
    REQUIRE(world.has<roboslop::PrevTransform>(e));
    REQUIRE_FALSE(world.has<roboslop::BodyDesc>(e));
}

TEST_CASE("syncPhysicsToTransform copies pose back to ECS", "[physics][system]") {
    auto joltWorld = roboslop::JoltWorld::make();
    roboslop::World world;
    roboslop::installJoltWorld(world, joltWorld);

    const auto e = world.create();
    world.emplace<roboslop::Transform>(e, roboslop::Transform{.position = {0.0F, 10.0F, 0.0F}});
    world.emplace<roboslop::BodyDesc>(
        e,
        roboslop::BodyDesc{
            .shape = roboslop::SphereShape{.radius = 0.5F},
            .motion = roboslop::BodyMotion::Dynamic,
        }
    );

    roboslop::SystemCtx ctx{.world = &world, .dt = 1.0 / 60.0};
    roboslop::physicsSpawn(ctx);
    for (int i = 0; i < 30; ++i) {
        roboslop::physicsStep(ctx);
        roboslop::syncPhysicsToTransform(ctx);
    }

    const auto& t = world.get<roboslop::Transform>(e);
    REQUIRE(t.position.y < 10.0F);
    REQUIRE(world.has<roboslop::PrevTransform>(e));
    // PrevTransform should hold the second-most-recent position, which
    // is strictly higher than the latest one for a free-falling body.
    const auto& pt = world.get<roboslop::PrevTransform>(e);
    REQUIRE(pt.position.y > t.position.y);
}
