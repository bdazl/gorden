module;

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <optional>

export module gorden.agent.robot;

import roboslop.ecs;
import roboslop.scene.transform;
import roboslop.sched;

namespace gorden {

export struct Robot {};

// Kinematic locomotion state. The simulation owns movement: the agent
// only ever sets `target` (through a validated moveTo), and the fixed
// system below walks there in a straight line on the XZ plane. No
// physics body yet — the robot passes through crates. `arrived` is a
// one-shot flag the brain consumes to raise a MoveCompleted event.
export struct RobotMotion {
    std::optional<glm::vec3> target{};
    float speed = 2.0F;         // m/s
    float arriveRadius = 0.15F; // m
    bool arrived = false;
};

// Pure step. Returns true on the tick the robot reaches its target;
// clears the target then. Faces the direction of travel (yaw only).
export [[nodiscard]] auto tickRobotMotion(roboslop::Transform& t, RobotMotion& m, float dt) noexcept
    -> bool {
    if (!m.target) {
        return false;
    }
    glm::vec3 to = *m.target - t.position;
    to.y = 0.0F;
    const float dist = glm::length(to);
    if (dist <= m.arriveRadius) {
        t.position.x = m.target->x;
        t.position.z = m.target->z;
        m.target.reset();
        return true;
    }
    const glm::vec3 dir = to / dist;
    const float step = std::min(m.speed * dt, dist);
    t.position += dir * step;
    // Right-handed, -Z forward: yaw so the robot's -Z faces `dir`.
    const float yaw = std::atan2(-dir.x, -dir.z);
    t.rotation = glm::angleAxis(yaw, glm::vec3{0.0F, 1.0F, 0.0F});
    return false;
}

// Fixed-update system: advances every Robot. Registered by the app as
//   { .name = "robotLocomotion", .writes = {"transforms"} }.
export auto robotLocomotion(roboslop::SystemCtx& c) -> void {
    c.world->forEach<Robot, RobotMotion, roboslop::Transform>([&](RobotMotion& m,
                                                                  roboslop::Transform& t) {
        if (tickRobotMotion(t, m, static_cast<float>(c.dt))) {
            m.arrived = true;
        }
    });
}

} // namespace gorden
