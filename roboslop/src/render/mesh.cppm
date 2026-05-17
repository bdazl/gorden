module;

#include <bgfx/bgfx.h>

// entt's sparse-set iterator's operator!= is a non-member that range-for
// can't see through the module boundary for single-component views. The
// include keeps the range-for in submitMeshes valid (see
// docs/decisions.md, 2026-05-17 ECS-facade entry).
#include <entt/entt.hpp>

#include <glm/gtc/type_ptr.hpp>
#include <glm/mat4x4.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

export module roboslop.render.mesh;

import roboslop.ecs;
import roboslop.scene.transform;

namespace roboslop {

// Renderable component. Holds non-owning bgfx handles: the buffers and
// program are freed at bgfx::shutdown(). A future iteration will wrap
// mesh data in an RAII owner when entities start coming and going.
export struct Mesh {
    bgfx::VertexBufferHandle vb{bgfx::kInvalidHandle};
    bgfx::IndexBufferHandle ib{bgfx::kInvalidHandle};
    bgfx::ProgramHandle program{bgfx::kInvalidHandle};
    std::uint64_t state = BGFX_STATE_DEFAULT;
    std::uint16_t viewId = 0;
};

// Position (vec3 float) + Color0 (uint8 x4, normalised). Matches
// gorden/assets/shaders/src/varying.def.sc — the layout vs_basic.sc /
// fs_basic.sc expect.
export [[nodiscard]] auto vertexLayoutPosColor() -> bgfx::VertexLayout {
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, /*normalized=*/true)
        .end();
    return layout;
}

// Copies vertex / index data into bgfx static buffers and returns a Mesh
// component pointing at them. Caller fills in the program handle (e.g.
// from roboslop::loadProgram) and any non-default state/viewId before
// emplacing on an entity.
export [[nodiscard]] auto makeStaticMesh(
    std::span<const std::byte> vertices,
    std::span<const std::uint16_t> indices,
    const bgfx::VertexLayout& layout
) -> Mesh {
    Mesh m;
    m.vb = bgfx::createVertexBuffer(
        bgfx::copy(vertices.data(), static_cast<std::uint32_t>(vertices.size())), layout
    );
    m.ib = bgfx::createIndexBuffer(
        bgfx::copy(
            indices.data(), static_cast<std::uint32_t>(indices.size() * sizeof(std::uint16_t))
        )
    );
    return m;
}

// Render system: submit every Mesh entity to its configured view. Must be
// called from inside App's render slot, between beginFrame and endFrame.
// Reaches into World::registry() because raw view iteration sidesteps
// World::forEach's callback indirection.
//
// When the entity carries a Transform, the resulting model matrix is fed
// to bgfx::setTransform per draw. Without a Transform bgfx uses identity,
// which keeps debug/triangle entities working without ceremony.
export auto submitMeshes(const World& world) -> void {
    const auto& reg = world.registry();
    for (const auto e : reg.view<const Mesh>()) {
        const auto& m = reg.get<const Mesh>(e);
        if (!bgfx::isValid(m.vb) || !bgfx::isValid(m.ib) || !bgfx::isValid(m.program)) {
            continue;
        }
        if (const auto* t = reg.try_get<const Transform>(e)) {
            const glm::mat4 model = toMatrix(*t);
            bgfx::setTransform(glm::value_ptr(model));
        }
        bgfx::setVertexBuffer(0, m.vb);
        bgfx::setIndexBuffer(m.ib);
        bgfx::setState(m.state);
        bgfx::submit(m.viewId, m.program);
    }
}

} // namespace roboslop
