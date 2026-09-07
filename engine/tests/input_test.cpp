import roboslop.platform.input;

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/vec2.hpp>

#include <cstddef>

namespace {

constexpr float Eps = 1e-5F;

} // namespace

TEST_CASE("computeMouseDelta returns zero when prev pose is invalid", "[platform][input]") {
    const roboslop::InputSnapshot prev; // cursorPosValid = false
    roboslop::InputSnapshot curr;
    curr.cursorPos = {100.0, 50.0};
    curr.cursorPosValid = true;

    const glm::vec2 d = roboslop::computeMouseDelta(prev, curr);
    REQUIRE(d.x == Catch::Approx(0.0F).margin(Eps));
    REQUIRE(d.y == Catch::Approx(0.0F).margin(Eps));
}

TEST_CASE("computeMouseDelta returns zero when curr pose is invalid", "[platform][input]") {
    roboslop::InputSnapshot prev;
    prev.cursorPos = {1.0, 2.0};
    prev.cursorPosValid = true;
    const roboslop::InputSnapshot curr; // cursorPosValid = false

    const glm::vec2 d = roboslop::computeMouseDelta(prev, curr);
    REQUIRE(d.x == Catch::Approx(0.0F).margin(Eps));
    REQUIRE(d.y == Catch::Approx(0.0F).margin(Eps));
}

TEST_CASE("computeMouseDelta returns curr - prev when both valid", "[platform][input]") {
    roboslop::InputSnapshot prev;
    prev.cursorPos = {100.0, 200.0};
    prev.cursorPosValid = true;
    roboslop::InputSnapshot curr;
    curr.cursorPos = {130.0, 180.0};
    curr.cursorPosValid = true;

    const glm::vec2 d = roboslop::computeMouseDelta(prev, curr);
    REQUIRE(d.x == Catch::Approx(30.0F).margin(Eps));
    REQUIRE(d.y == Catch::Approx(-20.0F).margin(Eps));
}

TEST_CASE("keyPressedEdge fires on false->true transition only", "[platform][input]") {
    roboslop::InputSnapshot prev;
    roboslop::InputSnapshot curr;
    const auto i = static_cast<std::size_t>(roboslop::Key::W);

    SECTION("rising edge") {
        curr.keys[i] = true;
        REQUIRE(roboslop::keyPressedEdge(prev, curr, roboslop::Key::W));
    }
    SECTION("held key does not fire") {
        prev.keys[i] = true;
        curr.keys[i] = true;
        REQUIRE_FALSE(roboslop::keyPressedEdge(prev, curr, roboslop::Key::W));
    }
    SECTION("released key does not fire") {
        prev.keys[i] = true;
        curr.keys[i] = false;
        REQUIRE_FALSE(roboslop::keyPressedEdge(prev, curr, roboslop::Key::W));
    }
}

TEST_CASE("keyReleasedEdge fires on true->false transition only", "[platform][input]") {
    roboslop::InputSnapshot prev;
    roboslop::InputSnapshot curr;
    const auto i = static_cast<std::size_t>(roboslop::Key::Escape);

    SECTION("falling edge") {
        prev.keys[i] = true;
        REQUIRE(roboslop::keyReleasedEdge(prev, curr, roboslop::Key::Escape));
    }
    SECTION("held does not fire") {
        prev.keys[i] = true;
        curr.keys[i] = true;
        REQUIRE_FALSE(roboslop::keyReleasedEdge(prev, curr, roboslop::Key::Escape));
    }
    SECTION("idle does not fire") {
        REQUIRE_FALSE(roboslop::keyReleasedEdge(prev, curr, roboslop::Key::Escape));
    }
}

TEST_CASE("mouseButtonPressedEdge fires only on rising edge", "[platform][input]") {
    roboslop::InputSnapshot prev;
    roboslop::InputSnapshot curr;
    const auto i = static_cast<std::size_t>(roboslop::MouseButton::Right);

    curr.mouseButtons[i] = true;
    REQUIRE(roboslop::mouseButtonPressedEdge(prev, curr, roboslop::MouseButton::Right));

    prev.mouseButtons[i] = true;
    REQUIRE_FALSE(roboslop::mouseButtonPressedEdge(prev, curr, roboslop::MouseButton::Right));
}

TEST_CASE("Input drives edges and delta from fed snapshots", "[platform][input]") {
    roboslop::Input input;

    roboslop::InputSnapshot first;
    first.keys[static_cast<std::size_t>(roboslop::Key::W)] = true;
    first.cursorPos = {10.0, 10.0};
    first.cursorPosValid = true;
    input.beginFrame(first);

    REQUIRE(input.keyDown(roboslop::Key::W));
    REQUIRE(input.keyPressed(roboslop::Key::W));

    roboslop::InputSnapshot second = first;
    second.cursorPos = {12.0, 7.0};
    input.beginFrame(second);

    // Held, not re-pressed; the delta is the difference between the two.
    REQUIRE(input.keyDown(roboslop::Key::W));
    REQUIRE_FALSE(input.keyPressed(roboslop::Key::W));
    REQUIRE(input.mouseDelta().x == Catch::Approx(2.0F).margin(Eps));
    REQUIRE(input.mouseDelta().y == Catch::Approx(-3.0F).margin(Eps));

    roboslop::InputSnapshot third = second;
    third.keys[static_cast<std::size_t>(roboslop::Key::W)] = false;
    input.beginFrame(third);

    REQUIRE(input.keyReleased(roboslop::Key::W));
}

TEST_CASE("Input reports a cursor-capture request before it is applied", "[platform][input]") {
    roboslop::Input input;

    REQUIRE_FALSE(input.cursorCaptured());

    // setCursorCaptured runs on scheduler workers and must not touch the
    // window; the request is readable immediately all the same, so a
    // system can express capture as a level within one tick.
    input.setCursorCaptured(true);

    REQUIRE(input.cursorCaptured());

    input.setCursorCaptured(false);

    REQUIRE_FALSE(input.cursorCaptured());
}
