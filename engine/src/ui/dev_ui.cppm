module;

#include <bgfx/bgfx.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>

#include <cstdint>
#include <expected>
#include <utility>

#include "imgui_bgfx_renderer.h"

export module roboslop.ui;

import roboslop.core.error;
import roboslop.ecs;
import roboslop.platform.window;
import roboslop.render.asset_cache;

namespace roboslop {

// Developer UI: one Dear ImGui context rendered through bgfx, with the
// GLFW platform backend for input. Owned by App (when
// AppConfig::enableDevUi is set) and reached from passes via the world
// context, like JoltWorld. Apps draw their panels between beginFrame()
// and endFrame() inside their own last render pass; the engine adds no
// widgets of its own.
//
// Lifetime: created after bgfx init and the AssetCache (it borrows the
// vs_imgui/fs_imgui program from there), destroyed before both.
export class DevUi {
  public:
    [[nodiscard]] static auto make(const Window& window, AssetCache& assets) -> Result<DevUi> {
        auto program = assets.program("vs_imgui", "fs_imgui");
        if (!program) {
            return std::unexpected(program.error());
        }

        IMGUI_CHECKVERSION();
        ImGuiContext* ctx = ImGui::CreateContext();
        ImGui::SetCurrentContext(ctx);
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.IniFilename = nullptr; // no imgui.ini next to the binary
        ImGui::StyleColorsDark();

        // install_callbacks=true chains onto whatever GLFW callbacks are
        // already set, so Window's own resize callback keeps working.
        ImGui_ImplGlfw_InitForOther(window.glfwHandle(), /*install_callbacks=*/true);
        auto renderer = detail::imguiBgfxInit(program->value);
        return DevUi{ctx, renderer};
    }

    DevUi(const DevUi&) = delete;
    auto operator=(const DevUi&) -> DevUi& = delete;

    DevUi(DevUi&& other) noexcept
        : ctx(std::exchange(other.ctx, nullptr)), renderer(other.renderer) {}

    auto operator=(DevUi&& other) noexcept -> DevUi& {
        if (this != &other) {
            shutdown();
            ctx = std::exchange(other.ctx, nullptr);
            renderer = other.renderer;
        }
        return *this;
    }

    ~DevUi() {
        shutdown();
    }

    // Starts an ImGui frame. Display size, framebuffer scale, and
    // delta time come from the GLFW backend.
    auto beginFrame() -> void {
        ImGui::SetCurrentContext(ctx);
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    // Finalises the frame and submits it into `viewId`. Call on the
    // bgfx API thread, from the pass that owns that view.
    auto endFrame(std::uint16_t viewId) -> void {
        ImGui::SetCurrentContext(ctx);
        ImGui::Render();
        detail::imguiBgfxRender(renderer, ImGui::GetDrawData(), viewId);
    }

    // True when ImGui is using the mouse / keyboard this frame, so the
    // app can skip its own camera or gameplay input. Valid after
    // beginFrame().
    [[nodiscard]] auto wantCaptureMouse() const noexcept -> bool {
        return ctx != nullptr && ImGui::GetIO().WantCaptureMouse;
    }

    [[nodiscard]] auto wantCaptureKeyboard() const noexcept -> bool {
        return ctx != nullptr && ImGui::GetIO().WantCaptureKeyboard;
    }

  private:
    DevUi(ImGuiContext* ctx, detail::ImguiBgfxRenderer renderer) noexcept
        : ctx(ctx), renderer(renderer) {}

    auto shutdown() noexcept -> void {
        if (ctx == nullptr) {
            return;
        }
        ImGui::SetCurrentContext(ctx);
        detail::imguiBgfxShutdown(renderer);
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext(ctx);
        ctx = nullptr;
    }

    ImGuiContext* ctx = nullptr;
    detail::ImguiBgfxRenderer renderer;
};

// Engine-side installation into the ECS context, mirroring
// installJoltWorld: passes reach the DevUi through PassCtx.world.
export auto installDevUi(World& world, DevUi& ui) -> void {
    world.registry().ctx().emplace<DevUi*>(&ui);
}

// nullptr when no DevUi was installed (dev UI disabled for this App).
export [[nodiscard]] auto devUi(World& world) -> DevUi* {
    auto* p = world.registry().ctx().find<DevUi*>();
    return p == nullptr ? nullptr : *p;
}

} // namespace roboslop
