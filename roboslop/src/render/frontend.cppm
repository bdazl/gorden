module;

#include <bgfx/bgfx.h>
#include <entt/entt.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat4x4.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <span>

export module roboslop.render.frontend;

import roboslop.ecs;
import roboslop.render.mesh;
import roboslop.scene.transform;

namespace roboslop {

// Bump-pointer arena reset once per frame. One heap allocation in the
// constructor, zero allocations afterwards. Lives on App and feeds the
// renderer's per-frame draw lists, skinning palettes, and other transient
// CPU buffers.
//
// Concurrency: not thread-safe. Per-frame allocations must come from one
// thread (the App's render thread in the current design); systems that
// want to fan out can each take their own sub-span and partition work
// over it without touching the arena cursor.
export class FrameArena {
  public:
    explicit FrameArena(std::size_t capacityBytes)
        : buf(std::make_unique<std::byte[]>(capacityBytes)), cap(capacityBytes), cursor(0) {}

    FrameArena(const FrameArena&) = delete;
    auto operator=(const FrameArena&) -> FrameArena& = delete;
    FrameArena(FrameArena&&) noexcept = default;
    auto operator=(FrameArena&&) noexcept -> FrameArena& = default;
    ~FrameArena() = default;

    auto reset() noexcept -> void {
        cursor = 0;
    }

    [[nodiscard]] auto capacity() const noexcept -> std::size_t {
        return cap;
    }

    [[nodiscard]] auto used() const noexcept -> std::size_t {
        return cursor;
    }

    // Returns an uninitialised span<T> of n elements aligned for T.
    // Aligns the absolute address (not just the offset) because the
    // backing buffer is only `__STDCPP_DEFAULT_NEW_ALIGNMENT__`-aligned
    // and consumers may request over-aligned types (cache-line padded
    // structures, SIMD payloads). Capacity exhaustion is an engine
    // bug, not a runtime input error; the assert here matches asan's
    // behaviour in debug builds and beats silent corruption in release.
    template <typename T>
    [[nodiscard]] auto allocate(std::size_t n) -> std::span<T> {
        const std::size_t bytes = n * sizeof(T);
        const auto base = reinterpret_cast<std::uintptr_t>(buf.get());
        const auto raw = base + cursor;
        const auto aligned = (raw + alignof(T) - 1U) & ~(alignof(T) - 1U);
        const std::size_t newCursor = (aligned - base) + bytes;
        assert(newCursor <= cap && "FrameArena: capacity exhausted");
        cursor = newCursor;
        // T's constructor isn't invoked here; consumers either fill
        // the span by assignment (POD types) or use placement-new on
        // the first byte of each slot.
        return std::span<T>{reinterpret_cast<T*>(aligned), n};
    }

  private:
    std::unique_ptr<std::byte[]> buf;
    std::size_t cap;
    std::size_t cursor;
};

// Flat, sortable draw record. Built once per frame by the renderer
// frontend, sorted by sortKey, then iterated by the backend. POD so it
// copies cheaply and lives in the FrameArena without heap indirection.
//
// `bones`/`boneCount` are reserved for skeletal animation (M5); they are
// nullptr/0 for static meshes. `albedo` is invalid until M2 wires
// textured materials. Adding fields later costs us a sort-key rebuild
// but no API churn.
export struct DrawItem {
    std::uint64_t sortKey = 0;
    bgfx::VertexBufferHandle vb{bgfx::kInvalidHandle};
    bgfx::IndexBufferHandle ib{bgfx::kInvalidHandle};
    bgfx::ProgramHandle program{bgfx::kInvalidHandle};
    std::uint64_t state = BGFX_STATE_DEFAULT;
    glm::mat4 model{1.0F};
    bgfx::TextureHandle albedo{bgfx::kInvalidHandle};
    const glm::mat4* bones = nullptr;
    std::uint8_t boneCount = 0;
    std::uint16_t viewId = 0;
};

// Sort key layout (high → low):
//   [viewClass:2][viewId:16][program:24][depth:22]
// viewClass is reserved (0 today) — used later to bucket opaque /
// transparent / depth-only into separate passes inside one view.
// depth is the front-to-back screen-space depth quantised to 22 bits;
// today's draws use 0 because we don't have a camera-depth pass yet.
export [[nodiscard]] auto makeSortKey(
    std::uint16_t viewId,
    std::uint32_t programIdx,
    std::uint32_t depth22 = 0,
    std::uint32_t viewClass = 0
) noexcept -> std::uint64_t {
    const std::uint64_t vc = static_cast<std::uint64_t>(viewClass & 0x3U) << 62U;
    const std::uint64_t vi = static_cast<std::uint64_t>(viewId) << 46U;
    const std::uint64_t pg = static_cast<std::uint64_t>(programIdx & 0xFFFFFFU) << 22U;
    const std::uint64_t dp = static_cast<std::uint64_t>(depth22 & 0x3FFFFFU);
    return vc | vi | pg | dp;
}

// Walks every entity with a Mesh + Transform and emits one DrawItem per
// entity into the arena. Returns the populated span. Frontend has full
// knowledge of which view this draw list targets; the viewId is baked
// into both sortKey and DrawItem.viewId.
//
// No allocations beyond the single arena reservation. The caller is
// responsible for sizing the arena to cover the worst-case entity count
// per frame.
export [[nodiscard]] auto
collectMeshDraws(const World& world, FrameArena& arena, std::uint16_t viewId)
    -> std::span<DrawItem> {
    const auto& reg = world.registry();
    auto view = reg.view<const Mesh, const Transform>();

    std::size_t count = 0;
    for (const auto e : view) {
        (void)e;
        ++count;
    }

    auto draws = arena.allocate<DrawItem>(count);
    std::size_t i = 0;
    for (const auto e : view) {
        const auto& m = reg.get<const Mesh>(e);
        const auto& t = reg.get<const Transform>(e);
        DrawItem& d = draws[i++];
        d.vb = m.vb;
        d.ib = m.ib;
        d.program = m.program;
        d.state = m.state;
        d.model = toMatrix(t);
        d.viewId = viewId;
        d.sortKey = makeSortKey(viewId, static_cast<std::uint32_t>(m.program.idx));
    }
    return draws;
}

// In-place sort. std::sort on a contiguous span is heap-free.
export auto sortDraws(std::span<DrawItem> items) -> void {
    std::sort(items.begin(), items.end(), [](const DrawItem& a, const DrawItem& b) noexcept {
        return a.sortKey < b.sortKey;
    });
}

// Iterates the sorted list and pushes each draw to bgfx. Must run on
// the bgfx API thread (the renderGraph executor). Skips items whose vb,
// ib, or program is invalid — common during the one frame between
// entity creation and asset readiness.
export auto submitDraws(std::span<const DrawItem> items) -> void {
    for (const auto& d : items) {
        if (!bgfx::isValid(d.vb) || !bgfx::isValid(d.ib) || !bgfx::isValid(d.program)) {
            continue;
        }
        bgfx::setTransform(glm::value_ptr(d.model));
        bgfx::setVertexBuffer(0, d.vb);
        bgfx::setIndexBuffer(d.ib);
        bgfx::setState(d.state);
        bgfx::submit(d.viewId, d.program);
    }
}

} // namespace roboslop
