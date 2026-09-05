// Minimal Dear ImGui renderer over bgfx. A plain header + TU rather than
// a module: it is third-party glue around imgui.h and bgfx.h, and the
// dev-UI module (roboslop.ui) includes it from its global module
// fragment. This is the "imgui_impl_bgfx" the dependency docs refer to.
#ifndef ROBOSLOP_UI_IMGUI_BGFX_RENDERER_H
#define ROBOSLOP_UI_IMGUI_BGFX_RENDERER_H

#include <bgfx/bgfx.h>
#include <imgui.h>

#include <cstdint>

namespace roboslop::detail {

struct ImguiBgfxRenderer {
    bgfx::VertexLayout layout;
    bgfx::ProgramHandle program{bgfx::kInvalidHandle};
    bgfx::UniformHandle sampler{bgfx::kInvalidHandle};
    bgfx::TextureHandle fontTexture{bgfx::kInvalidHandle};
};

// Builds the vertex layout, sampler uniform, and font-atlas texture for
// the current ImGui context. `program` is borrowed (owned by the
// AssetCache); everything else is owned by the returned struct.
[[nodiscard]] auto imguiBgfxInit(bgfx::ProgramHandle program) -> ImguiBgfxRenderer;

auto imguiBgfxShutdown(ImguiBgfxRenderer& r) noexcept -> void;

// Submits `drawData` into `viewId`. Must run on the bgfx API thread
// between the frame's begin and bgfx::frame().
auto imguiBgfxRender(const ImguiBgfxRenderer& r, const ImDrawData* drawData, std::uint16_t viewId)
    -> void;

} // namespace roboslop::detail

#endif // ROBOSLOP_UI_IMGUI_BGFX_RENDERER_H
