import roboslop.render.free_fly_camera;
import roboslop.scene.transform;

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <numbers>

namespace {

constexpr float Eps = 1e-4F;

} // namespace

TEST_CASE("tickFreeFlyCamera is a no-op when inactive", "[render][free_fly]") {
    roboslop::FreeFlyCamera ctrl;
    roboslop::Transform xf;
    xf.position = {7.0F, 8.0F, 9.0F};

    roboslop::FreeFlyTickInput in;
    in.forward = true;
    in.mouseDelta = {500.0F, 500.0F};
    in.active = false; // RMB not held

    roboslop::tickFreeFlyCamera(ctrl, xf, in, /*dt=*/1.0);

    REQUIRE(xf.position.x == Catch::Approx(7.0F));
    REQUIRE(xf.position.y == Catch::Approx(8.0F));
    REQUIRE(xf.position.z == Catch::Approx(9.0F));
    REQUIRE(ctrl.yawRadians == Catch::Approx(0.0F));
    REQUIRE(ctrl.pitchRadians == Catch::Approx(0.0F));
}

TEST_CASE("forward translates -Z by moveSpeed * dt", "[render][free_fly]") {
    roboslop::FreeFlyCamera ctrl;
    ctrl.moveSpeed = 5.0F;
    roboslop::Transform xf;

    roboslop::FreeFlyTickInput in;
    in.forward = true;
    in.active = true;

    roboslop::tickFreeFlyCamera(ctrl, xf, in, /*dt=*/1.0);

    REQUIRE(xf.position.x == Catch::Approx(0.0F).margin(Eps));
    REQUIRE(xf.position.y == Catch::Approx(0.0F).margin(Eps));
    REQUIRE(xf.position.z == Catch::Approx(-5.0F).margin(Eps));
}

TEST_CASE("boost multiplies move speed", "[render][free_fly]") {
    roboslop::FreeFlyCamera ctrl;
    ctrl.moveSpeed = 5.0F;
    ctrl.boostMultiplier = 4.0F;
    roboslop::Transform xf;

    roboslop::FreeFlyTickInput in;
    in.forward = true;
    in.boost = true;
    in.active = true;

    roboslop::tickFreeFlyCamera(ctrl, xf, in, /*dt=*/1.0);

    REQUIRE(xf.position.z == Catch::Approx(-20.0F).margin(Eps));
}

TEST_CASE("Space and LeftCtrl move along world up regardless of pitch", "[render][free_fly]") {
    roboslop::FreeFlyCamera ctrl;
    ctrl.pitchRadians = static_cast<float>(std::numbers::pi) * 0.25F; // pre-tilted up
    roboslop::Transform xf;

    roboslop::FreeFlyTickInput in;
    in.up = true;
    in.active = true;

    roboslop::tickFreeFlyCamera(ctrl, xf, in, /*dt=*/1.0);

    REQUIRE(xf.position.x == Catch::Approx(0.0F).margin(Eps));
    REQUIRE(xf.position.y == Catch::Approx(ctrl.moveSpeed).margin(Eps));
    REQUIRE(xf.position.z == Catch::Approx(0.0F).margin(Eps));
}

TEST_CASE("mouseDelta.x rightward yields negative yaw", "[render][free_fly]") {
    roboslop::FreeFlyCamera ctrl;
    ctrl.lookSensitivity = 0.01F;
    roboslop::Transform xf;

    roboslop::FreeFlyTickInput in;
    in.mouseDelta = {100.0F, 0.0F};
    in.active = true;

    roboslop::tickFreeFlyCamera(ctrl, xf, in, /*dt=*/0.016);

    REQUIRE(ctrl.yawRadians == Catch::Approx(-1.0F).margin(Eps));

    // After a right-look, forward should have rotated toward +X (the
    // camera now faces partly to the world-right).
    const glm::vec3 forward = xf.rotation * glm::vec3(0.0F, 0.0F, -1.0F);
    REQUIRE(forward.x > 0.0F);
}

TEST_CASE("pitch clamps to ~±89 degrees", "[render][free_fly]") {
    roboslop::FreeFlyCamera ctrl;
    ctrl.lookSensitivity = 0.01F;
    roboslop::Transform xf;

    roboslop::FreeFlyTickInput in;
    in.mouseDelta = {0.0F, -1.0e6F}; // huge upward look
    in.active = true;

    roboslop::tickFreeFlyCamera(ctrl, xf, in, /*dt=*/0.016);

    REQUIRE(ctrl.pitchRadians <= static_cast<float>(std::numbers::pi) * 0.5F);
    REQUIRE(ctrl.pitchRadians > 0.0F);

    // A second huge tick the other direction should clamp negative.
    in.mouseDelta = {0.0F, 1.0e6F};
    roboslop::tickFreeFlyCamera(ctrl, xf, in, /*dt=*/0.016);
    REQUIRE(ctrl.pitchRadians >= -static_cast<float>(std::numbers::pi) * 0.5F);
    REQUIRE(ctrl.pitchRadians < 0.0F);
}

TEST_CASE("diagonal motion is normalized (no speed boost)", "[render][free_fly]") {
    roboslop::FreeFlyCamera ctrl;
    ctrl.moveSpeed = 1.0F;
    roboslop::Transform xf;

    roboslop::FreeFlyTickInput in;
    in.forward = true;
    in.right = true;
    in.active = true;

    roboslop::tickFreeFlyCamera(ctrl, xf, in, /*dt=*/1.0);

    const float length = glm::length(xf.position);
    REQUIRE(length == Catch::Approx(1.0F).margin(Eps));
}
