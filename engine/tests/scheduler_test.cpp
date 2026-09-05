import roboslop.ecs;
import roboslop.sched;

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstddef>
#include <vector>

TEST_CASE("deriveInDegrees handles independent systems", "[sched][graph]") {
    std::vector<roboslop::SystemDesc> descs;
    descs.push_back({.name = "a", .reads = {}, .writes = {"x"}, .run = [](auto&) {}});
    descs.push_back({.name = "b", .reads = {}, .writes = {"y"}, .run = [](auto&) {}});
    descs.push_back({.name = "c", .reads = {}, .writes = {"z"}, .run = [](auto&) {}});

    const auto in = roboslop::deriveInDegrees(descs);
    REQUIRE(in == std::vector<std::size_t>{0, 0, 0});
}

TEST_CASE("deriveInDegrees adds edge on write/read conflict", "[sched][graph]") {
    std::vector<roboslop::SystemDesc> descs;
    descs.push_back({.name = "writer", .reads = {}, .writes = {"x"}, .run = [](auto&) {}});
    descs.push_back({.name = "reader", .reads = {"x"}, .writes = {}, .run = [](auto&) {}});

    const auto in = roboslop::deriveInDegrees(descs);
    REQUIRE(in == std::vector<std::size_t>{0, 1});
}

TEST_CASE("deriveInDegrees adds edge on write/write conflict", "[sched][graph]") {
    std::vector<roboslop::SystemDesc> descs;
    descs.push_back({.name = "a", .reads = {}, .writes = {"x"}, .run = [](auto&) {}});
    descs.push_back({.name = "b", .reads = {}, .writes = {"x"}, .run = [](auto&) {}});

    const auto in = roboslop::deriveInDegrees(descs);
    REQUIRE(in == std::vector<std::size_t>{0, 1});
}

TEST_CASE("deriveInDegrees adds edge on read/write conflict", "[sched][graph]") {
    std::vector<roboslop::SystemDesc> descs;
    descs.push_back({.name = "reader", .reads = {"x"}, .writes = {}, .run = [](auto&) {}});
    descs.push_back({.name = "writer", .reads = {}, .writes = {"x"}, .run = [](auto&) {}});

    const auto in = roboslop::deriveInDegrees(descs);
    REQUIRE(in == std::vector<std::size_t>{0, 1});
}

TEST_CASE("Scheduler runs every system in a graph with no edges", "[sched][run]") {
    std::atomic<int> counter{0};

    roboslop::SystemGraph g;
    for (int i = 0; i < 32; ++i) {
        g.add({
            .name = "n",
            .reads = {},
            .writes = {},
            .run = [&counter](roboslop::SystemCtx&) { counter.fetch_add(1); },
        });
    }
    g.compile();

    roboslop::Scheduler sched{2};
    roboslop::World world;
    roboslop::SystemCtx ctx{.world = &world};
    sched.run(g, ctx);

    REQUIRE(counter.load() == 32);
}

TEST_CASE("Scheduler enforces write→read order", "[sched][run]") {
    std::atomic<int> tick{0};
    int writerOrder = -1;
    int readerOrder = -1;

    roboslop::SystemGraph g;
    g.add({
        .name = "writer",
        .reads = {},
        .writes = {"x"},
        .run = [&](roboslop::SystemCtx&) { writerOrder = tick.fetch_add(1); },
    });
    g.add({
        .name = "reader",
        .reads = {"x"},
        .writes = {},
        .run = [&](roboslop::SystemCtx&) { readerOrder = tick.fetch_add(1); },
    });
    g.compile();

    roboslop::Scheduler sched{4};
    roboslop::World world;
    roboslop::SystemCtx ctx{.world = &world};
    sched.run(g, ctx);

    REQUIRE(writerOrder == 0);
    REQUIRE(readerOrder == 1);
}

TEST_CASE("Scheduler enforces chain across three systems", "[sched][run]") {
    std::atomic<int> tick{0};
    int aOrder = -1;
    int bOrder = -1;
    int cOrder = -1;

    roboslop::SystemGraph g;
    g.add({
        .name = "a",
        .reads = {},
        .writes = {"x"},
        .run = [&](roboslop::SystemCtx&) { aOrder = tick.fetch_add(1); },
    });
    g.add({
        .name = "b",
        .reads = {"x"},
        .writes = {"y"},
        .run = [&](roboslop::SystemCtx&) { bOrder = tick.fetch_add(1); },
    });
    g.add({
        .name = "c",
        .reads = {"y"},
        .writes = {},
        .run = [&](roboslop::SystemCtx&) { cOrder = tick.fetch_add(1); },
    });
    g.compile();

    roboslop::Scheduler sched{4};
    roboslop::World world;
    roboslop::SystemCtx ctx{.world = &world};
    sched.run(g, ctx);

    REQUIRE(aOrder == 0);
    REQUIRE(bOrder == 1);
    REQUIRE(cOrder == 2);
}

TEST_CASE("Scheduler reuses the same graph across multiple runs", "[sched][run]") {
    std::atomic<int> calls{0};

    roboslop::SystemGraph g;
    g.add({
        .name = "n",
        .reads = {},
        .writes = {},
        .run = [&calls](roboslop::SystemCtx&) { calls.fetch_add(1); },
    });
    g.compile();

    roboslop::Scheduler sched{2};
    roboslop::World world;
    roboslop::SystemCtx ctx{.world = &world};

    for (int i = 0; i < 8; ++i) {
        sched.run(g, ctx);
    }
    REQUIRE(calls.load() == 8);
}

TEST_CASE("SystemCtx is observable inside system bodies", "[sched][run]") {
    double seenDt = 0.0;
    roboslop::FrameStage seenStage = roboslop::FrameStage::FixedUpdate;

    roboslop::SystemGraph g;
    g.add({
        .name = "n",
        .reads = {},
        .writes = {},
        .run = [&](roboslop::SystemCtx& c) {
            seenDt = c.dt;
            seenStage = c.stage;
        },
    });
    g.compile();

    roboslop::Scheduler sched{1};
    roboslop::World world;
    roboslop::SystemCtx ctx{
        .world = &world,
        .dt = 0.016666,
        .stage = roboslop::FrameStage::Render,
    };
    sched.run(g, ctx);

    REQUIRE(seenDt == 0.016666);
    REQUIRE(seenStage == roboslop::FrameStage::Render);
}

TEST_CASE("defaultWorkerCount is at least 1", "[sched]") {
    REQUIRE(roboslop::defaultWorkerCount() >= 1U);
    REQUIRE(roboslop::defaultWorkerCount() <= 8U);
}
