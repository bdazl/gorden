import roboslop.app;
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
import roboslop.render.mesh;
import roboslop.scene.transform;
import roboslop.sched;

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
                    render.add({
                        .name = "main",
                        .reads = {"transforms"},
                        .writes = {"framebuffer"},
                        .record = [&arena](roboslop::PassCtx& c) {
                            roboslop::applyActiveCamera(
                                *c.world, c.viewId, c.viewportW, c.viewportH
                            );
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
