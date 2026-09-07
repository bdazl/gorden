module;

#include <bgfx/bgfx.h>

// entt's sparse-set iterator's operator!= is non-member and invisible
// across the module boundary for single-component range-for views;
// the include keeps the range-for in uploadDirectionalLight valid
// (see docs/decisions.md, 2026-05-17 ECS-facade entry).
#include <entt/entt.hpp>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <array>

export module roboslop.render.lighting;

import roboslop.ecs;

namespace roboslop {

// Directional light: an infinitely distant source. `direction` points
// FROM the light TO surfaces (the convention the shader expects). The
// vector does not need to be normalised — packDirectionalLightUniform
// normalises it.
export struct DirectionalLight {
    glm::vec3 direction{0.0F, -1.0F, 0.0F};
    glm::vec3 color{1.0F, 1.0F, 1.0F};
    float intensity = 1.0F;
};

// Bundle of bgfx uniform handles for the directional-light pass. Game
// code stashes one of these on the world's ctx storage at setup so the
// pass record callback can find it each frame without capturing it in
// the lambda.
export struct LightUniforms {
    bgfx::UniformHandle dir{bgfx::kInvalidHandle};
    bgfx::UniformHandle color{bgfx::kInvalidHandle};
};

// Pack a DirectionalLight into the two vec4s the shader binds:
//   u_lightDir   = vec4(normalize(direction), intensity)
//   u_lightColor = vec4(color, 0)
// Returned as a pair so the engine can `setUniform` each on the cached
// uniform handles. Pure function — unit-testable without bgfx.
export [[nodiscard]] auto packDirectionalLightUniform(const DirectionalLight& light) noexcept
    -> std::array<glm::vec4, 2> {
    const glm::vec3 dir = glm::normalize(light.direction);
    return {
        glm::vec4{dir, light.intensity},
        glm::vec4{light.color, 0.0F},
    };
}

// Finds the first DirectionalLight in the world, packs its uniforms,
// and pushes them to bgfx via the supplied handles. No-op when no
// light entity exists; the shader's fallback is a flat black surface.
export auto uploadDirectionalLight(
    const World& world, bgfx::UniformHandle uLightDir, bgfx::UniformHandle uLightColor
) -> void {
    const auto& reg = world.registry();
    for (const auto e : reg.view<const DirectionalLight>()) {
        const auto& light = reg.get<const DirectionalLight>(e);
        const auto packed = packDirectionalLightUniform(light);
        bgfx::setUniform(uLightDir, packed.data());
        bgfx::setUniform(uLightColor, &packed[1]);
        return; // first one wins
    }
}

} // namespace roboslop
