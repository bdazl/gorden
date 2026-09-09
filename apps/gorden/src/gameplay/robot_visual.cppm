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

// The actor Transform is the wheel contact on the ground. The model shares
// that origin; only its authored +Z facing needs adapting to gameplay -Z.
export [[nodiscard]] auto robotVisualTransform() -> roboslop::Transform {
    return roboslop::Transform{
        .rotation = glm::angleAxis(std::numbers::pi_v<float>, glm::vec3{0.0F, 1.0F, 0.0F})
    };
}

export [[nodiscard]] auto
loadRobotModel(roboslop::SceneRuntime& runtime, roboslop::AssetCache& assets)
    -> roboslop::Result<roboslop::ModelInstance> {
    auto model = runtime.instantiateModel(assets, "models/gorden.glb");
    if (!model) {
        return std::unexpected(model.error());
    }
    const auto local = roboslop::toMatrix(robotVisualTransform());
    for (auto& part : model->parts) {
        part.local = local * part.local;
    }
    return model;
}

} // namespace gorden
