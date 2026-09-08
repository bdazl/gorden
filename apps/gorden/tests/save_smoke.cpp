// Display-dependent integration test, deliberately separate from headless CTest:
// applySave rebuilds the scene through SceneRuntime, which needs a real
// render context. Run it from the build directory with a temporary output
// directory as its only argument.
import gorden.agent.brain;
import gorden.agent.memory;
import gorden.agent.observation;
import gorden.agent.robot;
import gorden.save;
import gorden.player;
import roboslop.physics;
import roboslop.app;
import roboslop.core.error;
import roboslop.ecs;
import roboslop.llm;
import roboslop.llm.backend;
import roboslop.render.asset_cache;
import roboslop.render.camera;
import roboslop.render.frontend;
import roboslop.render.graph;
import roboslop.render.lighting;
import roboslop.scene.document;
import roboslop.scene.runtime;
import roboslop.scene.savegame;
import roboslop.scene.transform;
import roboslop.sched;

#include <bgfx/bgfx.h>
#include <glm/vec3.hpp>

#include <filesystem>
#include <memory>
#include <print>
#include <string>
#include <vector>

namespace {

constexpr const char* ScenePath = "assets/scenes/room.json";
constexpr float MovedX = -7.5F;

} // namespace

auto main(int argc, char** argv) -> int {
    if (argc != 2) {
        std::println(stderr, "Usage: gorden_save_smoke <temporary-output-directory>");
        return 1;
    }
    const std::filesystem::path savePath = std::filesystem::path(argv[1]) / "smoke.json";
    auto scene = roboslop::loadScene(ScenePath);
    if (!scene) {
        std::println(stderr, "{}", scene.error().context);
        return 1;
    }

    int frame = 0;
    bool failed = false;
    std::string failure;
    const auto document = *scene;
    const auto probe = document.objects.front().id;
    roboslop::LightUniforms uniforms;
    auto app = roboslop::App::make(
        {.window = {.title = "gorden-save-smoke", .width = 800, .height = 600},
         .assetRoot = "assets",
         .onSetup = [&](roboslop::World& world,
                        roboslop::AssetCache& assets) -> roboslop::Result<void> {
             const auto camera = world.create();
             world.emplace<roboslop::Transform>(camera, document.camera);
             world.emplace<roboslop::Camera>(
                 camera, roboslop::Camera{.projection = roboslop::Perspective{}}
             );
             world.emplace<roboslop::ActiveCamera>(camera);
             const auto player = world.create();
             world.emplace<roboslop::Transform>(
                 player, roboslop::Transform{.position = {0.0F, 0.4F, 4.0F}}
             );
             world.emplace<gorden::Player>(player);
             world.emplace<gorden::Named>(player, gorden::Named{.name = "Player"});
             uniforms = {
                 .dir = assets.uniform("u_lightDir", bgfx::UniformType::Vec4),
                 .color = assets.uniform("u_lightColor", bgfx::UniformType::Vec4)
             };
             auto& runtime = world.registry().ctx().emplace<roboslop::SceneRuntime>();
             if (auto loaded = runtime.replace(world, assets, document, true); !loaded) {
                 return loaded;
             }
             world.forEach<roboslop::SceneIdentity>([&world](auto entity, const auto& identity) {
                 world.emplace<gorden::Named>(entity, gorden::Named{.name = identity.name});
             });

             const auto robot = world.create();
             world.emplace<roboslop::Transform>(
                 robot, roboslop::Transform{.position = {2.0F, 0.0F, 4.0F}}
             );
             world.emplace<gorden::Named>(robot, gorden::Named{.name = "Gorden"});
             world.emplace<gorden::Robot>(robot);
             world.emplace<gorden::RobotMotion>(robot, gorden::RobotMotion{.speed = 2.5F});

             auto& brain = world.registry().ctx().emplace<gorden::AgentBrain>(
                 std::make_unique<roboslop::ScriptedProvider>(
                     std::vector<roboslop::ChatResponse>{}
                 ),
                 gorden::BrainConfig{},
                 robot,
                 player
             );
             gorden::AgentMemory memory;
             memory.remember("the crate hides a key", 1, 10.0);
             memory.setGoal("open the crate", 11.0);
             brain.setMemory(std::move(memory), 12.0);
             return {};
         },
         .onBuildGraphs =
             [&](roboslop::SystemGraph& fixed,
                 roboslop::RenderGraph& render,
                 roboslop::FrameArena& arena) {
                 roboslop::registerPhysicsSystems(fixed);
                 fixed.add({
                     .name = "player",
                     .reads = {},
                     .writes = {"transforms", "physicsState"},
                     .run = [](roboslop::SystemCtx& c) {
                         auto& world = *c.world;
                         const auto entity =
                             world.registry().ctx().get<gorden::AgentBrain>().playerEntity();
                         gorden::movePlayer(
                             world.get<gorden::Player>(entity),
                             world.get<roboslop::Transform>(entity),
                             *world.registry().ctx().get<roboslop::JoltWorld*>(),
                             glm::vec3{0.0F},
                             static_cast<float>(c.dt)
                         );
                     },
                 });
                 render.add(
                     {.name = "smoke",
                      .reads = {},
                      .writes = {"framebuffer"},
                      .record = [&](roboslop::PassCtx& c) {
                          auto& world = *c.world;
                          auto& brain = world.registry().ctx().get<gorden::AgentBrain>();
                          auto& runtime = world.registry().ctx().get<roboslop::SceneRuntime>();
                          const auto fail = [&](std::string why) {
                              failed = true;
                              failure = std::move(why);
                          };

                          if (frame == 1) {
                              // Move the robot away from where the save
                              // will remember it, then write the save.
                              world.get<roboslop::Transform>(brain.robotEntity()).position.x =
                                  MovedX;
                              const auto save =
                                  gorden::captureSave(world, brain, "scenes/room.json");
                              if (!roboslop::saveSaveGame(savePath, save)) {
                                  fail("saveSaveGame failed");
                              }
                          } else if (frame == 2) {
                              // Now change the world and forget everything.
                              world.get<roboslop::Transform>(brain.robotEntity()).position.x = 0.0F;
                              brain.setMemory(gorden::AgentMemory{}, 0.0);
                              world.get<roboslop::Transform>(brain.playerEntity()).position.x =
                                  5.0F;
                              world.get<gorden::Player>(brain.playerEntity()).reset();
                          } else if (frame == 3) {
                              auto loaded = roboslop::loadSaveGame(savePath);
                              if (!loaded) {
                                  fail("loadSaveGame failed: " + loaded.error().context);
                              } else if (
                                  auto applied = gorden::applySave(
                                      world, *c.assets, runtime, document, brain, *loaded
                                  );
                                  !applied
                              ) {
                                  fail("applySave failed: " + applied.error().context);
                              }
                          } else if (frame == 4) {
                              if (world.get<roboslop::Transform>(brain.robotEntity()).position.x !=
                                  MovedX) {
                                  fail("the robot did not return to its saved position");
                              }
                              if (world.get<roboslop::Transform>(brain.playerEntity()).position.x !=
                                  0.0F) {
                                  fail(
                                      "the player controller did not return to its saved position"
                                  );
                              }
                              if (brain.memory().recall("crate key", 5).size() != 1 ||
                                  brain.memory().activeGoals().size() != 1 ||
                                  brain.simTime() != 12.0) {
                                  fail("the memory did not come back");
                              }
                              bool found = false;
                              world.forEach<roboslop::SceneIdentity, gorden::Named>(
                                  [&](const auto& identity, const auto& named) {
                                      found |= identity.id == probe && !named.name.empty();
                                  }
                              );
                              if (!found) {
                                  fail("the rebuilt scene lost its perception names");
                              }
                          }

                          roboslop::applyActiveCamera(world, c.viewId, c.viewportW, c.viewportH);
                          roboslop::uploadDirectionalLight(world, uniforms.dir, uniforms.color);
                          auto draws = roboslop::collectMeshDraws(world, arena, c.viewId);
                          roboslop::sortDraws(draws);
                          roboslop::submitDraws(draws);
                          if (++frame > 5 || failed) {
                              roboslop::requestAppClose(world);
                          }
                      }}
                 );
             }}
    );
    if (!app) {
        std::println(stderr, "{}", app.error().context);
        return 1;
    }
    const auto result = app->run();
    if (!result) {
        std::println(stderr, "{}", result.error().context);
        return 1;
    }
    if (failed) {
        std::println(stderr, "gorden save smoke failed: {}", failure);
        return 1;
    }
    std::println("gorden save smoke passed ({} frames)", frame);
    return 0;
}
