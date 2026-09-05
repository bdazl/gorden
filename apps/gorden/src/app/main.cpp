import roboslop.app;
import roboslop.audio;
import roboslop.core.error;
import roboslop.ecs;
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

#include <bgfx/bgfx.h>
#include <glm/vec3.hpp>

#include <array>
#include <cstdint>
#include <expected>
#include <print>
#include <span>

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
    auto app = roboslop::App::make(
        roboslop::AppConfig{
            .window = roboslop::WindowConfig{.title = "gorden", .width = 1280, .height = 720},
            .tickRateHz = 60.0,
            .assetRoot = "assets",
            .onSetup = [](roboslop::World& world,
                          roboslop::AssetCache& assets) -> roboslop::Result<void> {
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

                const auto layout = roboslop::vertexLayoutPosColor();
                auto cube = roboslop::makeStaticMesh(
                    std::as_bytes(std::span{kCubeVertices}), std::span{kCubeIndices}, layout
                );
                cube.program = prog->value;

                spawnGround(world, cube);
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
                            roboslop::updateFreeFlyCameras(*c.world, *c.input, c.dt);
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
