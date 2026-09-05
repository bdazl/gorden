module;

#include <bgfx/bgfx.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>

#include <algorithm>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "imgui_bgfx_renderer.h"

export module roboslop.ui;

import roboslop.core.error;
import roboslop.ecs;
import roboslop.platform.window;
import roboslop.render.asset_cache;

namespace roboslop {

export struct DevUiConfig {
    // Where ImGui persists window positions / docking layout. Empty
    // keeps everything in memory for the session.
    std::filesystem::path iniPath;
};

// A registered dev window. The registry owns Begin/End and the View
// menu entry; `draw` only emits the window's contents. `id` is stable
// (used for persisted layout), `title` is what the user sees. `height`
// is the expanded height in the stack; the user can drag it.
export struct DevWindow {
    std::string id;
    std::string title;
    std::function<void()> draw;
    bool visible = true;
    float height = 300.0F;
    bool collapsed = false;
};

// Per-window layout as persisted by apps.
export struct DevWindowState {
    std::string id;
    bool visible = true;
    float height = 300.0F;
    bool collapsed = false;
};

export struct DevLayout {
    float stackWidth = 0.0F; // 0 = keep the current width
    std::vector<DevWindowState> windows;
};

// The stack geometry. Windows sit in one column anchored to the
// top-right corner, sharing a width; each keeps its own height and can
// be collapsed to its title bar.
export inline constexpr float DevStackDefaultWidth = 440.0F;
export inline constexpr float DevStackMinWidth = 240.0F;
export inline constexpr float DevStackMargin = 8.0F;
export inline constexpr float DevStackGap = 4.0F;

// Developer UI: one Dear ImGui context rendered through bgfx, with the
// GLFW platform backend for input. Owned by App (when
// AppConfig::enableDevUi is set) and reached from passes via the world
// context, like JoltWorld.
//
// Apps register windows once (registerWindow, typically in onSetup)
// and, in their last render pass, call beginFrame() → drawWindows() →
// any ad-hoc ImGui → endFrame(). drawWindows() draws the main menu bar
// with a View menu (one checkbox per window, Show / Hide / Collapse /
// Expand all) and the visible windows as a *stack* in the top-right
// corner: one shared width (drag any window's left edge), per-window
// heights (drag the bottom edge), collapse via the title-bar arrow or
// a double-click. F1 (handled by App) toggles the whole overlay.
//
// Lifetime: created after bgfx init and the AssetCache (it borrows the
// vs_imgui/fs_imgui program from there), destroyed before both.
export class DevUi {
  public:
    [[nodiscard]] static auto
    make(const Window& window, AssetCache& assets, DevUiConfig config = {}) -> Result<DevUi> {
        auto program = assets.program("vs_imgui", "fs_imgui");
        if (!program) {
            return std::unexpected(program.error());
        }

        IMGUI_CHECKVERSION();
        ImGuiContext* ctx = ImGui::CreateContext();
        ImGui::SetCurrentContext(ctx);
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.IniFilename = nullptr; // set below once the path string is owned
        ImGui::StyleColorsDark();

        // install_callbacks=true chains onto whatever GLFW callbacks are
        // already set, so Window's own resize callback keeps working.
        ImGui_ImplGlfw_InitForOther(window.glfwHandle(), /*install_callbacks=*/true);
        auto renderer = detail::imguiBgfxInit(program->value);
        DevUi ui{ctx, renderer};
        ui.iniPath = config.iniPath.string();
        if (!ui.iniPath.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(config.iniPath.parent_path(), ec);
            io.IniFilename = ui.iniPath.c_str();
        }
        return ui;
    }

    DevUi(const DevUi&) = delete;
    auto operator=(const DevUi&) -> DevUi& = delete;

    DevUi(DevUi&& other) noexcept
        : ctx(std::exchange(other.ctx, nullptr)), renderer(other.renderer),
          windows(std::move(other.windows)), iniPath(std::move(other.iniPath)),
          stackWidth(other.stackWidth), enabled(other.enabled) {
        rebindIni();
    }

    auto operator=(DevUi&& other) noexcept -> DevUi& {
        if (this != &other) {
            shutdown();
            ctx = std::exchange(other.ctx, nullptr);
            renderer = other.renderer;
            windows = std::move(other.windows);
            iniPath = std::move(other.iniPath);
            stackWidth = other.stackWidth;
            enabled = other.enabled;
            rebindIni();
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

    // --- Window registry -------------------------------------------------

    // Registers (or replaces, by id) a window. Visibility given here is
    // the default; applyVisibility() overrides it from saved settings.
    auto registerWindow(DevWindow window) -> void {
        auto it = std::ranges::find(windows, window.id, &DevWindow::id);
        if (it != windows.end()) {
            *it = std::move(window);
        } else {
            windows.push_back(std::move(window));
        }
    }

    // Menu bar + the stack of visible registered windows. Call between
    // beginFrame() and endFrame(). Does nothing while the overlay is
    // disabled (F1), so the frame still renders an empty draw list.
    auto drawWindows() -> void {
        if (!enabled) {
            return;
        }
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("View")) {
                for (auto& w : windows) {
                    ImGui::MenuItem(w.title.c_str(), nullptr, &w.visible);
                }
                if (!windows.empty()) {
                    ImGui::Separator();
                }
                if (ImGui::MenuItem("Show all")) {
                    for (auto& w : windows) {
                        w.visible = true;
                    }
                }
                if (ImGui::MenuItem("Hide all")) {
                    for (auto& w : windows) {
                        w.visible = false;
                    }
                }
                if (ImGui::MenuItem("Collapse all")) {
                    for (auto& w : windows) {
                        w.collapsed = true;
                    }
                }
                if (ImGui::MenuItem("Expand all")) {
                    for (auto& w : windows) {
                        w.collapsed = false;
                    }
                }
                ImGui::Separator();
                ImGui::MenuItem("Hide overlay", "F1", &enabledMenuProxy);
                if (!enabledMenuProxy) {
                    enabledMenuProxy = true;
                    enabled = false;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // Stack layout. Position and size are imposed every frame from
        // our own state; what the user drags is read back after Begin
        // and becomes next frame's state, so resizing still works while
        // the column stays anchored to the right edge.
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        float y = vp->WorkPos.y + DevStackMargin;
        for (auto& w : windows) {
            if (!w.visible) {
                continue;
            }
            const float x = vp->WorkPos.x + vp->WorkSize.x - stackWidth - DevStackMargin;
            ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(stackWidth, w.height), ImGuiCond_Always);
            ImGui::SetNextWindowSizeConstraints(
                ImVec2(DevStackMinWidth, 80.0F),
                ImVec2(vp->WorkSize.x - 2.0F * DevStackMargin, vp->WorkSize.y)
            );
            ImGui::SetNextWindowCollapsed(w.collapsed, ImGuiCond_Always);
            constexpr ImGuiWindowFlags Flags = ImGuiWindowFlags_NoMove |
                                               ImGuiWindowFlags_NoDocking |
                                               ImGuiWindowFlags_NoSavedSettings;
            const bool open = ImGui::Begin(w.title.c_str(), &w.visible, Flags);
            const ImVec2 size = ImGui::GetWindowSize();
            w.collapsed = ImGui::IsWindowCollapsed();
            if (!w.collapsed) {
                w.height = size.y;
                if (size.x != stackWidth) {
                    stackWidth = std::clamp(
                        size.x, DevStackMinWidth, vp->WorkSize.x - 2.0F * DevStackMargin
                    );
                }
            }
            if (open && w.draw) {
                w.draw();
            }
            ImGui::End();
            y += size.y + DevStackGap;
        }
    }

    [[nodiscard]] auto windowVisible(std::string_view id) const noexcept -> bool {
        const auto it = std::ranges::find(windows, id, &DevWindow::id);
        return it != windows.end() && it->visible;
    }

    auto setWindowVisible(std::string_view id, bool visible) -> void {
        if (auto it = std::ranges::find(windows, id, &DevWindow::id); it != windows.end()) {
            it->visible = visible;
        }
    }

    // Snapshot for settings persistence, and its inverse. Unknown ids
    // in applyLayout are ignored (a window that no longer exists); a
    // zero stackWidth keeps the current one.
    [[nodiscard]] auto layout() const -> DevLayout {
        DevLayout out;
        out.stackWidth = stackWidth;
        out.windows.reserve(windows.size());
        for (const auto& w : windows) {
            out.windows.push_back(
                {.id = w.id, .visible = w.visible, .height = w.height, .collapsed = w.collapsed}
            );
        }
        return out;
    }

    auto applyLayout(const DevLayout& saved) -> void {
        if (saved.stackWidth >= DevStackMinWidth) {
            stackWidth = saved.stackWidth;
        }
        for (const auto& v : saved.windows) {
            if (auto it = std::ranges::find(windows, v.id, &DevWindow::id); it != windows.end()) {
                it->visible = v.visible;
                if (v.height >= 80.0F) {
                    it->height = v.height;
                }
                it->collapsed = v.collapsed;
            }
        }
    }

    [[nodiscard]] auto stackWidthPx() const noexcept -> float {
        return stackWidth;
    }

    // Whole-overlay toggle (F1). Input still flows through ImGui so the
    // GLFW backend stays consistent; only drawing is skipped.
    auto toggleEnabled() noexcept -> void {
        enabled = !enabled;
    }

    [[nodiscard]] auto isEnabled() const noexcept -> bool {
        return enabled;
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

    // io.IniFilename points into `iniPath`, which moves with the
    // object; re-point it after every move.
    auto rebindIni() noexcept -> void {
        if (ctx != nullptr) {
            ImGui::SetCurrentContext(ctx);
            ImGui::GetIO().IniFilename = iniPath.empty() ? nullptr : iniPath.c_str();
        }
    }

    ImGuiContext* ctx = nullptr;
    detail::ImguiBgfxRenderer renderer;
    std::vector<DevWindow> windows;
    std::string iniPath;
    float stackWidth = DevStackDefaultWidth;
    bool enabled = true;
    bool enabledMenuProxy = true; // MenuItem needs a bool* to toggle
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
