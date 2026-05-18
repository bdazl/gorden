import roboslop.render.graph;

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

TEST_CASE("derivePassInDegrees handles independent passes", "[render][graph]") {
    std::vector<roboslop::PassDesc> ps;
    ps.push_back({.name = "a", .reads = {}, .writes = {"x"}, .record = nullptr});
    ps.push_back({.name = "b", .reads = {}, .writes = {"y"}, .record = nullptr});

    const auto in = roboslop::derivePassInDegrees(ps);
    REQUIRE(in == std::vector<std::size_t>{0, 0});
}

TEST_CASE("derivePassInDegrees edges on write→read", "[render][graph]") {
    std::vector<roboslop::PassDesc> ps;
    ps.push_back({.name = "writer", .reads = {}, .writes = {"shadowMap"}, .record = nullptr});
    ps.push_back({.name = "reader", .reads = {"shadowMap"}, .writes = {}, .record = nullptr});

    const auto in = roboslop::derivePassInDegrees(ps);
    REQUIRE(in == std::vector<std::size_t>{0, 1});
}

TEST_CASE("RenderGraph executeStub visits passes in add-order", "[render][graph]") {
    std::vector<std::uint16_t> seenIds;
    std::vector<std::string> seenNames;

    roboslop::RenderGraph g;
    g.add({
        .name = "shadow",
        .reads = {},
        .writes = {"shadowMap"},
        .record = [&](roboslop::PassCtx& c) {
            seenIds.push_back(c.viewId);
            seenNames.emplace_back("shadow");
        },
    });
    g.add({
        .name = "main",
        .reads = {"shadowMap"},
        .writes = {"framebuffer"},
        .record = [&](roboslop::PassCtx& c) {
            seenIds.push_back(c.viewId);
            seenNames.emplace_back("main");
        },
    });
    g.add({
        .name = "post",
        .reads = {"framebuffer"},
        .writes = {},
        .record = [&](roboslop::PassCtx& c) {
            seenIds.push_back(c.viewId);
            seenNames.emplace_back("post");
        },
    });

    g.executeStub(1280, 720);

    REQUIRE(seenIds == std::vector<std::uint16_t>{0, 1, 2});
    REQUIRE(seenNames == std::vector<std::string>{"shadow", "main", "post"});
}

TEST_CASE("RenderGraph executeStub passes viewport size through", "[render][graph]") {
    int w = -1;
    int h = -1;

    roboslop::RenderGraph g;
    g.add({
        .name = "p",
        .reads = {},
        .writes = {},
        .record = [&](roboslop::PassCtx& c) {
            w = c.viewportW;
            h = c.viewportH;
        },
    });
    g.executeStub(800, 600);

    REQUIRE(w == 800);
    REQUIRE(h == 600);
}

TEST_CASE("RenderGraph size reflects add count", "[render][graph]") {
    roboslop::RenderGraph g;
    REQUIRE(g.size() == 0);
    g.add({.name = "a", .reads = {}, .writes = {}, .record = nullptr});
    g.add({.name = "b", .reads = {}, .writes = {}, .record = nullptr});
    REQUIRE(g.size() == 2);
}

TEST_CASE("RenderGraph tolerates a null record callback", "[render][graph]") {
    roboslop::RenderGraph g;
    g.add({.name = "nop", .reads = {}, .writes = {}, .record = nullptr});
    // Should not crash or invoke a null std::function.
    g.executeStub(100, 100);
    REQUIRE(g.size() == 1);
}
