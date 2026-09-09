module;

#include <glm/gtc/quaternion.hpp>

#include <expected>
#include <numbers>

export module gorden.player_visual;

import roboslop.core.error;
import roboslop.render.asset_cache;
import roboslop.render.model;
import roboslop.scene.runtime;
import roboslop.scene.transform;

namespace gorden {

export [[nodiscard]] auto
loadPlayerModel(roboslop::SceneRuntime& runtime, roboslop::AssetCache& assets)
    -> roboslop::Result<roboslop::ModelInstance> {
    auto model = runtime.instantiateModel(assets, "models/player.glb");
    if (!model) {
        return std::unexpected(model.error());
    }
    // Authored feet are at y=0 and the face points +Z. Gameplay uses a
    // centred 1.8 m capsule and -Z forward. Correct only the local visual;
    // movement, the camera and saves continue to use the capsule centre.
    const auto local = roboslop::toMatrix(
        roboslop::Transform{
            .position = {0.0F, -0.9F, 0.0F},
            .rotation = glm::angleAxis(std::numbers::pi_v<float>, glm::vec3{0.0F, 1.0F, 0.0F})
        }
    );
    for (auto& part : model->parts) {
        part.local = local * part.local;
    }
    return model;
}

} // namespace gorden
