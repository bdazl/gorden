import roboslop.ecs;

#include <catch2/catch_test_macros.hpp>

namespace {

struct Position {
    float x = 0.0F;
    float y = 0.0F;
};

struct Velocity {
    float dx = 0.0F;
    float dy = 0.0F;
};

} // namespace

TEST_CASE("World::create returns a valid entity", "[ecs]") {
    roboslop::World w;
    const auto e = w.create();
    REQUIRE(w.valid(e));
}

TEST_CASE("World::destroy invalidates the entity", "[ecs]") {
    roboslop::World w;
    const auto e = w.create();
    w.destroy(e);
    REQUIRE_FALSE(w.valid(e));
}

TEST_CASE("World::emplace + get round-trips component data", "[ecs]") {
    roboslop::World w;
    const auto e = w.create();
    w.emplace<Position>(e, 1.0F, 2.0F);

    const auto& p = w.get<Position>(e);
    REQUIRE(p.x == 1.0F);
    REQUIRE(p.y == 2.0F);
}

TEST_CASE("World::has flips with emplace / remove", "[ecs]") {
    roboslop::World w;
    const auto e = w.create();

    REQUIRE_FALSE(w.has<Position>(e));
    w.emplace<Position>(e);
    REQUIRE(w.has<Position>(e));
    (void)w.remove<Position>(e);
    REQUIRE_FALSE(w.has<Position>(e));
}

TEST_CASE("World::tryGet returns nullptr for missing component", "[ecs]") {
    roboslop::World w;
    const auto e = w.create();
    REQUIRE(w.tryGet<Position>(e) == nullptr);
    w.emplace<Position>(e, 5.0F, 6.0F);
    REQUIRE(w.tryGet<Position>(e) != nullptr);
}

TEST_CASE("World::forEach visits only matching entities", "[ecs]") {
    roboslop::World w;
    const auto e1 = w.create();
    const auto e2 = w.create();
    const auto e3 = w.create();

    w.emplace<Position>(e1);
    w.emplace<Position>(e2);
    w.emplace<Velocity>(e2);
    w.emplace<Velocity>(e3);

    int posCount = 0;
    w.forEach<Position>([&](auto, auto&) { ++posCount; });
    REQUIRE(posCount == 2);

    int posVelCount = 0;
    w.forEach<Position, Velocity>([&](auto, auto&, auto&) { ++posVelCount; });
    REQUIRE(posVelCount == 1);
}

TEST_CASE("World::forEach passes component refs that mutate underlying state", "[ecs]") {
    roboslop::World w;
    const auto e = w.create();
    w.emplace<Position>(e, 1.0F, 2.0F);

    w.forEach<Position>([](auto, auto& p) {
        p.x += 10.0F;
        p.y += 20.0F;
    });

    const auto& p = w.get<Position>(e);
    REQUIRE(p.x == 11.0F);
    REQUIRE(p.y == 22.0F);
}
