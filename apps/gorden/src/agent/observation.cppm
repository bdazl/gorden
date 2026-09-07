module;

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

export module gorden.agent.observation;

import roboslop.ecs;
import roboslop.scene.transform;

namespace gorden {

// Human-readable identity the robot perceives entities by. Names are
// what the model sees and what tools refer to; entity ids never reach
// the model.
export struct Named {
    std::string name{};
};

export struct ObservedEntity {
    std::string name{};
    glm::vec3 position{0.0F};
    float distance = 0.0F;
};

// What the robot gets to know for one think. Deliberately small: the
// three first tools need positions and names, nothing else (the
// "semantic-first, minimal" direction in docs/architecture.md).
export struct Observation {
    glm::vec3 robotPosition{0.0F};
    glm::vec3 playerPosition{0.0F};
    bool robotMoving = false;
    std::vector<ObservedEntity> nearby{}; // sorted by distance, robot excluded
    std::vector<std::string> recentEvents{};
    std::string playerMessage{};
};

// Engine-side truth → robot perception. Every Named entity with a
// Transform within `radius` of the robot is visible; there is no
// line-of-sight yet.
export [[nodiscard]] auto buildObservation(
    const roboslop::World& world, roboslop::Entity robot, roboslop::Entity player, float radius
) -> Observation {
    Observation obs;
    obs.robotPosition = world.get<roboslop::Transform>(robot).position;
    obs.playerPosition = world.get<roboslop::Transform>(player).position;

    world.forEach<Named, roboslop::Transform>(
        [&](roboslop::Entity e, const Named& named, const roboslop::Transform& t) {
            if (e == robot) {
                return;
            }
            const float d = glm::distance(t.position, obs.robotPosition);
            if (d <= radius) {
                obs.nearby.push_back(
                    ObservedEntity{.name = named.name, .position = t.position, .distance = d}
                );
            }
        }
    );
    std::ranges::sort(obs.nearby, [](const ObservedEntity& a, const ObservedEntity& b) {
        return a.distance < b.distance;
    });
    return obs;
}

[[nodiscard]] static auto vec3Json(const glm::vec3& v) -> nlohmann::json {
    // Two decimals: enough for a 10 m yard, fewer tokens for the model.
    auto r = [](float f) { return static_cast<double>(static_cast<int>(f * 100.0F)) / 100.0; };
    return {{"x", r(v.x)}, {"y", r(v.y)}, {"z", r(v.z)}};
}

// The text the model actually reads. Kept as one JSON object so the
// prompt format is stable and easy to log.
export [[nodiscard]] auto observationToJson(const Observation& obs) -> std::string {
    nlohmann::json j;
    j["robot"] = {{"position", vec3Json(obs.robotPosition)}, {"moving", obs.robotMoving}};
    j["player"] = {{"position", vec3Json(obs.playerPosition)}};
    nlohmann::json nearby = nlohmann::json::array();
    for (const auto& e : obs.nearby) {
        nearby.push_back({
            {"name", e.name},
            {"position", vec3Json(e.position)},
            {"distance", static_cast<double>(static_cast<int>(e.distance * 10.0F)) / 10.0},
        });
    }
    j["nearby"] = std::move(nearby);
    j["events"] = obs.recentEvents;
    if (!obs.playerMessage.empty()) {
        j["player_message"] = obs.playerMessage;
    }
    return j.dump();
}

} // namespace gorden
