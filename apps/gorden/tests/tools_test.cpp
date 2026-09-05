import gorden.agent.observation;
import gorden.agent.tools;
import roboslop.llm;

#include <catch2/catch_test_macros.hpp>
#include <glm/vec3.hpp>

#include <string>
#include <variant>

namespace {

auto observationWith(glm::vec3 robotPos, std::string visibleName) -> gorden::Observation {
    gorden::Observation obs;
    obs.robotPosition = robotPos;
    obs.nearby.push_back(
        {.name = std::move(visibleName), .position = {3.0F, 0.0F, 0.0F}, .distance = 3.0F}
    );
    return obs;
}

} // namespace

TEST_CASE("toolSpecs exposes the three first-slice tools", "[agent][tools]") {
    const auto specs = gorden::toolSpecs();
    REQUIRE(specs.size() == 3);
    REQUIRE(specs[0].name == "moveTo");
    REQUIRE(specs[1].name == "inspect");
    REQUIRE(specs[2].name == "say");
}

TEST_CASE("parseToolCall turns model JSON into commands", "[agent][tools]") {
    const auto move = gorden::parseToolCall(
        {.id = "1", .name = "moveTo", .argumentsJson = R"({"x": 2.5, "z": -1})"}
    );
    REQUIRE(move.has_value());
    REQUIRE(std::holds_alternative<gorden::MoveTo>(*move));
    REQUIRE(std::get<gorden::MoveTo>(*move).target.x == 2.5F);
    REQUIRE(std::get<gorden::MoveTo>(*move).target.z == -1.0F);

    const auto inspect = gorden::parseToolCall(
        {.id = "2", .name = "inspect", .argumentsJson = R"({"name": "generator"})"}
    );
    REQUIRE(inspect.has_value());
    REQUIRE(std::get<gorden::Inspect>(*inspect).name == "generator");

    const auto say =
        gorden::parseToolCall({.id = "3", .name = "say", .argumentsJson = R"({"text": "hi"})"});
    REQUIRE(say.has_value());
    REQUIRE(std::get<gorden::Say>(*say).text == "hi");
}

TEST_CASE("parseToolCall rejects unknown tools and bad arguments", "[agent][tools]") {
    const auto unknown = gorden::parseToolCall({.id = "1", .name = "fly", .argumentsJson = "{}"});
    REQUIRE_FALSE(unknown.has_value());
    REQUIRE(unknown.error().code == static_cast<int>(gorden::ToolError::UnknownTool));

    const auto bad =
        gorden::parseToolCall({.id = "1", .name = "moveTo", .argumentsJson = R"({"x": "far"})"});
    REQUIRE_FALSE(bad.has_value());
    REQUIRE(bad.error().code == static_cast<int>(gorden::ToolError::BadArguments));

    const auto notJson = gorden::parseToolCall({.id = "1", .name = "say", .argumentsJson = "text"});
    REQUIRE_FALSE(notJson.has_value());
    REQUIRE(notJson.error().code == static_cast<int>(gorden::ToolError::BadArguments));
}

TEST_CASE("validate enforces move distance and world bounds", "[agent][tools]") {
    const auto obs = observationWith({0.0F, 0.0F, 0.0F}, "crate-1");
    const gorden::Rules rules{.maxMoveDistance = 5.0F, .worldHalfExtent = 10.0F};

    REQUIRE(gorden::validate(gorden::MoveTo{.target = {3.0F, 0.0F, 4.0F}}, obs, rules).has_value());

    const auto far = gorden::validate(gorden::MoveTo{.target = {6.0F, 0.0F, 0.0F}}, obs, rules);
    REQUIRE_FALSE(far.has_value());
    REQUIRE(far.error().code == static_cast<int>(gorden::ToolError::TooFar));

    const gorden::Rules generous{.maxMoveDistance = 100.0F, .worldHalfExtent = 10.0F};
    const auto outside =
        gorden::validate(gorden::MoveTo{.target = {0.0F, 0.0F, 11.0F}}, obs, generous);
    REQUIRE_FALSE(outside.has_value());
    REQUIRE(outside.error().code == static_cast<int>(gorden::ToolError::OutOfWorld));
}

TEST_CASE("validate only allows inspecting visible entities", "[agent][tools]") {
    const auto obs = observationWith({0.0F, 0.0F, 0.0F}, "crate-1");
    REQUIRE(gorden::validate(gorden::Inspect{.name = "crate-1"}, obs, {}).has_value());
    const auto unseen = gorden::validate(gorden::Inspect{.name = "generator"}, obs, {});
    REQUIRE_FALSE(unseen.has_value());
    REQUIRE(unseen.error().code == static_cast<int>(gorden::ToolError::UnknownEntity));
}

TEST_CASE("validate bounds say text", "[agent][tools]") {
    const auto obs = observationWith({0.0F, 0.0F, 0.0F}, "crate-1");
    const gorden::Rules rules{.maxSayLength = 5};
    REQUIRE(gorden::validate(gorden::Say{.text = "hello"}, obs, rules).has_value());
    REQUIRE(
        gorden::validate(gorden::Say{.text = ""}, obs, rules).error().code ==
        static_cast<int>(gorden::ToolError::EmptyText)
    );
    REQUIRE(
        gorden::validate(gorden::Say{.text = "hello!"}, obs, rules).error().code ==
        static_cast<int>(gorden::ToolError::TextTooLong)
    );
}

TEST_CASE("describeCommand renders one line per command", "[agent][tools]") {
    REQUIRE(
        gorden::describeCommand(gorden::MoveTo{.target = {1.0F, 0.0F, 2.0F}}) == "moveTo(1.0, 2.0)"
    );
    REQUIRE(gorden::describeCommand(gorden::Inspect{.name = "x"}) == "inspect(x)");
    REQUIRE(gorden::describeCommand(gorden::Say{.text = "hi"}) == "say(\"hi\")");
    REQUIRE(gorden::commandName(gorden::Say{.text = "hi"}) == "say");
}
