import roboslop.app;
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
import roboslop.scene.transform;
import roboslop.sched;

#include <array>
#include <cstdint>
#include <expected>
#include <print>
#include <span>

namespace {

struct TriangleVertex {
    float x;
    float y;
    float z;
    std::uint32_t abgr;
};

constexpr std::array<TriangleVertex, 3> kTriangleVertices = {
    TriangleVertex{0.0F, 0.5F, 0.0F, 0xFF0000FFU},
    TriangleVertex{-0.5F, -0.5F, 0.0F, 0xFF00FF00U},
    TriangleVertex{0.5F, -0.5F, 0.0F, 0xFFFF0000U},
};

constexpr std::array<std::uint16_t, 3> kTriangleIndices = {0, 1, 2};

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
                    cameraEntity, roboslop::Transform{.position = {0.0F, 0.0F, 3.0F}}
                );
                world.emplace<roboslop::Camera>(
                    cameraEntity, roboslop::Camera{.projection = roboslop::Perspective{}}
                );
                world.emplace<roboslop::ActiveCamera>(cameraEntity);
                world.emplace<roboslop::FreeFlyCamera>(cameraEntity);

                const auto layout = roboslop::vertexLayoutPosColor();
                auto mesh = roboslop::makeStaticMesh(
                    std::as_bytes(std::span{kTriangleVertices}), std::span{kTriangleIndices}, layout
                );
                mesh.program = prog->value;

                const auto e = world.create();
                world.emplace<roboslop::Mesh>(e, mesh);
                world.emplace<roboslop::Transform>(e);
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
