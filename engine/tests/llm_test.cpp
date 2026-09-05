import roboslop.llm;
import roboslop.llm.backend;

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>
#include <vector>

TEST_CASE("ScriptedProvider replays its script then falls back", "[llm][scripted]") {
    roboslop::ScriptedProvider provider(
        {roboslop::ChatResponse{.content = "first"}, roboslop::ChatResponse{.content = "second"}},
        roboslop::ChatResponse{.content = "done"}
    );
    const roboslop::ChatRequest req{
        .model = "x", .messages = {{.role = roboslop::Role::User, .content = "hi"}}
    };

    REQUIRE(provider.complete(req)->content == "first");
    REQUIRE(provider.complete(req)->content == "second");
    REQUIRE(provider.remaining() == 0);
    REQUIRE(provider.complete(req)->content == "done");
    REQUIRE(provider.recordedRequests().size() == 3);
    REQUIRE(provider.recordedRequests()[0].messages[0].content == "hi");
}

TEST_CASE("startCompletion delivers the provider's answer asynchronously", "[llm][async]") {
    roboslop::ScriptedProvider provider({roboslop::ChatResponse{
        .toolCalls = {{.id = "c1", .name = "say", .argumentsJson = "{\"text\":\"yo\"}"}},
        .finishReason = "tool_calls",
    }});
    auto handle = roboslop::startCompletion(
        provider, roboslop::ChatRequest{.model = "x", .messages = {{.content = "go"}}}
    );
    REQUIRE(handle.active());

    for (int i = 0; i < 500 && !handle.ready(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    REQUIRE(handle.ready());
    auto result = handle.take();
    REQUIRE(result.has_value());
    REQUIRE(result->toolCalls.size() == 1);
    REQUIRE(result->toolCalls[0].name == "say");
    REQUIRE_FALSE(handle.active());
}

TEST_CASE("roleName covers every role", "[llm]") {
    REQUIRE(roboslop::roleName(roboslop::Role::System) == "system");
    REQUIRE(roboslop::roleName(roboslop::Role::User) == "user");
    REQUIRE(roboslop::roleName(roboslop::Role::Assistant) == "assistant");
    REQUIRE(roboslop::roleName(roboslop::Role::Tool) == "tool");
}
