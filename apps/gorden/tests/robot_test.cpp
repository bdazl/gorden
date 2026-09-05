import gorden.agent.robot;
import roboslop.scene.transform;

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

TEST_CASE("tickRobotMotion walks toward the target and arrives", "[agent][robot]") {
    roboslop::Transform t{.position = {0.0F, 0.5F, 0.0F}};
    gorden::RobotMotion m{
        .target = glm::vec3{2.0F, 0.0F, 0.0F}, .speed = 1.0F, .arriveRadius = 0.1F
    };

    REQUIRE_FALSE(gorden::tickRobotMotion(t, m, 0.5F));
    REQUIRE(t.position.x == Catch::Approx(0.5F));
    REQUIRE(t.position.y == Catch::Approx(0.5F)); // height untouched
    REQUIRE(m.target.has_value());

    bool arrived = false;
    for (int i = 0; i < 10 && !arrived; ++i) {
        arrived = gorden::tickRobotMotion(t, m, 0.5F);
    }
    REQUIRE(arrived);
    REQUIRE(t.position.x == Catch::Approx(2.0F));
    REQUIRE_FALSE(m.target.has_value());
}

TEST_CASE("tickRobotMotion is a no-op without a target", "[agent][robot]") {
    roboslop::Transform t{.position = {1.0F, 0.0F, 1.0F}};
    gorden::RobotMotion m;
    REQUIRE_FALSE(gorden::tickRobotMotion(t, m, 1.0F));
    REQUIRE(t.position.x == 1.0F);
}

TEST_CASE("tickRobotMotion never overshoots on a large step", "[agent][robot]") {
    roboslop::Transform t{};
    gorden::RobotMotion m{.target = glm::vec3{0.0F, 0.0F, -1.0F}, .speed = 100.0F};
    const bool arrived = gorden::tickRobotMotion(t, m, 1.0F);
    // A single huge step lands exactly on the target (within the
    // arrive radius) rather than flying past it.
    REQUIRE((
        arrived || glm::length(t.position - glm::vec3{0.0F, 0.0F, -1.0F}) <= m.arriveRadius + 1e-4F
    ));
}
