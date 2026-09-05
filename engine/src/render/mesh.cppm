module;

#include <bgfx/bgfx.h>

#include <cstddef>
#include <cstdint>
#include <span>

export module roboslop.render.mesh;

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
// apps/gorden/assets/shaders/src/varying.def.sc — the layout vs_basic.sc /
// fs_basic.sc expect.
export [[nodiscard]] auto vertexLayoutPosColor() -> bgfx::VertexLayout {
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, /*normalized=*/true)
        .end();
    return layout;
}

// Position (vec3 float) + Normal (vec3 float) + TexCoord0 (vec2 float).
// Matches MeshVertex in roboslop.assets.mesh and the textured shader
// pair vs_textured.sc / fs_textured.sc.
export [[nodiscard]] auto vertexLayoutPosNormalUv() -> bgfx::VertexLayout {
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
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

} // namespace roboslop
