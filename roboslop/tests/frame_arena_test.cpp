import roboslop.render.frontend;

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>

TEST_CASE("FrameArena reports its capacity and zero initial usage", "[render][arena]") {
    roboslop::FrameArena arena{1024};
    REQUIRE(arena.capacity() == 1024);
    REQUIRE(arena.used() == 0);
}

TEST_CASE("FrameArena allocate returns a span of the requested size", "[render][arena]") {
    roboslop::FrameArena arena{1024};
    auto s = arena.allocate<std::uint32_t>(8);
    REQUIRE(s.size() == 8);
    REQUIRE(arena.used() >= 8U * sizeof(std::uint32_t));
}

TEST_CASE("FrameArena allocations are aligned to alignof(T)", "[render][arena]") {
    roboslop::FrameArena arena{4096};
    auto bytes = arena.allocate<std::byte>(1);
    REQUIRE(bytes.size() == 1);

    struct alignas(64) AlignedThing {
        char data[8];
    };

    auto things = arena.allocate<AlignedThing>(1);
    const auto addr = reinterpret_cast<std::uintptr_t>(things.data());
    REQUIRE(addr % 64U == 0U);
}

TEST_CASE("FrameArena reset returns cursor to zero", "[render][arena]") {
    roboslop::FrameArena arena{1024};
    (void)arena.allocate<std::uint64_t>(16);
    REQUIRE(arena.used() > 0);
    arena.reset();
    REQUIRE(arena.used() == 0);

    // Same address after reset → arena reuses memory rather than
    // allocating new storage.
    auto first = arena.allocate<std::uint64_t>(4);
    arena.reset();
    auto second = arena.allocate<std::uint64_t>(4);
    REQUIRE(first.data() == second.data());
}

TEST_CASE("FrameArena packs successive allocations contiguously", "[render][arena]") {
    roboslop::FrameArena arena{4096};
    auto a = arena.allocate<std::uint32_t>(4);
    auto b = arena.allocate<std::uint32_t>(4);
    REQUIRE(b.data() == a.data() + 4);
}

TEST_CASE("makeSortKey orders by viewId first", "[render][sort]") {
    const auto a = roboslop::makeSortKey(/*viewId=*/0, /*program=*/100);
    const auto b = roboslop::makeSortKey(/*viewId=*/1, /*program=*/0);
    REQUIRE(a < b);
}

TEST_CASE("makeSortKey orders by program after viewId", "[render][sort]") {
    const auto a = roboslop::makeSortKey(/*viewId=*/3, /*program=*/2);
    const auto b = roboslop::makeSortKey(/*viewId=*/3, /*program=*/7);
    REQUIRE(a < b);
}
