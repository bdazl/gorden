module;

#include <bgfx/bgfx.h>
#include <spdlog/spdlog.h>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat4x4.hpp>
#include <glm/matrix.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <optional>
#include <variant>

export module roboslop.render.camera;

import roboslop.ecs;
import roboslop.scene.transform;

// All matrices in roboslop are glm column-major, right-handed, +Y up, -Z
// forward. glm::value_ptr feeds bgfx::setViewTransform / setTransform
// without transpose. Depth convention is read from bgfx::getCaps() per
// frame in applyActiveCamera; the pure helpers below take it as an arg so
// they stay testable without a bgfx device.

namespace roboslop {

export struct Perspective {
    float fovYRadians = glm::radians(60.0F);
    float nearZ = 0.1F;
    float farZ = 1000.0F;
};

export struct Orthographic {
    float halfHeight = 5.0F;
    float nearZ = -100.0F;
    float farZ = 100.0F;
};

export struct Camera {
    std::variant<Perspective, Orthographic> projection;
};

// Empty tag: marks the camera entity the renderer reads each frame.
export struct ActiveCamera {};

// homogeneousNdc=true → depth in [-1, 1] (OpenGL/GLES); false → [0, 1]
// (Vulkan/D3D/Metal). Mirrors bgfx::getCaps()->homogeneousDepth.
export [[nodiscard]] auto
projectionMatrix(const Camera& cam, float aspect, bool homogeneousNdc) noexcept -> glm::mat4 {
    return std::visit(
        [&](auto&& proj) -> glm::mat4 {
            using P = std::decay_t<decltype(proj)>;
            if constexpr (std::is_same_v<P, Perspective>) {
                return homogeneousNdc ? glm::perspectiveRH_NO(
                                            proj.fovYRadians, aspect, proj.nearZ, proj.farZ
                                        )
                                      : glm::perspectiveRH_ZO(
                                            proj.fovYRadians, aspect, proj.nearZ, proj.farZ
                                        );
            } else {
                const float halfW = proj.halfHeight * aspect;
                return homogeneousNdc ? glm::orthoRH_NO(
                                            -halfW, halfW, -proj.halfHeight, proj.halfHeight,
                                            proj.nearZ, proj.farZ
                                        )
                                      : glm::orthoRH_ZO(
                                            -halfW, halfW, -proj.halfHeight, proj.halfHeight,
                                            proj.nearZ, proj.farZ
                                        );
            }
        },
        cam.projection
    );
}

// View = inverse(cameraWorldTransform). Scale on the camera is undefined
// behaviour at this stage; no lookAt helper yet.
export [[nodiscard]] auto viewMatrix(const Transform& cameraTransform) noexcept -> glm::mat4 {
    return glm::inverse(toMatrix(cameraTransform));
}

// First entity with Camera + Transform + ActiveCamera. Logs a warning at
// most once per call when more than one match is found, and returns the
// first. Returns nullopt when no entity carries the full combo.
export [[nodiscard]] auto findActiveCamera(const World& world) -> std::optional<Entity> {
    auto view = world.registry().view<const Camera, const Transform, const ActiveCamera>();
    std::optional<Entity> first;
    std::size_t count = 0;
    for (const auto e : view) {
        if (!first.has_value()) {
            first = e;
        }
        ++count;
    }
    if (count > 1) {
        spdlog::warn("roboslop.render.camera: {} entities tagged ActiveCamera; picking first", count);
    }
    return first;
}

// Reads ActiveCamera + Camera + Transform and pushes view*proj to the
// given bgfx view. Call once per view per frame, before any draws into
// that view. No-op (with a warning) when no active camera exists.
export auto
applyActiveCamera(const World& world, std::uint16_t viewId, int viewportW, int viewportH) -> void {
    const auto cam = findActiveCamera(world);
    if (!cam.has_value()) {
        spdlog::warn("roboslop.render.camera: no ActiveCamera entity; skipping setViewTransform");
        return;
    }
    const auto& reg = world.registry();
    const auto& transform = reg.get<const Transform>(*cam);
    const auto& camera = reg.get<const Camera>(*cam);

    const float aspect =
        viewportH > 0 ? static_cast<float>(viewportW) / static_cast<float>(viewportH) : 1.0F;
    const bool homogeneous = bgfx::getCaps()->homogeneousDepth;

    const glm::mat4 view = viewMatrix(transform);
    const glm::mat4 proj = projectionMatrix(camera, aspect, homogeneous);
    bgfx::setViewTransform(viewId, glm::value_ptr(view), glm::value_ptr(proj));
}

} // namespace roboslop
