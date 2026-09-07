import roboslop.scene.transform;

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <numbers>

namespace {

constexpr float Eps = 1e-5F;

auto approxEqual(const glm::mat4& a, const glm::mat4& b) -> bool {
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            if (Catch::Approx(a[col][row]).margin(Eps) != b[col][row]) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

TEST_CASE("Transform default-constructs to identity", "[scene][transform]") {
    const roboslop::Transform t;
    REQUIRE(approxEqual(roboslop::toMatrix(t), glm::mat4(1.0F)));
}

TEST_CASE("Transform translation places origin at position", "[scene][transform]") {
    roboslop::Transform t;
    t.position = {1.0F, 2.0F, 3.0F};
    const glm::vec4 origin = roboslop::toMatrix(t) * glm::vec4(0.0F, 0.0F, 0.0F, 1.0F);
    REQUIRE(origin.x == Catch::Approx(1.0F));
    REQUIRE(origin.y == Catch::Approx(2.0F));
    REQUIRE(origin.z == Catch::Approx(3.0F));
    REQUIRE(origin.w == Catch::Approx(1.0F));
}

TEST_CASE("Transform uniform scale scales unit X axis", "[scene][transform]") {
    roboslop::Transform t;
    t.scale = {2.0F, 2.0F, 2.0F};
    const glm::vec4 x = roboslop::toMatrix(t) * glm::vec4(1.0F, 0.0F, 0.0F, 1.0F);
    REQUIRE(x.x == Catch::Approx(2.0F));
    REQUIRE(x.y == Catch::Approx(0.0F).margin(Eps));
    REQUIRE(x.z == Catch::Approx(0.0F).margin(Eps));
}

TEST_CASE("Transform 90deg Y rotation maps +X to -Z", "[scene][transform]") {
    // Right-handed: rotating +X by +90° around +Y yields -Z.
    roboslop::Transform t;
    t.rotation =
        glm::angleAxis(static_cast<float>(std::numbers::pi) * 0.5F, glm::vec3(0.0F, 1.0F, 0.0F));
    const glm::vec4 v = roboslop::toMatrix(t) * glm::vec4(1.0F, 0.0F, 0.0F, 1.0F);
    REQUIRE(v.x == Catch::Approx(0.0F).margin(Eps));
    REQUIRE(v.y == Catch::Approx(0.0F).margin(Eps));
    REQUIRE(v.z == Catch::Approx(-1.0F).margin(Eps));
}

TEST_CASE("Transform composes T * R * S", "[scene][transform]") {
    roboslop::Transform t;
    t.position = {1.0F, 2.0F, 3.0F};
    t.rotation =
        glm::angleAxis(static_cast<float>(std::numbers::pi) * 0.25F, glm::vec3(0.0F, 1.0F, 0.0F));
    t.scale = {2.0F, 0.5F, 4.0F};

    const glm::mat4 expected = glm::translate(glm::mat4(1.0F), t.position) *
                               glm::mat4_cast(t.rotation) * glm::scale(glm::mat4(1.0F), t.scale);
    REQUIRE(approxEqual(roboslop::toMatrix(t), expected));
}
