import roboslop.ecs;
import roboslop.render.asset_cache;
import roboslop.render.frontend;
import roboslop.render.material;
import roboslop.render.mesh;

#include <bgfx/bgfx.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("rebindProgram rewrites matching Mesh and Material programs", "[render][frontend]") {
    roboslop::World w;
    const bgfx::ProgramHandle oldProg{7};
    const bgfx::ProgramHandle newProg{9};
    const bgfx::ProgramHandle other{3};

    const auto a = w.create();
    w.emplace<roboslop::Mesh>(a, roboslop::Mesh{.program = oldProg});

    const auto b = w.create();
    w.emplace<roboslop::Mesh>(b, roboslop::Mesh{.program = other});
    w.emplace<roboslop::Material>(b, roboslop::Material{.program = {oldProg}});

    const auto c = w.create();
    w.emplace<roboslop::Mesh>(c, roboslop::Mesh{.program = oldProg});
    w.emplace<roboslop::Material>(c, roboslop::Material{.program = {other}});

    const auto n = roboslop::rebindProgram(w, oldProg, newProg);
    REQUIRE(n == 3);

    REQUIRE(w.get<roboslop::Mesh>(a).program.idx == newProg.idx);
    REQUIRE(w.get<roboslop::Mesh>(b).program.idx == other.idx);
    REQUIRE(w.get<roboslop::Material>(b).program.value.idx == newProg.idx);
    REQUIRE(w.get<roboslop::Mesh>(c).program.idx == newProg.idx);
    REQUIRE(w.get<roboslop::Material>(c).program.value.idx == other.idx);
}

TEST_CASE("rebindProgram is a no-op when nothing matches", "[render][frontend]") {
    roboslop::World w;
    const auto e = w.create();
    w.emplace<roboslop::Mesh>(e, roboslop::Mesh{.program = bgfx::ProgramHandle{1}});
    REQUIRE(roboslop::rebindProgram(w, bgfx::ProgramHandle{2}, bgfx::ProgramHandle{3}) == 0);
    REQUIRE(w.get<roboslop::Mesh>(e).program.idx == 1);
}
