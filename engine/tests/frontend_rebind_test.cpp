import roboslop.ecs;
import roboslop.render.asset_cache;
import roboslop.render.frontend;
import roboslop.render.material;
import roboslop.render.mesh;
import roboslop.render.model;
import roboslop.scene.transform;

#include <bgfx/bgfx.h>
#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/matrix_transform.hpp>

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

TEST_CASE("rebindProgram rewrites programs inside ModelInstance parts", "[render][frontend]") {
    roboslop::World w;
    const bgfx::ProgramHandle oldProg{7};
    const bgfx::ProgramHandle newProg{9};
    const bgfx::ProgramHandle other{3};

    const auto e = w.create();
    w.emplace<roboslop::ModelInstance>(
        e,
        roboslop::ModelInstance{
            .parts = {
                roboslop::ModelDrawPart{
                    .mesh = {.program = oldProg}, .material = {.program = {oldProg}}
                },
                roboslop::ModelDrawPart{
                    .mesh = {.program = other}, .material = {.program = {oldProg}}
                }
            }
        }
    );

    REQUIRE(roboslop::rebindProgram(w, oldProg, newProg) == 3);
    const auto& parts = w.get<roboslop::ModelInstance>(e).parts;
    REQUIRE(parts[0].mesh.program.idx == newProg.idx);
    REQUIRE(parts[0].material.program.value.idx == newProg.idx);
    REQUIRE(parts[1].mesh.program.idx == other.idx);
    REQUIRE(parts[1].material.program.value.idx == newProg.idx);
}

TEST_CASE("collectMeshDraws emits one draw per mesh and per model part", "[render][frontend]") {
    roboslop::World w;
    roboslop::FrameArena arena{64 * 1024};

    const auto a = w.create();
    w.emplace<roboslop::Mesh>(a, roboslop::Mesh{.program = bgfx::ProgramHandle{1}});
    w.emplace<roboslop::Transform>(a, roboslop::Transform{.position = {1, 0, 0}});

    const auto b = w.create();
    w.emplace<roboslop::Transform>(b, roboslop::Transform{.position = {0, 2, 0}});
    w.emplace<roboslop::ModelInstance>(
        b,
        roboslop::ModelInstance{
            .parts = {
                roboslop::ModelDrawPart{
                    .mesh = {.program = bgfx::ProgramHandle{2}},
                    .material = {.program = {bgfx::ProgramHandle{5}}},
                    .local = glm::translate(glm::mat4{1.0F}, glm::vec3{0, 0, 3})
                },
                roboslop::ModelDrawPart{
                    .mesh = {.program = bgfx::ProgramHandle{2}}, .material = {}, .local = {}
                }
            }
        }
    );

    const auto draws = roboslop::collectMeshDraws(w, arena, 4);
    REQUIRE(draws.size() == 3);
    REQUIRE(draws[0].program.idx == 1);
    REQUIRE(draws[0].model[3][0] == 1.0F);
    // Material wins over the part mesh's program; local composes after the entity.
    REQUIRE(draws[1].program.idx == 5);
    REQUIRE(draws[1].model[3][1] == 2.0F);
    REQUIRE(draws[1].model[3][2] == 3.0F);
    REQUIRE(draws[2].program.idx == 2);
    REQUIRE(draws[2].model[3][2] == 0.0F);
    REQUIRE(draws[2].viewId == 4);
}
