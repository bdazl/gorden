module;

#include <bgfx/bgfx.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

export module roboslop.render.graph;

import roboslop.ecs;
import roboslop.render.asset_cache;
import roboslop.render.context;

namespace roboslop {

// Per-pass context handed to a pass's record callback. Pointers (not
// references) so unit tests can drive RenderGraph::execute with stubs
// (nullptr world, render context, and asset cache); production code
// always passes non-null pointers. `assets` is the App-owned AssetCache,
// exposed so a pass can swap GPU resources on the render thread (shader
// hot reload) without stashing a pointer in the world context.
export struct PassCtx {
    std::uint16_t viewId = 0;
    int viewportW = 0;
    int viewportH = 0;
    World* world = nullptr;
    RenderContext* rc = nullptr;
    AssetCache* assets = nullptr;
};

// Declarative render pass. Resource ids are opaque strings (e.g.
// "drawItems", "shadowMap", "framebuffer"); RenderGraph derives a
// pass ordering from add-order conflict edges, identical to
// roboslop.sched's SystemGraph. Because edges flow forward in
// add-order, cycles are impossible by construction — add-order itself
// is a valid topological order and is what RenderGraph::execute walks.
export struct PassDesc {
    std::string_view name;
    std::vector<std::string_view> reads;
    std::vector<std::string_view> writes;
    std::function<void(PassCtx&)> record;
};

namespace detail {

[[nodiscard]] inline auto passesConflict(const PassDesc& a, const PassDesc& b) noexcept -> bool {
    auto any = [](std::span<const std::string_view> xs,
                  std::span<const std::string_view> ys) noexcept -> bool {
        for (const auto& x : xs) {
            for (const auto& y : ys) {
                if (x == y) {
                    return true;
                }
            }
        }
        return false;
    };
    return any(a.writes, b.writes) || any(a.writes, b.reads) || any(a.reads, b.writes);
}

} // namespace detail

// bgfx supports up to 256 views; RenderGraph fails-fast above that. The
// number is plenty for any MVP-chain milestone (M3 + M5 add at most a
// handful of passes).
export inline constexpr std::size_t MaxRenderPasses = 256;

// Logical pass-DAG over bgfx view-IDs. Passes are recorded
// sequentially in add-order on the bgfx API thread; parallel work
// inside a pass is the pass body's responsibility (via tf::Subflow
// inside a system, when the data is CPU-only).
export class RenderGraph {
  public:
    RenderGraph() = default;

    RenderGraph(const RenderGraph&) = delete;
    auto operator=(const RenderGraph&) -> RenderGraph& = delete;
    RenderGraph(RenderGraph&&) noexcept = default;
    auto operator=(RenderGraph&&) noexcept -> RenderGraph& = default;
    ~RenderGraph() = default;

    auto add(PassDesc desc) -> void {
        assert(passes.size() < MaxRenderPasses);
        passes.push_back(std::move(desc));
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t {
        return passes.size();
    }

    // Walks passes in add-order (the natural topological order under
    // forward-only edges), assigning dense view-IDs from 0. Caller
    // supplies the world, render context, and asset cache; viewport
    // size is read from the render context per execute().
    auto execute(World& world, RenderContext& rc, AssetCache& assets) -> void {
        const int w = rc.framebufferWidth();
        const int h = rc.framebufferHeight();
        for (std::size_t i = 0; i < passes.size(); ++i) {
            PassCtx ctx{
                .viewId = static_cast<std::uint16_t>(i),
                .viewportW = w,
                .viewportH = h,
                .world = &world,
                .rc = &rc,
                .assets = &assets,
            };
            bgfx::setViewRect(
                ctx.viewId, 0, 0, static_cast<std::uint16_t>(w), static_cast<std::uint16_t>(h)
            );
            if (passes[i].record) {
                passes[i].record(ctx);
            }
        }
    }

    // Test-only overload: lets unit tests drive execute() without a
    // live bgfx device. The bgfx::setViewRect call is skipped; record
    // callbacks run with the supplied viewport size and nullptr
    // world/rc/assets.
    auto executeStub(int viewportW, int viewportH) -> void {
        for (std::size_t i = 0; i < passes.size(); ++i) {
            PassCtx ctx{
                .viewId = static_cast<std::uint16_t>(i),
                .viewportW = viewportW,
                .viewportH = viewportH,
                .world = nullptr,
                .rc = nullptr,
                .assets = nullptr,
            };
            if (passes[i].record) {
                passes[i].record(ctx);
            }
        }
    }

  private:
    std::vector<PassDesc> passes;
};

// Pure helper for unit tests: in-degree per pass under add-order
// conflict edges. Mirrors RenderGraph's edge rule (writer↔reader,
// writer↔writer) without touching bgfx.
export [[nodiscard]] auto derivePassInDegrees(std::span<const PassDesc> passes)
    -> std::vector<std::size_t> {
    std::vector<std::size_t> in(passes.size(), 0);
    for (std::size_t i = 0; i < passes.size(); ++i) {
        for (std::size_t j = i + 1; j < passes.size(); ++j) {
            if (detail::passesConflict(passes[i], passes[j])) {
                ++in[j];
            }
        }
    }
    return in;
}

} // namespace roboslop
