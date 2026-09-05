import roboslop.app;
import roboslop.assets.shader_compiler;
import roboslop.core.error;
import roboslop.ecs;
import roboslop.platform.input;
import roboslop.platform.window;
import roboslop.render.asset_cache;
import roboslop.render.camera;
import roboslop.render.context;
import roboslop.render.free_fly_camera;
import roboslop.render.frontend;
import roboslop.render.graph;
import roboslop.render.mesh;
import roboslop.render.primitives;
import roboslop.render.shader;
import roboslop.scene.transform;
import roboslop.sched;
import roboslop.ui;
import shaderlab.reload;
import shaderlab.watcher;

#include <bgfx/bgfx.h>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <imgui.h>

#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <print>
#include <string>
#include <vector>

namespace {

// Per-app state parked in the world context so the fixed system and
// both render passes can reach it without capturing app locals.
struct LabScene {
    roboslop::Entity sphere{};
    roboslop::Entity plane{};
    roboslop::Mesh planeMesh; // kept so the plane can be re-shown after hiding
    bool showPlane = true;
    bgfx::UniformHandle uParams{bgfx::kInvalidHandle};
    std::chrono::steady_clock::time_point start;
    // Written by the dev-UI pass on the render thread, read by the
    // camera system on the next frame's fixed step.
    bool uiWantsMouse = false;
    std::string diagBuffer; // mutable copy for ImGui's read-only text box
};

constexpr std::string_view VsName = "vs_lab";
constexpr std::string_view FsName = "fs_lab";

auto drawLabPanel(roboslop::World& world, LabScene& scene, shaderlab::ShaderReloader& reloader)
    -> void {

    const auto status = reloader.currentStatus();
    ImVec4 colour{0.7F, 0.7F, 0.7F, 1.0F};
    switch (status) {
    case shaderlab::ReloadStatus::Ok:
        colour = ImVec4(0.4F, 0.9F, 0.4F, 1.0F);
        break;
    case shaderlab::ReloadStatus::Failed:
        colour = ImVec4(0.95F, 0.35F, 0.35F, 1.0F);
        break;
    case shaderlab::ReloadStatus::Compiling:
        colour = ImVec4(0.95F, 0.85F, 0.3F, 1.0F);
        break;
    case shaderlab::ReloadStatus::Idle:
        break;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, colour);
    ImGui::TextUnformatted(std::format("Status: {}", shaderlab::statusName(status)).c_str());
    ImGui::PopStyleColor();
    if (reloader.compileGeneration() > 0) {
        ImGui::SameLine();
        ImGui::TextUnformatted(
            std::format(
                "(compile #{}, {:.0f} ms, {} handles rebound)",
                reloader.compileGeneration(),
                reloader.lastCompileMs(),
                reloader.lastRebindCount()
            )
                .c_str()
        );
    }

    const auto& set = reloader.shaderSet();
    ImGui::TextUnformatted(set.vsSource.string().c_str());
    ImGui::TextUnformatted(set.fsSource.string().c_str());
    ImGui::TextUnformatted(std::format("profile: {}", set.profile).c_str());

    if (ImGui::Button("Recompile now")) {
        reloader.requestCompile();
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Show plane", &scene.showPlane)) {
        if (scene.showPlane) {
            world.emplace<roboslop::Mesh>(scene.plane, scene.planeMesh);
        } else {
            world.remove<roboslop::Mesh>(scene.plane);
        }
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Diagnostics");
    scene.diagBuffer = reloader.diagnostics();
    if (scene.diagBuffer.empty()) {
        scene.diagBuffer = "(none)";
    }
    ImGui::InputTextMultiline(
        "##diagnostics",
        scene.diagBuffer.data(),
        scene.diagBuffer.size() + 1,
        ImVec2(-1.0F, -32.0F),
        ImGuiInputTextFlags_ReadOnly
    );
    ImGui::TextUnformatted("RMB + WASD / Space / Ctrl: fly.  Shift: boost.  Esc: quit.");
}

auto setupScene(roboslop::World& world, roboslop::AssetCache& assets) -> roboslop::Result<void> {
    auto prog = assets.program(VsName, FsName);
    if (!prog) {
        return std::unexpected(prog.error());
    }

    const auto cameraEntity = world.create();
    world.emplace<roboslop::Transform>(
        cameraEntity, roboslop::Transform{.position = {0.0F, 1.5F, 4.5F}}
    );
    world.emplace<roboslop::Camera>(
        cameraEntity, roboslop::Camera{.projection = roboslop::Perspective{}}
    );
    world.emplace<roboslop::ActiveCamera>(cameraEntity);
    world.emplace<roboslop::FreeFlyCamera>(
        cameraEntity, roboslop::FreeFlyCamera{.pitchRadians = -0.25F}
    );

    LabScene scene;
    scene.start = std::chrono::steady_clock::now();
    scene.uParams = assets.uniform("u_labParams", bgfx::UniformType::Vec4);

    auto sphereMesh = roboslop::makeGeometryMesh(roboslop::sphereGeometry(32, 64, 1.0F));
    sphereMesh.program = prog->value;
    scene.sphere = world.create();
    world.emplace<roboslop::Transform>(
        scene.sphere, roboslop::Transform{.position = {0.0F, 1.0F, 0.0F}}
    );
    world.emplace<roboslop::Mesh>(scene.sphere, sphereMesh);

    scene.planeMesh = roboslop::makeGeometryMesh(roboslop::planeGeometry(8.0F, 8));
    scene.planeMesh.program = prog->value;
    scene.plane = world.create();
    world.emplace<roboslop::Transform>(
        scene.plane, roboslop::Transform{.position = {0.0F, -0.5F, 0.0F}}
    );
    world.emplace<roboslop::Mesh>(scene.plane, scene.planeMesh);

    auto& ctx = world.registry().ctx();
    ctx.emplace<LabScene>(std::move(scene));

    // Runtime compiler: same shaderc binary and include path the
    // build-time pass uses, output profile chosen from the live
    // renderer so the blob format matches what bgfx expects.
    const std::filesystem::path srcDir{SHADERLAB_SHADER_SOURCE_DIR};
    const auto profile = roboslop::shaderProfileFor(bgfx::getRendererType());
    if (profile.empty()) {
        return std::unexpected(roboslop::toError(roboslop::ShaderError::UnsupportedRenderer));
    }
    auto& reloader = ctx.emplace<shaderlab::ShaderReloader>(
        roboslop::ShaderCompiler{std::filesystem::path{SHADERLAB_SHADERC_PATH}},
        shaderlab::LabShaderSet{
            .vsSource = srcDir / "vs_lab.sc",
            .fsSource = srcDir / "fs_lab.sc",
            .varyingDef = srcDir / "varying.def.sc",
            .includeDirs = {std::filesystem::path{SHADERLAB_BGFX_SHADER_INCLUDE}, srcDir},
            .profile = std::string{profile},
            .scratchDir = std::filesystem::temp_directory_path() / "roboslop-shaderlab",
            .cacheVs = std::string{VsName},
            .cacheFs = std::string{FsName},
        }
    );
    reloader.setCurrentProgram(*prog);

    const auto& s = reloader.shaderSet();
    ctx.emplace<shaderlab::ShaderWatcher>(
        std::vector<std::filesystem::path>{s.vsSource, s.fsSource, s.varyingDef}
    );

    if (auto* ui = roboslop::devUi(world); ui != nullptr) {
        ui->registerWindow(
            roboslop::DevWindow{
                .id = "shaderlab",
                .title = "Shader Lab",
                .draw =
                    [&world]() {
                        auto& c = world.registry().ctx();
                        drawLabPanel(world, c.get<LabScene>(), c.get<shaderlab::ShaderReloader>());
                    },
                .visible = true,
                .height = 380.0F,
                .collapsed = false,
            }
        );
    }
    return {};
}

auto buildGraphs(
    roboslop::SystemGraph& fixed, roboslop::RenderGraph& render, roboslop::FrameArena& arena
) -> void {
    fixed.add({
        .name = "freeFlyCameras",
        .reads = {"input"},
        .writes = {"transforms"},
        .run = [](roboslop::SystemCtx& c) {
            // A fly may only start while no dev-UI window wants the
            // mouse; the engine handles capture and release.
            const auto& scene = c.world->registry().ctx().get<LabScene>();
            roboslop::updateFreeFlyCameras(
                *c.world, *c.input, c.dt, /*allowCapture=*/!scene.uiWantsMouse
            );
        },
    });

    render.add({
        .name = "main",
        .reads = {"transforms"},
        .writes = {"framebuffer"},
        .record = [&arena](roboslop::PassCtx& c) {
            roboslop::applyActiveCamera(*c.world, c.viewId, c.viewportW, c.viewportH);
            auto& scene = c.world->registry().ctx().get<LabScene>();
            const float t =
                std::chrono::duration<float>(std::chrono::steady_clock::now() - scene.start)
                    .count();
            const float aspect =
                c.viewportH > 0 ? static_cast<float>(c.viewportW) / static_cast<float>(c.viewportH)
                                : 1.0F;
            const glm::vec4 params{t, aspect, 0.0F, 0.0F};
            bgfx::setUniform(scene.uParams, &params);
            auto draws = roboslop::collectMeshDraws(*c.world, arena, c.viewId);
            roboslop::sortDraws(draws);
            roboslop::submitDraws(draws);
        },
    });

    render.add({
        .name = "devUi",
        .reads = {"framebuffer"},
        .writes = {"framebuffer"},
        .record = [](roboslop::PassCtx& c) {
            auto* ui = roboslop::devUi(*c.world);
            if (ui == nullptr) {
                return;
            }
            auto& ctx = c.world->registry().ctx();
            auto& scene = ctx.get<LabScene>();
            auto& reloader = ctx.get<shaderlab::ShaderReloader>();
            auto& watcher = ctx.get<shaderlab::ShaderWatcher>();

            if (watcher.poll(std::chrono::steady_clock::now())) {
                reloader.requestCompile();
            }
            reloader.pump(*c.world, *c.assets);

            ui->beginFrame();
            ui->drawWindows();
            scene.uiWantsMouse = ui->wantCaptureMouse();
            ui->endFrame(c.viewId);
        },
    });
}

} // namespace

auto main() -> int {
    auto app = roboslop::App::make(
        roboslop::AppConfig{
            .window = roboslop::WindowConfig{.title = "shaderlab", .width = 1280, .height = 720},
            .tickRateHz = 60.0,
            .assetRoot = "assets",
            .enableDevUi = true,
            .onSetup = setupScene,
            .onBuildGraphs = buildGraphs,
        }
    );

    if (!app) {
        std::println(
            stderr, "shaderlab init failed: {} ({})", app.error().message, app.error().context
        );
        return 1;
    }

    const auto result = app->run();
    if (!result) {
        std::println(
            stderr,
            "shaderlab exited with error: {} ({})",
            result.error().message,
            result.error().context
        );
        return 1;
    }
    return 0;
}
