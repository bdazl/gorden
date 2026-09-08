import gorden.player;
import roboslop.ecs;
import roboslop.physics;
import roboslop.physics.components;
import roboslop.platform.input;
import roboslop.scene.transform;
import roboslop.sched;

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <numbers>

namespace {
constexpr float Dt = 1.0F / 60.0F;

struct Room {
    roboslop::JoltWorld physics = roboslop::JoltWorld::make();
    roboslop::World world;
    gorden::Player player;
    roboslop::Transform transform{.position = {0.0F, 2.0F, 0.0F}};

    Room() {
        roboslop::installJoltWorld(world, physics);
        box({0.0F, -0.5F, 0.0F}, {10.0F, 0.5F, 10.0F});
    }

    auto box(glm::vec3 position, glm::vec3 halfExtents) -> void {
        const auto e = world.create();
        world.emplace<roboslop::Transform>(e, roboslop::Transform{.position = position});
        world.emplace<roboslop::BodyDesc>(
            e,
            roboslop::BodyDesc{
                .shape = roboslop::BoxShape{.halfExtents = halfExtents},
                .motion = roboslop::BodyMotion::Static
            }
        );
        roboslop::SystemCtx context{.world = &world, .dt = static_cast<double>(Dt)};
        roboslop::physicsSpawn(context);
    }

    auto tick(glm::vec3 velocity = glm::vec3{0.0F}, int count = 1) -> void {
        for (int i = 0; i < count; ++i) {
            physics.step(Dt);
            gorden::movePlayer(player, transform, physics, velocity, Dt);
        }
    }
};
} // namespace

TEST_CASE("Player movement is horizontal, camera-relative and analog", "[player]") {
    roboslop::Input input;
    roboslop::InputSnapshot snapshot;
    snapshot.keys[static_cast<std::size_t>(roboslop::Key::W)] = true;
    snapshot.keys[static_cast<std::size_t>(roboslop::Key::D)] = true;
    input.beginFrame(snapshot);
    gorden::PlayerControls controls;
    auto command = gorden::readPlayerInput(input, controls);
    REQUIRE(glm::length(command.move) == Catch::Approx(1.0F));
    const gorden::OrbitCamera orbit{.yaw = std::numbers::pi_v<float> / 2.0F, .pitch = -1.0F};
    const auto forward = gorden::playerVelocity(orbit, {0.0F, 1.0F});
    REQUIRE(forward.x == Catch::Approx(-4.0F));
    REQUIRE(forward.y == 0.0F);
    REQUIRE(forward.z == Catch::Approx(0.0F).margin(1e-5F));
    snapshot.keys.fill(false);
    snapshot.gamepad = {.connected = true, .leftStick = {0.6F, 0.0F}};
    input.beginFrame(snapshot);
    command = gorden::readPlayerInput(input, controls);
    REQUIRE(command.move.x == Catch::Approx(0.5F));
    snapshot.gamepad = {};
    input.beginFrame(snapshot);
    REQUIRE(glm::length(gorden::readPlayerInput(input, controls).move) == 0.0F);
}

TEST_CASE("UI focus and cancellation release gameplay input", "[player][input]") {
    roboslop::Input input;
    roboslop::InputSnapshot snapshot;
    snapshot.keys[static_cast<std::size_t>(roboslop::Key::W)] = true;
    snapshot.mouseButtons[1] = true;
    snapshot.gamepad = {.connected = true, .leftStick = {1.0F, 0.0F}, .rightStick = {1.0F, 0.0F}};
    input.beginFrame(snapshot);
    gorden::PlayerControls controls;
    (void)gorden::readPlayerInput(input, controls);
    REQUIRE(input.cursorCaptured());
    SECTION("keyboard UI") {
        controls.uiKeyboard = true;
    }
    SECTION("window unfocused") {
        snapshot.focused = false;
    }
    SECTION("controller cancel") {
        snapshot.gamepad.buttonB = true;
    }
    SECTION("escape") {
        snapshot.keys[static_cast<std::size_t>(roboslop::Key::Escape)] = true;
    }
    input.beginFrame(snapshot);
    const auto command = gorden::readPlayerInput(input, controls);
    REQUIRE(glm::length(command.move) == 0.0F);
    REQUIRE(glm::length(command.stickLook) == 0.0F);
    REQUIRE_FALSE(input.cursorCaptured());
    controls.uiKeyboard = false;
    snapshot.focused = true;
    snapshot.gamepad.buttonB = false;
    snapshot.keys[static_cast<std::size_t>(roboslop::Key::Escape)] = false;
    input.beginFrame(snapshot);
    (void)gorden::readPlayerInput(input, controls);
    REQUIRE_FALSE(input.cursorCaptured()); // must release the cancelled hold
}

TEST_CASE("Mouse UI drag cannot become camera capture mid-hold", "[player][input]") {
    roboslop::Input input;
    roboslop::InputSnapshot snapshot;
    snapshot.mouseButtons[1] = true;
    input.beginFrame(snapshot);
    gorden::PlayerControls controls{.uiMouse = true};
    (void)gorden::readPlayerInput(input, controls);
    controls.uiMouse = false;
    (void)gorden::readPlayerInput(input, controls);
    REQUIRE_FALSE(input.cursorCaptured());
    snapshot.mouseButtons[1] = false;
    input.beginFrame(snapshot);
    (void)gorden::readPlayerInput(input, controls);
    snapshot.mouseButtons[1] = true;
    input.beginFrame(snapshot);
    (void)gorden::readPlayerInput(input, controls);
    REQUIRE(input.cursorCaptured());
}

TEST_CASE("Orbit pitch is bounded and stick look is independent of tick rate", "[player][camera]") {
    gorden::OrbitCamera a;
    auto b = a;
    const gorden::PlayerInput input{.stickLook = {0.3F, 0.1F}};
    for (int i = 0; i < 60; ++i) {
        gorden::turnCamera(a, input, 1.0F / 60.0F);
    }
    for (int i = 0; i < 120; ++i) {
        gorden::turnCamera(b, input, 1.0F / 120.0F);
    }
    REQUIRE(a.yaw == Catch::Approx(b.yaw));
    REQUIRE(a.pitch == Catch::Approx(b.pitch));
    gorden::turnCamera(a, {.mouseLook = {1e6F, 1e6F}}, Dt);
    REQUIRE(a.pitch == Catch::Approx(-1.2F));
    REQUIRE(std::abs(a.yaw) <= std::numbers::pi_v<float>);
}

TEST_CASE("Character falls onto the floor and rests without drift", "[player][physics]") {
    Room room;
    room.tick({}, 180);
    REQUIRE(room.player.grounded);
    REQUIRE(room.transform.position.y == Catch::Approx(0.92F).margin(0.03F));
    const auto resting = room.transform.position;
    room.tick({}, 180);
    REQUIRE(glm::length(room.transform.position - resting) < 0.001F);
}

TEST_CASE("Character stops at walls and slides tangentially", "[player][physics]") {
    Room room;
    room.box({2.0F, 2.0F, 0.0F}, {0.1F, 2.0F, 10.0F});
    room.tick({}, 120);
    room.tick({4.0F, 0.0F, 0.0F}, 120);
    REQUIRE(room.transform.position.x < 1.56F);
    REQUIRE(room.transform.position.x > 1.4F);
    const float previousZ = room.transform.position.z;
    room.tick({2.8F, 0.0F, -2.8F}, 60);
    REQUIRE(room.transform.position.x < 1.56F);
    REQUIRE(room.transform.position.z < previousZ - 2.0F);
    REQUIRE(room.player.grounded);
}

TEST_CASE(
    "Character steps onto a low obstacle and reset discards old motion", "[player][physics]"
) {
    Room room;
    room.box({2.0F, 0.1F, 0.0F}, {1.0F, 0.1F, 2.0F});
    room.tick({}, 120);
    room.tick({2.0F, 0.0F, 0.0F}, 60);
    REQUIRE(room.transform.position.x > 1.8F);
    REQUIRE(room.transform.position.y > 1.05F);
    room.transform.position = {-3.0F, 1.0F, 0.0F};
    room.player.reset();
    room.tick({}, 120);
    REQUIRE(room.transform.position.x == Catch::Approx(-3.0F));
    REQUIRE(room.player.grounded);
}

TEST_CASE(
    "Camera follows the avatar and retracts before crossing a wall", "[player][camera][physics]"
) {
    Room room;
    room.tick({}, 120);
    const gorden::OrbitCamera orbit{.pitch = 0.0F};
    roboslop::Transform camera;
    gorden::followPlayer(orbit, room.transform, camera, room.physics);
    REQUIRE(camera.position.z == Catch::Approx(4.0F));
    room.box({0.0F, 2.0F, 2.0F}, {5.0F, 2.0F, 0.1F});
    gorden::followPlayer(orbit, room.transform, camera, room.physics);
    REQUIRE(camera.position.z < 1.71F);
    REQUIRE(camera.position.z > 1.5F);
    REQUIRE(camera.position.y > room.transform.position.y);
}
