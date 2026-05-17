import roboslop.app;
import roboslop.core.error;
import roboslop.ecs;
import roboslop.platform.window;
import roboslop.render.context;
import roboslop.render.mesh;
import roboslop.render.shader;

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <print>
#include <span>
#include <utility>

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
    // Program outlives App's run loop — captured by reference in onSetup
    // so the bgfx program handle survives until App's destructor runs
    // bgfx::shutdown().
    roboslop::Program program;
    const std::filesystem::path assetRoot = "assets";

    auto app = roboslop::App::make(
        roboslop::AppConfig{
            .window = roboslop::WindowConfig{.title = "gorden", .width = 1280, .height = 720},
            .tickRateHz = 60.0,
            .assetRoot = assetRoot,
            .onSetup = [&program, assetRoot](roboslop::World& world) -> roboslop::Result<void> {
                auto loaded = roboslop::loadProgram(assetRoot, "vs_basic", "fs_basic");
                if (!loaded) {
                    return std::unexpected(loaded.error());
                }
                program = std::move(*loaded);

                const auto layout = roboslop::vertexLayoutPosColor();
                auto mesh = roboslop::makeStaticMesh(
                    std::as_bytes(std::span{kTriangleVertices}), std::span{kTriangleIndices}, layout
                );
                mesh.program = program.handle();

                const auto e = world.create();
                world.emplace<roboslop::Mesh>(e, mesh);
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
