import gorden.agent.memory;

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <string>

TEST_CASE("Recall ranks episodes by matching words, newest first", "[agent][memory]") {
    gorden::AgentMemory memory;
    memory.remember("the player asked me to find the generator", 1, 1.0);
    memory.remember("a crate stood by the wall", 2, 2.0);
    memory.remember("the generator hums near the crate", 3, 3.0);

    const auto hits = memory.recall("generator crate", 10);
    REQUIRE(hits.size() == 3);
    REQUIRE(hits[0].text == "the generator hums near the crate"); // two words match
    REQUIRE(hits[1].text == "a crate stood by the wall");         // newer of the one-word hits
    REQUIRE(memory.recall("generator", 10).size() == 2);
    REQUIRE(memory.recall("generator crate", 1).size() == 1);
    REQUIRE(memory.recall("submarine", 10).empty());
}

TEST_CASE("Episodes and goals get distinct ids and episodes are capped", "[agent][memory]") {
    gorden::AgentMemory memory{gorden::MemoryLimits{.maxEpisodes = 2}};
    REQUIRE(memory.remember("first", 1, 1.0).id == "ep-1");
    const auto goal = memory.setGoal("find the generator", 1.0);
    REQUIRE(goal.id == "goal-2");
    memory.remember("second", 1, 2.0);
    memory.remember("third", 1, 3.0);
    REQUIRE(memory.episodes().size() == 2);
    REQUIRE(memory.episodes().front().text == "second");
}

TEST_CASE("A belief revises the value held for the same predicate", "[agent][memory]") {
    gorden::AgentMemory memory;
    memory.believe({.subject = "crate", .predicate = "location", .value = "by the wall"});
    memory.believe({
        .subject = "crate",
        .predicate = "location",
        .value = "by the door",
        .source = "player",
        .confidence = 0.9F,
    });
    memory.believe({.subject = "crate", .predicate = "colour", .value = "brown"});
    REQUIRE(memory.beliefs().size() == 2);
    REQUIRE(memory.beliefs()[0].value == "by the door");
    REQUIRE(memory.beliefs()[0].source == "player");
}

TEST_CASE("Goals close by id and only active ones are listed", "[agent][memory]") {
    gorden::AgentMemory memory;
    const auto first = memory.setGoal("find the generator", 1.0).id;
    memory.setGoal("greet the player", 2.0);
    REQUIRE(memory.activeGoals().size() == 2);
    REQUIRE(memory.closeGoal(first, gorden::GoalStatus::Done));
    REQUIRE_FALSE(memory.closeGoal("goal-404", gorden::GoalStatus::Done));
    REQUIRE(memory.activeGoals().size() == 1);
    REQUIRE(memory.activeGoals()[0].text == "greet the player");
    REQUIRE(memory.goals()[0].status == gorden::GoalStatus::Done);
}

TEST_CASE("Memory round trips through JSON including the id counter", "[agent][memory]") {
    gorden::AgentMemory memory;
    memory.remember("the generator hums", 4, 12.5);
    memory.believe({
        .subject = "generator",
        .predicate = "state",
        .value = "running",
        .source = "observed",
        .learnedAt = 12.5,
        .confidence = 0.8F,
    });
    memory.setGoal("switch it off", 13.0);
    REQUIRE(memory.closeGoal("goal-2", gorden::GoalStatus::Abandoned));

    const auto json = gorden::toJson(memory);
    auto decoded = gorden::memoryFromJson(json);
    REQUIRE(decoded);
    REQUIRE(gorden::toJson(*decoded) == json);
    REQUIRE(decoded->idCounter() == memory.idCounter());
    REQUIRE(decoded->remember("next", 5, 14.0).id == "ep-3");
    REQUIRE(decoded->goals()[0].status == gorden::GoalStatus::Abandoned);
}

TEST_CASE("Malformed memory JSON fails instead of throwing", "[agent][memory]") {
    auto json = gorden::toJson(gorden::AgentMemory{});
    json["version"] = 2;
    REQUIRE_FALSE(gorden::memoryFromJson(json));

    json = gorden::toJson(gorden::AgentMemory{});
    json["episodes"].push_back({{"id", "ep-1"}});
    const auto decoded = gorden::memoryFromJson(json);
    REQUIRE_FALSE(decoded);
    REQUIRE(decoded.error().category == "gorden.agent.memory");
}
