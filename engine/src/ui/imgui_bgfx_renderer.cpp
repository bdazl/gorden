#include "imgui_bgfx_renderer.h"

#include <bgfx/bgfx.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat4x4.hpp>
#include <imgui.h>

#include <cstdint>
#include <cstring>

namespace roboslop::detail {

namespace {

// ImTextureID round-trips a bgfx texture index through a void*. Stored
// as idx + 1 so a valid handle with idx 0 is not confused with ImGui's
// "no texture" null id.
[[nodiscard]] auto toTextureId(bgfx::TextureHandle h) noexcept -> ImTextureID {
    // NOLINTNEXTLINE(performance-no-int-to-ptr) ImTextureID is a void* by ImGui's design.
    return reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(h.idx) + 1U);
}

[[nodiscard]] auto fromTextureId(ImTextureID id) noexcept -> bgfx::TextureHandle {
    return bgfx::TextureHandle{
        static_cast<std::uint16_t>(reinterpret_cast<std::uintptr_t>(id) - 1U)
    };
}

// Same packing as BGFX_STATE_BLEND_FUNC(src, dst), spelled out because
// the macro expands to C-style casts the engine's warning set rejects.
[[nodiscard]] constexpr auto blendFunc(std::uint64_t src, std::uint64_t dst) noexcept
    -> std::uint64_t {
    const std::uint64_t rgb = src | (dst << 4U);
    return rgb | (rgb << 8U);
}

} // namespace

auto imguiBgfxInit(bgfx::ProgramHandle program) -> ImguiBgfxRenderer {
    ImguiBgfxRenderer r;
    r.program = program;
    r.layout.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, /*normalized=*/true)
        .end();
    r.sampler = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);

    ImGuiIO& io = ImGui::GetIO();
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    r.fontTexture = bgfx::createTexture2D(
        static_cast<std::uint16_t>(width),
        static_cast<std::uint16_t>(height),
        /*hasMips=*/false,
        /*numLayers=*/1,
        bgfx::TextureFormat::BGRA8,
        0,
        bgfx::copy(pixels, static_cast<std::uint32_t>(width * height * 4))
    );
    io.Fonts->SetTexID(toTextureId(r.fontTexture));
    io.BackendRendererName = "roboslop_imgui_bgfx";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    return r;
}

auto imguiBgfxShutdown(ImguiBgfxRenderer& r) noexcept -> void {
    if (bgfx::isValid(r.fontTexture)) {
        bgfx::destroy(r.fontTexture);
        r.fontTexture = bgfx::TextureHandle{bgfx::kInvalidHandle};
    }
    if (bgfx::isValid(r.sampler)) {
        bgfx::destroy(r.sampler);
        r.sampler = bgfx::UniformHandle{bgfx::kInvalidHandle};
    }
    r.program = bgfx::ProgramHandle{bgfx::kInvalidHandle};
}

auto imguiBgfxRender(const ImguiBgfxRenderer& r, const ImDrawData* drawData, std::uint16_t viewId)
    -> void {
    const int fbWidth = static_cast<int>(drawData->DisplaySize.x * drawData->FramebufferScale.x);
    const int fbHeight = static_cast<int>(drawData->DisplaySize.y * drawData->FramebufferScale.y);
    if (fbWidth <= 0 || fbHeight <= 0) {
        return;
    }

    bgfx::setViewName(viewId, "devUi");
    bgfx::setViewMode(viewId, bgfx::ViewMode::Sequential);

    const bgfx::Caps* caps = bgfx::getCaps();
    const float x = drawData->DisplayPos.x;
    const float y = drawData->DisplayPos.y;
    const float w = drawData->DisplaySize.x;
    const float h = drawData->DisplaySize.y;
    const glm::mat4 ortho = caps->homogeneousDepth
                                ? glm::orthoRH_NO(x, x + w, y + h, y, 0.0F, 1000.0F)
                                : glm::orthoRH_ZO(x, x + w, y + h, y, 0.0F, 1000.0F);
    bgfx::setViewTransform(viewId, nullptr, glm::value_ptr(ortho));
    bgfx::setViewRect(
        viewId, 0, 0, static_cast<std::uint16_t>(fbWidth), static_cast<std::uint16_t>(fbHeight)
    );

    const ImVec2 clipOff = drawData->DisplayPos;
    const ImVec2 clipScale = drawData->FramebufferScale;

    for (int n = 0; n < drawData->CmdListsCount; ++n) {
        const ImDrawList* cmdList = drawData->CmdLists[n];
        const auto numVertices = static_cast<std::uint32_t>(cmdList->VtxBuffer.size());
        const auto numIndices = static_cast<std::uint32_t>(cmdList->IdxBuffer.size());
        if (numVertices == 0 || numIndices == 0) {
            continue;
        }

        if (bgfx::getAvailTransientVertexBuffer(numVertices, r.layout) < numVertices ||
            bgfx::getAvailTransientIndexBuffer(numIndices, sizeof(ImDrawIdx) == 4) < numIndices) {
            // Out of transient space this frame: drop the rest of the UI
            // rather than corrupt it. Next frame gets a fresh budget.
            break;
        }

        bgfx::TransientVertexBuffer tvb{};
        bgfx::TransientIndexBuffer tib{};
        bgfx::allocTransientVertexBuffer(&tvb, numVertices, r.layout);
        bgfx::allocTransientIndexBuffer(&tib, numIndices, sizeof(ImDrawIdx) == 4);
        std::memcpy(tvb.data, cmdList->VtxBuffer.begin(), numVertices * sizeof(ImDrawVert));
        std::memcpy(tib.data, cmdList->IdxBuffer.begin(), numIndices * sizeof(ImDrawIdx));

        for (const ImDrawCmd& cmd : cmdList->CmdBuffer) {
            if (cmd.UserCallback != nullptr) {
                cmd.UserCallback(cmdList, &cmd);
                continue;
            }
            if (cmd.ElemCount == 0) {
                continue;
            }

            const float clipX0 = (cmd.ClipRect.x - clipOff.x) * clipScale.x;
            const float clipY0 = (cmd.ClipRect.y - clipOff.y) * clipScale.y;
            const float clipX1 = (cmd.ClipRect.z - clipOff.x) * clipScale.x;
            const float clipY1 = (cmd.ClipRect.w - clipOff.y) * clipScale.y;
            if (clipX1 <= clipX0 || clipY1 <= clipY0) {
                continue;
            }
            const auto sx = static_cast<std::uint16_t>(clipX0 < 0.0F ? 0.0F : clipX0);
            const auto sy = static_cast<std::uint16_t>(clipY0 < 0.0F ? 0.0F : clipY0);
            const auto ex = static_cast<std::uint16_t>(
                clipX1 > static_cast<float>(fbWidth) ? static_cast<float>(fbWidth) : clipX1
            );
            const auto ey = static_cast<std::uint16_t>(
                clipY1 > static_cast<float>(fbHeight) ? static_cast<float>(fbHeight) : clipY1
            );
            bgfx::setScissor(
                sx, sy, static_cast<std::uint16_t>(ex - sx), static_cast<std::uint16_t>(ey - sy)
            );

            const bgfx::TextureHandle tex =
                cmd.GetTexID() != nullptr ? fromTextureId(cmd.GetTexID()) : r.fontTexture;

            constexpr std::uint64_t State =
                BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA |
                blendFunc(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);
            bgfx::setState(State);
            bgfx::setTexture(0, r.sampler, tex);
            bgfx::setVertexBuffer(0, &tvb, cmd.VtxOffset, numVertices);
            bgfx::setIndexBuffer(&tib, cmd.IdxOffset, cmd.ElemCount);
            bgfx::submit(viewId, r.program);
        }
    }
}

} // namespace roboslop::detail
