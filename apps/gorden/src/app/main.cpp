import gorden.agent.brain;
import gorden.agent.observation;
import gorden.agent.robot;
import gorden.settings;
import roboslop.app;
import roboslop.audio;
import roboslop.core.error;
import roboslop.core.paths;
import roboslop.ecs;
import roboslop.llm;
import roboslop.llm.backend;
import roboslop.physics;
import roboslop.physics.components;
import roboslop.platform.input;
import roboslop.platform.window;
import roboslop.render.asset_cache;
import roboslop.render.camera;
import roboslop.render.context;
import roboslop.render.free_fly_camera;
import roboslop.render.frontend;
import roboslop.render.graph;
import roboslop.render.lighting;
import roboslop.render.material;
import roboslop.render.mesh;
import roboslop.scene.transform;
import roboslop.sched;
import roboslop.ui;

#include <bgfx/bgfx.h>
#include <glm/vec3.hpp>
#include <imgui.h>
#include <spdlog/spdlog.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <filesystem>
#include <format>
#include <map>
#include <memory>
#include <print>
#include <span>
#include <string>
#include <vector>

namespace {

// Cube mesh shared by every physics-driven entity in the M1 demo. Until
// the asset pipeline (M2) lands, geometry is hard-coded here. Eight
// corners coloured one face per axis sign so the rotation is readable
// at a glance once Jolt is integrating angular velocity.
struct CubeVertex {
    float x;
    float y;
    float z;
    std::uint32_t abgr;
};

constexpr std::array<CubeVertex, 8> kCubeVertices = {
    CubeVertex{-0.5F, -0.5F, -0.5F, 0xFFFF0000U},
    CubeVertex{+0.5F, -0.5F, -0.5F, 0xFFFF8800U},
    CubeVertex{+0.5F, +0.5F, -0.5F, 0xFFFFFF00U},
    CubeVertex{-0.5F, +0.5F, -0.5F, 0xFF00FF00U},
    CubeVertex{-0.5F, -0.5F, +0.5F, 0xFF00FFFFU},
    CubeVertex{+0.5F, -0.5F, +0.5F, 0xFF0080FFU},
    CubeVertex{+0.5F, +0.5F, +0.5F, 0xFF0000FFU},
    CubeVertex{-0.5F, +0.5F, +0.5F, 0xFFFF00FFU},
};

constexpr std::array<std::uint16_t, 36> kCubeIndices = {
    0, 1, 2, 0, 2, 3, // -Z
    4, 6, 5, 4, 7, 6, // +Z
    0, 3, 7, 0, 7, 4, // -X
    1, 5, 6, 1, 6, 2, // +X
    3, 2, 6, 3, 6, 7, // +Y
    0, 4, 5, 0, 5, 1, // -Y
};

// Textured cube — 24 unique vertices (one per face corner) so the UVs
// and face-aligned normals don't share verts across faces. Each face
// is a unit-square 0..1 UV mapping.
struct TexCubeVertex {
    float position[3];
    float normal[3];
    float uv[2];
};

constexpr std::array<TexCubeVertex, 24> kTexCubeVertices = {
    // -Z face (normal 0,0,-1)
    TexCubeVertex{{-0.5F, -0.5F, -0.5F}, {0, 0, -1}, {0, 0}},
    TexCubeVertex{{+0.5F, -0.5F, -0.5F}, {0, 0, -1}, {1, 0}},
    TexCubeVertex{{+0.5F, +0.5F, -0.5F}, {0, 0, -1}, {1, 1}},
    TexCubeVertex{{-0.5F, +0.5F, -0.5F}, {0, 0, -1}, {0, 1}},
    // +Z
    TexCubeVertex{{-0.5F, -0.5F, +0.5F}, {0, 0, +1}, {0, 0}},
    TexCubeVertex{{+0.5F, -0.5F, +0.5F}, {0, 0, +1}, {1, 0}},
    TexCubeVertex{{+0.5F, +0.5F, +0.5F}, {0, 0, +1}, {1, 1}},
    TexCubeVertex{{-0.5F, +0.5F, +0.5F}, {0, 0, +1}, {0, 1}},
    // -X
    TexCubeVertex{{-0.5F, -0.5F, -0.5F}, {-1, 0, 0}, {0, 0}},
    TexCubeVertex{{-0.5F, +0.5F, -0.5F}, {-1, 0, 0}, {1, 0}},
    TexCubeVertex{{-0.5F, +0.5F, +0.5F}, {-1, 0, 0}, {1, 1}},
    TexCubeVertex{{-0.5F, -0.5F, +0.5F}, {-1, 0, 0}, {0, 1}},
    // +X
    TexCubeVertex{{+0.5F, -0.5F, -0.5F}, {+1, 0, 0}, {0, 0}},
    TexCubeVertex{{+0.5F, +0.5F, -0.5F}, {+1, 0, 0}, {1, 0}},
    TexCubeVertex{{+0.5F, +0.5F, +0.5F}, {+1, 0, 0}, {1, 1}},
    TexCubeVertex{{+0.5F, -0.5F, +0.5F}, {+1, 0, 0}, {0, 1}},
    // -Y
    TexCubeVertex{{-0.5F, -0.5F, -0.5F}, {0, -1, 0}, {0, 0}},
    TexCubeVertex{{+0.5F, -0.5F, -0.5F}, {0, -1, 0}, {1, 0}},
    TexCubeVertex{{+0.5F, -0.5F, +0.5F}, {0, -1, 0}, {1, 1}},
    TexCubeVertex{{-0.5F, -0.5F, +0.5F}, {0, -1, 0}, {0, 1}},
    // +Y
    TexCubeVertex{{-0.5F, +0.5F, -0.5F}, {0, +1, 0}, {0, 0}},
    TexCubeVertex{{+0.5F, +0.5F, -0.5F}, {0, +1, 0}, {1, 0}},
    TexCubeVertex{{+0.5F, +0.5F, +0.5F}, {0, +1, 0}, {1, 1}},
    TexCubeVertex{{-0.5F, +0.5F, +0.5F}, {0, +1, 0}, {0, 1}},
};

constexpr std::array<std::uint16_t, 36> kTexCubeIndices = {
    0,  1,  2,  0,  2,  3,  // -Z
    4,  6,  5,  4,  7,  6,  // +Z
    8,  9,  10, 8,  10, 11, // -X
    12, 14, 13, 12, 15, 14, // +X
    16, 18, 17, 16, 19, 18, // -Y
    20, 21, 22, 20, 22, 23, // +Y
};

// 4×4 RGBA checkerboard so the cube's UV mapping is obvious at a
// glance. Stored row-major top-to-bottom (stbi's convention, mirrored
// by aiProcess_FlipUVs for Assimp loads).
constexpr std::array<std::uint8_t, 4 * 4 * 4> kCheckerPixels = [] {
    std::array<std::uint8_t, 64> p{};
    for (std::size_t y = 0; y < 4; ++y) {
        for (std::size_t x = 0; x < 4; ++x) {
            const bool dark = ((x + y) & 1U) != 0U;
            const std::size_t i = (y * 4U + x) * 4U;
            p[i + 0] = dark ? std::uint8_t{32} : std::uint8_t{220};
            p[i + 1] = dark ? std::uint8_t{32} : std::uint8_t{220};
            p[i + 2] = dark ? std::uint8_t{96} : std::uint8_t{80};
            p[i + 3] = std::uint8_t{255};
        }
    }
    return p;
}();

auto spawnDynamicCube(roboslop::World& world, const roboslop::Mesh& mesh, glm::vec3 pos) -> void {
    const auto e = world.create();
    world.emplace<roboslop::Transform>(e, roboslop::Transform{.position = pos});
    world.emplace<roboslop::Mesh>(e, mesh);
    world.emplace<roboslop::BodyDesc>(
        e,
        roboslop::BodyDesc{
            .shape = roboslop::BoxShape{.halfExtents = {0.5F, 0.5F, 0.5F}},
            .motion = roboslop::BodyMotion::Dynamic,
            .mass = 1.0F,
            .friction = 0.5F,
            .restitution = 0.1F,
        }
    );
}

auto spawnDynamicSphere(roboslop::World& world, const roboslop::Mesh& mesh, glm::vec3 pos) -> void {
    const auto e = world.create();
    world.emplace<roboslop::Transform>(e, roboslop::Transform{.position = pos});
    world.emplace<roboslop::Mesh>(e, mesh);
    world.emplace<roboslop::BodyDesc>(
        e,
        roboslop::BodyDesc{
            .shape = roboslop::SphereShape{.radius = 0.5F},
            .motion = roboslop::BodyMotion::Dynamic,
            .mass = 1.0F,
            .friction = 0.5F,
            .restitution = 0.4F,
        }
    );
}

// A named, static prop the robot can perceive and inspect. Reuses the
// vertex-coloured cube; physics keeps the player's dynamic cubes from
// falling through it.
auto spawnProp(
    roboslop::World& world, const roboslop::Mesh& mesh, std::string name, glm::vec3 pos, float size
) -> void {
    const auto e = world.create();
    world.emplace<roboslop::Transform>(
        e, roboslop::Transform{.position = pos, .scale = {size, size, size}}
    );
    world.emplace<roboslop::Mesh>(e, mesh);
    world.emplace<gorden::Named>(e, gorden::Named{.name = std::move(name)});
    world.emplace<roboslop::BodyDesc>(
        e,
        roboslop::BodyDesc{
            .shape = roboslop::BoxShape{.halfExtents = {size * 0.5F, size * 0.5F, size * 0.5F}},
            .motion = roboslop::BodyMotion::Static,
        }
    );
}

// Per-frame UI state for the Robot panel, parked in the world context.
struct RobotPanelState {
    std::array<char, 256> input{};
    bool uiWantsMouse = false;
    bool cameraActive = false;
    bool scrollTranscript = false;
};

// Settings as loaded/edited, plus the edit buffers for the Settings
// window and the last window-visibility snapshot (saved on change).
struct SettingsState {
    gorden::GordenSettings settings;
    std::filesystem::path path;
    std::array<char, 64> playerBuf{};
    std::array<char, 64> robotBuf{};
    std::map<std::string, bool> lastVisibility;
    std::string status;
};

auto copyToBuffer(std::array<char, 64>& buf, const std::string& text) -> void {
    buf.fill('\0');
    std::strncpy(buf.data(), text.c_str(), buf.size() - 1);
}

// Pushes the names in `st.settings` into the world: Named components
// and the brain's prompt.
auto applyNames(roboslop::World& world, SettingsState& st, gorden::AgentBrain& brain) -> void {
    world.get<gorden::Named>(brain.robotEntity()).name = st.settings.robotName;
    world.get<gorden::Named>(brain.playerEntity()).name = st.settings.playerName;
    brain.setNames(st.settings.robotName, st.settings.playerName);
}

auto saveSettingsNow(SettingsState& st) -> void {
    if (auto r = gorden::saveSettings(st.path, st.settings); !r) {
        st.status = std::format("save failed: {} ({})", r.error().message, r.error().context);
        spdlog::warn("gorden: {}", st.status);
    } else {
        st.status = "saved to " + st.path.string();
    }
}

auto readNameBuffers(SettingsState& st) -> void {
    if (st.playerBuf[0] != '\0') {
        st.settings.playerName = st.playerBuf.data();
    }
    if (st.robotBuf[0] != '\0') {
        st.settings.robotName = st.robotBuf.data();
    }
}

auto drawSettingsPanel(roboslop::World& world, SettingsState& st, gorden::AgentBrain& brain)
    -> void {
    ImGui::InputText("Player name", st.playerBuf.data(), st.playerBuf.size());
    ImGui::InputText("Robot name", st.robotBuf.data(), st.robotBuf.size());
    if (ImGui::Button("Apply")) {
        readNameBuffers(st);
        applyNames(world, st, brain);
        st.status = "applied";
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        readNameBuffers(st);
        applyNames(world, st, brain);
        saveSettingsNow(st);
    }
    ImGui::TextDisabled("%s", st.path.string().c_str());
    if (!st.status.empty()) {
        ImGui::TextUnformatted(st.status.c_str());
    }
}

// OPENAI_API_KEY present → the OpenAI-compatible backend (model and
// base URL overridable via GORDEN_MODEL / OPENAI_BASE_URL, so the same
// code targets a llama.cpp server). Otherwise a scripted demo so the
// whole chain still runs. The key is read once and never logged.
auto makeProvider() -> std::unique_ptr<roboslop::Provider> {
    const char* key = std::getenv("OPENAI_API_KEY");
    if (key != nullptr && key[0] != '\0') {
        roboslop::OpenAiConfig cfg;
        cfg.apiKey = key;
        if (const char* model = std::getenv("GORDEN_MODEL"); model != nullptr && model[0] != '\0') {
            cfg.model = model;
        }
        if (const char* base = std::getenv("OPENAI_BASE_URL"); base != nullptr && base[0] != '\0') {
            cfg.baseUrl = base;
        }
        spdlog::info(
            "gorden: LLM backend openai-compatible, model={}, base={}", cfg.model, cfg.baseUrl
        );
        return std::make_unique<roboslop::OpenAiProvider>(std::move(cfg));
    }
    spdlog::warn("gorden: OPENAI_API_KEY not set; using the scripted demo provider");
    auto say = [](std::string text, std::string id) {
        return roboslop::ToolCall{
            .id = std::move(id),
            .name = "say",
            .argumentsJson = "{\"text\":\"" + std::move(text) + "\"}",
        };
    };
    std::vector<roboslop::ChatResponse> script{
        roboslop::ChatResponse{
            .toolCalls =
                {say("Hello! I am a scripted robot. Watch me walk to the generator.", "s1"),
                 {.id = "s2", .name = "moveTo", .argumentsJson = "{\"x\":0,\"z\":-4.5}"}},
            .finishReason = "tool_calls",
        },
        roboslop::ChatResponse{
            .toolCalls = {say("I am at the generator. Set OPENAI_API_KEY for a real brain.", "s3")},
            .finishReason = "tool_calls",
        },
    };
    return std::make_unique<roboslop::ScriptedProvider>(
        std::move(script),
        roboslop::ChatResponse{
            .toolCalls = {say("(scripted) I have run out of script.", "s0")},
            .finishReason = "tool_calls",
        }
    );
}

auto drawRobotPanel(roboslop::World& world, gorden::AgentBrain& brain, RobotPanelState& st)
    -> void {

    ImGui::TextUnformatted(
        std::format(
            "provider: {}   state: {}   thinks: {}",
            brain.providerName(),
            brain.thinking() ? "Thinking" : "Idle",
            brain.thinkCount()
        )
            .c_str()
    );
    ImGui::Separator();

    ImGui::BeginChild("transcript", ImVec2(0.0F, 180.0F), ImGuiChildFlags_Border);
    const auto& names = brain.config();
    for (const auto& line : brain.transcript()) {
        const bool robot = line.who == "robot";
        ImGui::PushStyleColor(
            ImGuiCol_Text, robot ? ImVec4(0.6F, 0.9F, 1.0F, 1.0F) : ImVec4(0.9F, 0.9F, 0.9F, 1.0F)
        );
        ImGui::TextWrapped(
            "%s: %s", (robot ? names.robotName : names.playerName).c_str(), line.text.c_str()
        );
        ImGui::PopStyleColor();
    }
    if (st.scrollTranscript) {
        ImGui::SetScrollHereY(1.0F);
        st.scrollTranscript = false;
    }
    ImGui::EndChild();

    ImGui::SetNextItemWidth(-80.0F);
    const bool entered = ImGui::InputText(
        "##say", st.input.data(), st.input.size(), ImGuiInputTextFlags_EnterReturnsTrue
    );
    ImGui::SameLine();
    const bool clicked = ImGui::Button("Send", ImVec2(-1.0F, 0.0F));
    if ((entered || clicked) && st.input[0] != '\0') {
        brain.playerSays(std::string{st.input.data()});
        st.input.fill('\0');
        st.scrollTranscript = true;
        ImGui::SetKeyboardFocusHere(-1);
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Validated action log");
    ImGui::BeginChild("log", ImVec2(0.0F, 0.0F), ImGuiChildFlags_Border);
    const auto& log = brain.actionLog();
    const std::size_t first = log.size() > 60 ? log.size() - 60 : 0;
    for (std::size_t i = first; i < log.size(); ++i) {
        ImGui::TextWrapped("%s", log[i].c_str());
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0F) {
        ImGui::SetScrollHereY(1.0F);
    }
    ImGui::EndChild();
    (void)world;
}

auto spawnGround(roboslop::World& world, const roboslop::Mesh& mesh) -> void {
    const auto e = world.create();
    world.emplace<roboslop::Transform>(
        e,
        roboslop::Transform{
            .position = {0.0F, -1.0F, 0.0F},
            .scale = {20.0F, 1.0F, 20.0F},
        }
    );
    world.emplace<roboslop::Mesh>(e, mesh);
    world.emplace<roboslop::BodyDesc>(
        e,
        roboslop::BodyDesc{
            .shape = roboslop::BoxShape{.halfExtents = {10.0F, 0.5F, 10.0F}},
            .motion = roboslop::BodyMotion::Static,
        }
    );
}

} // namespace

auto main() -> int {
    // Settings first: names are needed while the scene is built.
    SettingsState initial;
    initial.path = gorden::settingsPath();
    if (auto loaded = gorden::loadSettings(initial.path); loaded) {
        initial.settings = std::move(*loaded);
    } else {
        spdlog::warn("gorden: settings unreadable ({}); using defaults", loaded.error().context);
    }
    copyToBuffer(initial.playerBuf, initial.settings.playerName);
    copyToBuffer(initial.robotBuf, initial.settings.robotName);

    auto app = roboslop::App::make(
        roboslop::AppConfig{
            .window = roboslop::WindowConfig{.title = "gorden", .width = 1280, .height = 720},
            .tickRateHz = 60.0,
            .assetRoot = "assets",
            .enableDevUi = true,
            .devUiIniPath = roboslop::configDir() / "gorden.imgui.ini",
            .onSetup = [initial](roboslop::World& world, roboslop::AssetCache& assets)
                -> roboslop::Result<void> {
                auto prog = assets.program("vs_basic", "fs_basic");
                if (!prog) {
                    return std::unexpected(prog.error());
                }

                const auto cameraEntity = world.create();
                world.emplace<roboslop::Transform>(
                    cameraEntity, roboslop::Transform{.position = {6.0F, 4.0F, 12.0F}}
                );
                world.emplace<roboslop::Camera>(
                    cameraEntity, roboslop::Camera{.projection = roboslop::Perspective{}}
                );
                world.emplace<roboslop::ActiveCamera>(cameraEntity);
                world.emplace<roboslop::FreeFlyCamera>(cameraEntity);
                // The camera doubles as the audio listener so 3D
                // attenuation tracks the viewer.
                world.emplace<roboslop::AudioListener>(cameraEntity);
                world.emplace<gorden::Named>(
                    cameraEntity, gorden::Named{.name = initial.settings.playerName}
                );

                const auto layout = roboslop::vertexLayoutPosColor();
                auto cube = roboslop::makeStaticMesh(
                    std::as_bytes(std::span{kCubeVertices}), std::span{kCubeIndices}, layout
                );
                cube.program = prog->value;

                spawnGround(world, cube);
                spawnProp(world, cube, "crate-1", {4.0F, 0.0F, -2.0F}, 1.0F);
                spawnProp(world, cube, "crate-2", {-4.0F, 0.0F, -3.0F}, 1.0F);
                spawnProp(world, cube, "generator", {0.0F, 0.25F, -6.0F}, 1.5F);
                spawnDynamicCube(world, cube, glm::vec3{-1.5F, 5.0F, 0.0F});
                spawnDynamicCube(world, cube, glm::vec3{+1.5F, 5.5F, 0.0F});
                spawnDynamicCube(world, cube, glm::vec3{0.0F, 7.0F, -1.0F});
                spawnDynamicSphere(world, cube, glm::vec3{0.5F, 9.0F, 0.5F});
                spawnDynamicSphere(world, cube, glm::vec3{-0.5F, 11.0F, 0.5F});

                // Textured cube — wires the M2 asset path end-to-end:
                // pos+normal+uv vertex layout, a procedurally built
                // RGBA8 albedo, the textured shader pair, and a
                // Material component the frontend reads at draw time.
                auto texProg = assets.program("vs_textured", "fs_textured");
                if (!texProg) {
                    return std::unexpected(texProg.error());
                }
                const auto texLayout = roboslop::vertexLayoutPosNormalUv();
                auto texMesh = roboslop::makeStaticMesh(
                    std::as_bytes(std::span{kTexCubeVertices}),
                    std::span{kTexCubeIndices},
                    texLayout
                );
                texMesh.program = texProg->value;

                const bgfx::Memory* texelMem = bgfx::copy(
                    kCheckerPixels.data(), static_cast<std::uint32_t>(kCheckerPixels.size())
                );
                const bgfx::TextureHandle albedo = bgfx::createTexture2D(
                    /*width=*/4,
                    /*height=*/4,
                    /*hasMips=*/false,
                    /*numLayers=*/1,
                    bgfx::TextureFormat::RGBA8,
                    BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT,
                    texelMem
                );
                const bgfx::UniformHandle sAlbedo = assets.sampler("s_albedo");
                const roboslop::Material material{
                    .program = *texProg,
                    .albedo = albedo,
                    .sAlbedo = sAlbedo,
                };

                const auto te = world.create();
                world.emplace<roboslop::Transform>(
                    te, roboslop::Transform{.position = {3.0F, 5.0F, 0.0F}}
                );
                world.emplace<roboslop::Mesh>(te, texMesh);
                world.emplace<roboslop::Material>(te, material);
                world.emplace<roboslop::BodyDesc>(
                    te,
                    roboslop::BodyDesc{
                        .shape = roboslop::BoxShape{.halfExtents = {0.5F, 0.5F, 0.5F}},
                        .motion = roboslop::BodyMotion::Dynamic,
                        .mass = 1.0F,
                        .friction = 0.5F,
                        .restitution = 0.2F,
                    }
                );

                // The robot: a textured cube with no physics body, moved
                // kinematically by gorden.agent.robot when the brain
                // accepts a moveTo.
                const auto robot = world.create();
                world.emplace<roboslop::Transform>(
                    robot, roboslop::Transform{.position = {2.0F, 0.0F, 4.0F}}
                );
                world.emplace<roboslop::Mesh>(robot, texMesh);
                world.emplace<roboslop::Material>(robot, material);
                world.emplace<gorden::Named>(
                    robot, gorden::Named{.name = initial.settings.robotName}
                );
                world.emplace<gorden::Robot>(robot);
                world.emplace<gorden::RobotMotion>(robot, gorden::RobotMotion{.speed = 2.5F});

                auto& ctx = world.registry().ctx();
                gorden::BrainConfig brainCfg;
                brainCfg.robotName = initial.settings.robotName;
                brainCfg.playerName = initial.settings.playerName;
                ctx.emplace<gorden::AgentBrain>(makeProvider(), brainCfg, robot, cameraEntity);
                ctx.emplace<RobotPanelState>();
                auto& st = ctx.emplace<SettingsState>(initial);

                if (auto* ui = roboslop::devUi(world); ui != nullptr) {
                    ui->registerWindow(
                        roboslop::DevWindow{
                            .id = "robot",
                            .title = "Robot",
                            .draw =
                                [&world]() {
                                    auto& c = world.registry().ctx();
                                    drawRobotPanel(
                                        world, c.get<gorden::AgentBrain>(), c.get<RobotPanelState>()
                                    );
                                },
                            .visible = true,
                        }
                    );
                    ui->registerWindow(
                        roboslop::DevWindow{
                            .id = "settings",
                            .title = "Settings",
                            .draw =
                                [&world]() {
                                    auto& c = world.registry().ctx();
                                    drawSettingsPanel(
                                        world, c.get<SettingsState>(), c.get<gorden::AgentBrain>()
                                    );
                                },
                            .visible = false,
                        }
                    );
                    std::vector<roboslop::WindowVisibility> saved;
                    for (const auto& [id, visible] : st.settings.windows) {
                        saved.push_back({.id = id, .visible = visible});
                    }
                    ui->applyVisibility(saved);
                    for (const auto& v : ui->visibility()) {
                        st.lastVisibility[v.id] = v.visible;
                    }
                }

                // One directional light shading the textured cube. The
                // basic-shader entities (vertex-coloured) ignore lighting
                // entirely, so the M3 lighting only affects the M2 cube.
                const auto le = world.create();
                world.emplace<roboslop::DirectionalLight>(
                    le,
                    roboslop::DirectionalLight{
                        .direction = {-0.3F, -1.0F, -0.2F},
                        .color = {1.0F, 0.95F, 0.85F},
                        .intensity = 1.2F,
                    }
                );

                // Stash light uniform handles on the world so the pass
                // record callback can find them each frame without
                // capturing app-locals.
                world.registry().ctx().emplace<roboslop::LightUniforms>(roboslop::LightUniforms{
                    .dir = assets.uniform("u_lightDir", bgfx::UniformType::Vec4),
                    .color = assets.uniform("u_lightColor", bgfx::UniformType::Vec4),
                });
                return {};
            },
            .onBuildGraphs =
                [](roboslop::SystemGraph& fixed,
                   roboslop::RenderGraph& render,
                   roboslop::FrameArena& arena) {
                    fixed.add({
                        .name = "freeFlyCameras",
                        .reads = {"input"},
                        .writes = {"transforms"},
                        .run = [](roboslop::SystemCtx& c) {
                            auto& st = c.world->registry().ctx().get<RobotPanelState>();
                            // Same gating as shaderlab: a fly starts only
                            // when the press lands outside the UI.
                            if (c.input->mouseButtonPressed(roboslop::MouseButton::Right)) {
                                st.cameraActive = !st.uiWantsMouse;
                            }
                            if (!st.cameraActive) {
                                return;
                            }
                            roboslop::updateFreeFlyCameras(*c.world, *c.input, c.dt);
                            if (c.input->mouseButtonReleased(roboslop::MouseButton::Right)) {
                                st.cameraActive = false;
                            }
                        },
                    });
                    fixed.add({
                        .name = "robotLocomotion",
                        .reads = {},
                        .writes = {"transforms"},
                        .run = [](roboslop::SystemCtx& c) { gorden::robotLocomotion(c); },
                    });
                    fixed.add({
                        .name = "agentPump",
                        .reads = {"transforms"},
                        .writes = {"agent"},
                        .run = [](roboslop::SystemCtx& c) {
                            c.world->registry().ctx().get<gorden::AgentBrain>().pump(*c.world);
                        },
                    });
                    roboslop::registerPhysicsSystems(fixed);
                    roboslop::registerAudioSystems(fixed);
                    render.add({
                        .name = "main",
                        .reads = {"transforms", "lights"},
                        .writes = {"framebuffer"},
                        .record = [&arena](roboslop::PassCtx& c) {
                            roboslop::applyActiveCamera(
                                *c.world, c.viewId, c.viewportW, c.viewportH
                            );
                            const auto& lu =
                                c.world->registry().ctx().get<roboslop::LightUniforms>();
                            roboslop::uploadDirectionalLight(*c.world, lu.dir, lu.color);
                            auto draws = roboslop::collectMeshDraws(*c.world, arena, c.viewId);
                            roboslop::sortDraws(draws);
                            roboslop::submitDraws(draws);
                        },
                    });
                    render.add({
                        .name = "robotChat",
                        .reads = {"framebuffer"},
                        .writes = {"framebuffer"},
                        .record = [](roboslop::PassCtx& c) {
                            auto* ui = roboslop::devUi(*c.world);
                            if (ui == nullptr) {
                                return;
                            }
                            auto& ctx = c.world->registry().ctx();
                            auto& st = ctx.get<RobotPanelState>();
                            ui->beginFrame();
                            ui->drawWindows();
                            st.uiWantsMouse = ui->wantCaptureMouse();
                            ui->endFrame(c.viewId);

                            // Persist window visibility when it changes
                            // (menu or close button; F1 does not count).
                            auto& settings = ctx.get<SettingsState>();
                            std::map<std::string, bool> now;
                            for (const auto& v : ui->visibility()) {
                                now[v.id] = v.visible;
                            }
                            if (now != settings.lastVisibility) {
                                settings.lastVisibility = now;
                                settings.settings.windows = now;
                                saveSettingsNow(settings);
                            }
                        },
                    });
                },
        }
    );

    if (!app) {
        std::println(stderr, "app init failed: {}", app.error().message);
        return 1;
    }

    const auto result = app->run();
    if (!result) {
        std::println(
            stderr, "app exited with error: {} ({})", result.error().message, result.error().context
        );
        return 1;
    }
    return 0;
}
