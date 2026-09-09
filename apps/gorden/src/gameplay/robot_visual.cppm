module;

#include <glm/gtc/quaternion.hpp>

#include <expected>
#include <numbers>

export module gorden.robot_visual;

import roboslop.core.error;
import roboslop.render.asset_cache;
import roboslop.render.model;
import roboslop.scene.runtime;
import roboslop.scene.transform;

namespace gorden {

export [[nodiscard]] auto
loadRobotModel(roboslop::SceneRuntime& runtime, roboslop::AssetCache& assets)
    -> roboslop::Result<roboslop::ModelInstance> {
    auto model = runtime.instantiateModel(assets, "models/gorden.glb");
    if (!model) {
        return std::unexpected(model.error());
    }
    // Keep the former unit cube's bottom at local y=-0.5 so existing
    // spawn positions and saves stay grounded. The authored wheel contact
    // is y=0 and +Z forward; locomotion faces -Z.
    const auto local = roboslop::toMatrix(
        roboslop::Transform{
            .position = {0.0F, -0.5F, 0.0F},
            .rotation = glm::angleAxis(std::numbers::pi_v<float>, glm::vec3{0.0F, 1.0F, 0.0F})
        }
    );
    for (auto& part : model->parts) {
        part.local = local * part.local;
    }
    return model;
}

} // namespace gorden
