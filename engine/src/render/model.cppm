module;

#include <glm/mat4x4.hpp>

#include <vector>

export module roboslop.render.model;

import roboslop.render.material;
import roboslop.render.mesh;

namespace roboslop {

// One drawable piece of an imported model: its buffers, the surface to
// draw it with, and where it sits relative to the owning entity.
// Handles are non-owning, like Mesh and Material; whoever uploaded the
// buffers (SceneRuntime for scene models) frees them.
export struct ModelDrawPart {
    Mesh mesh{};
    Material material{};
    glm::mat4 local{1.0F}; // part space -> entity space
};

// Multi-part renderable. The frontend emits one draw per part with
// model = entity transform * part.local, so a whole model moves as one
// entity: physics and picking see a single Transform.
export struct ModelInstance {
    std::vector<ModelDrawPart> parts;
};

} // namespace roboslop
