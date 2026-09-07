import roboslop.time.clock;

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

TEST_CASE("FixedTimestep with half-tick frame consumes zero steps", "[time]") {
    roboslop::FixedTimestep ts{60.0};

    REQUIRE(ts.advance(1.0 / 120.0) == 0);
    REQUIRE(ts.alpha() == Approx(0.5));
}

TEST_CASE("FixedTimestep with double-tick frame consumes two steps", "[time]") {
    roboslop::FixedTimestep ts{60.0};

    REQUIRE(ts.advance(1.0 / 30.0) == 2);
    REQUIRE(ts.alpha() == Approx(0.0).margin(1e-9));
}

TEST_CASE("FixedTimestep clamps very long frames to MaxFrameTime", "[time]") {
    roboslop::FixedTimestep ts{60.0};

    // 10 seconds of wall-clock time would unbounded-ly schedule 600 steps;
    // the clamp caps it at MaxFrameTime (0.25 s) * 60 Hz = 15 steps.
    REQUIRE(ts.advance(10.0) == 15);
}

TEST_CASE("FixedTimestep alpha stays within unit interval across many short frames", "[time]") {
    roboslop::FixedTimestep ts{60.0};

    for (int i = 0; i < 1000; ++i) {
        (void)ts.advance(1.0 / 240.0);
        REQUIRE(ts.alpha() >= 0.0);
        REQUIRE(ts.alpha() < 1.0);
    }
}

TEST_CASE("FixedTimestep::fixedDelta matches configured rate", "[time]") {
    const roboslop::FixedTimestep ts{120.0};

    REQUIRE(ts.fixedDelta() == Approx(1.0 / 120.0));
}

TEST_CASE("Clock::tickFrame returns non-negative delta", "[time]") {
    roboslop::Clock c;

    const double dt = c.tickFrame();
    REQUIRE(dt >= 0.0);
}
