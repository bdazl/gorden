import roboslop.app;
import roboslop.core.error;
import roboslop.ecs;
import roboslop.platform.window;
import roboslop.render.asset_cache;
import roboslop.render.context;
import roboslop.render.mesh;
import roboslop.scene.transform;

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
            .onSetup = [](roboslop::World& world, roboslop::AssetCache& assets)
                -> roboslop::Result<void> {
                auto prog = assets.program("vs_basic", "fs_basic");
                if (!prog) {
                    return std::unexpected(prog.error());
                }

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
            .onFixedUpdate = {},
            .onRender = [](roboslop::World& world,
                           roboslop::RenderContext& /*ctx*/,
                           double /*alpha*/) { roboslop::submitMeshes(world); },
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
