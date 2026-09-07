import roboslop.ecs;
import roboslop.render.camera;
import roboslop.scene.transform;

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace {

constexpr float Eps = 1e-5F;

[[nodiscard]] auto applyAndDivide(const glm::mat4& m, const glm::vec4& v) -> glm::vec4 {
    const glm::vec4 clip = m * v;
    return clip / clip.w;
}

} // namespace

TEST_CASE("Perspective ZO depth maps near plane to z=0", "[render][camera]") {
    const roboslop::Camera cam{
        .projection = roboslop::Perspective{.fovYRadians = 1.0F, .nearZ = 1.0F, .farZ = 100.0F},
    };
    const glm::mat4 p = roboslop::projectionMatrix(cam, 16.0F / 9.0F, /*homogeneousNdc=*/false);
    const glm::vec4 ndc = applyAndDivide(p, glm::vec4(0.0F, 0.0F, -1.0F, 1.0F));
    REQUIRE(ndc.z == Catch::Approx(0.0F).margin(Eps));
}

TEST_CASE("Perspective NO depth maps near plane to z=-1", "[render][camera]") {
    const roboslop::Camera cam{
        .projection = roboslop::Perspective{.fovYRadians = 1.0F, .nearZ = 1.0F, .farZ = 100.0F},
    };
    const glm::mat4 p = roboslop::projectionMatrix(cam, 16.0F / 9.0F, /*homogeneousNdc=*/true);
    const glm::vec4 ndc = applyAndDivide(p, glm::vec4(0.0F, 0.0F, -1.0F, 1.0F));
    REQUIRE(ndc.z == Catch::Approx(-1.0F).margin(Eps));
}

TEST_CASE("Perspective maps far plane to z=1 (both depth conventions)", "[render][camera]") {
    const roboslop::Camera cam{
        .projection = roboslop::Perspective{.fovYRadians = 1.0F, .nearZ = 1.0F, .farZ = 100.0F},
    };
    const glm::vec4 farPoint(0.0F, 0.0F, -100.0F, 1.0F);
    const glm::mat4 zo = roboslop::projectionMatrix(cam, 1.0F, /*homogeneousNdc=*/false);
    const glm::mat4 no = roboslop::projectionMatrix(cam, 1.0F, /*homogeneousNdc=*/true);
    REQUIRE(applyAndDivide(zo, farPoint).z == Catch::Approx(1.0F).margin(Eps));
    REQUIRE(applyAndDivide(no, farPoint).z == Catch::Approx(1.0F).margin(Eps));
}

TEST_CASE("Perspective aspect ratio scales X", "[render][camera]") {
    // For the same world-space X, doubling aspect halves NDC x.
    const roboslop::Camera cam{
        .projection = roboslop::Perspective{.fovYRadians = 1.0F, .nearZ = 1.0F, .farZ = 100.0F},
    };
    const glm::vec4 p(1.0F, 0.0F, -2.0F, 1.0F);
    const glm::mat4 a1 = roboslop::projectionMatrix(cam, 1.0F, /*homogeneousNdc=*/false);
    const glm::mat4 a2 = roboslop::projectionMatrix(cam, 2.0F, /*homogeneousNdc=*/false);
    const float ndc1 = applyAndDivide(a1, p).x;
    const float ndc2 = applyAndDivide(a2, p).x;
    REQUIRE(ndc2 == Catch::Approx(ndc1 * 0.5F).margin(Eps));
}

TEST_CASE("Orthographic respects halfHeight", "[render][camera]") {
    const roboslop::Camera cam{
        .projection = roboslop::Orthographic{.halfHeight = 5.0F, .nearZ = -10.0F, .farZ = 10.0F},
    };
    const glm::mat4 p = roboslop::projectionMatrix(cam, 1.0F, /*homogeneousNdc=*/false);
    const glm::vec4 ndc = applyAndDivide(p, glm::vec4(0.0F, 5.0F, 0.0F, 1.0F));
    REQUIRE(ndc.y == Catch::Approx(1.0F).margin(Eps));
}

TEST_CASE("Orthographic respects aspect for X extent", "[render][camera]") {
    const roboslop::Camera cam{
        .projection = roboslop::Orthographic{.halfHeight = 1.0F, .nearZ = -10.0F, .farZ = 10.0F},
    };
    const glm::mat4 p = roboslop::projectionMatrix(cam, 2.0F, /*homogeneousNdc=*/false);
    const glm::vec4 ndc = applyAndDivide(p, glm::vec4(2.0F, 0.0F, 0.0F, 1.0F));
    REQUIRE(ndc.x == Catch::Approx(1.0F).margin(Eps));
}

TEST_CASE("viewMatrix is inverse of camera-transform matrix", "[render][camera]") {
    roboslop::Transform t;
    t.position = {1.0F, 2.0F, 3.0F};
    const glm::mat4 product = roboslop::viewMatrix(t) * roboslop::toMatrix(t);
    const glm::mat4 identity(1.0F);
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            REQUIRE(product[col][row] == Catch::Approx(identity[col][row]).margin(Eps));
        }
    }
}

TEST_CASE("findActiveCamera picks the entity tagged ActiveCamera", "[render][camera]") {
    roboslop::World w;
    const auto inactive = w.create();
    w.emplace<roboslop::Transform>(inactive);
    w.emplace<roboslop::Camera>(inactive);

    const auto active = w.create();
    w.emplace<roboslop::Transform>(active);
    w.emplace<roboslop::Camera>(active);
    w.emplace<roboslop::ActiveCamera>(active);

    const auto picked = roboslop::findActiveCamera(w);
    REQUIRE(picked.has_value());
    REQUIRE(*picked == active);
}

TEST_CASE("findActiveCamera returns nullopt when no camera is tagged", "[render][camera]") {
    roboslop::World w;
    const auto e = w.create();
    w.emplace<roboslop::Transform>(e);
    w.emplace<roboslop::Camera>(e);

    REQUIRE_FALSE(roboslop::findActiveCamera(w).has_value());
}

TEST_CASE("findActiveCamera ignores entities missing Transform or Camera", "[render][camera]") {
    roboslop::World w;

    const auto noTransform = w.create();
    w.emplace<roboslop::Camera>(noTransform);
    w.emplace<roboslop::ActiveCamera>(noTransform);

    const auto noCamera = w.create();
    w.emplace<roboslop::Transform>(noCamera);
    w.emplace<roboslop::ActiveCamera>(noCamera);

    REQUIRE_FALSE(roboslop::findActiveCamera(w).has_value());
}
