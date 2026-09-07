import roboslop.llm;
import roboslop.llm.openai_wire;

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <string>

TEST_CASE("buildChatCompletionBody emits messages, tools, and settings", "[llm][openai]") {
    const roboslop::ChatRequest req{
        .model = "gpt-4.1-mini",
        .messages =
            {
                {.role = roboslop::Role::System, .content = "You are a robot."},
                {.role = roboslop::Role::User, .content = "go"},
                {.role = roboslop::Role::Assistant,
                 .content = "",
                 .toolCalls =
                     {{.id = "c1", .name = "moveTo", .argumentsJson = R"({"x":1,"z":2})"}}},
                {.role = roboslop::Role::Tool, .content = "accepted", .toolCallId = "c1"},
            },
        .tools =
            {{.name = "moveTo",
              .description = "Walk to a point",
              .parametersSchemaJson = R"({"type":"object","properties":{"x":{"type":"number"}}})"}},
        .temperature = 0.5,
        .maxTokens = 128,
    };
    const auto body = nlohmann::json::parse(roboslop::buildChatCompletionBody(req));

    REQUIRE(body["model"] == "gpt-4.1-mini");
    REQUIRE(body["temperature"] == 0.5);
    REQUIRE(body["max_tokens"] == 128);
    REQUIRE(body["messages"].size() == 4);
    REQUIRE(body["messages"][0]["role"] == "system");
    REQUIRE(body["messages"][2]["role"] == "assistant");
    REQUIRE(body["messages"][2]["content"].is_null());
    REQUIRE(body["messages"][2]["tool_calls"][0]["function"]["name"] == "moveTo");
    REQUIRE(body["messages"][2]["tool_calls"][0]["function"]["arguments"] == "{\"x\":1,\"z\":2}");
    REQUIRE(body["messages"][3]["role"] == "tool");
    REQUIRE(body["messages"][3]["tool_call_id"] == "c1");
    REQUIRE(body["tools"].size() == 1);
    REQUIRE(body["tools"][0]["type"] == "function");
    REQUIRE(body["tools"][0]["function"]["parameters"]["properties"]["x"]["type"] == "number");
    REQUIRE(body["tool_choice"] == "auto");
}

TEST_CASE("buildChatCompletionBody omits tools when none are given", "[llm][openai]") {
    const roboslop::ChatRequest req{.model = "m", .messages = {{.content = "hi"}}};
    const auto body = nlohmann::json::parse(roboslop::buildChatCompletionBody(req));
    REQUIRE_FALSE(body.contains("tools"));
    REQUIRE_FALSE(body.contains("tool_choice"));
}

TEST_CASE("parseChatCompletion reads a plain content reply", "[llm][openai]") {
    const std::string text = R"({
        "choices": [{"message": {"role": "assistant", "content": "Hello!"}, "finish_reason": "stop"}],
        "usage": {"prompt_tokens": 12, "completion_tokens": 3}
    })";
    const auto r = roboslop::parseChatCompletion(text);
    REQUIRE(r.has_value());
    REQUIRE(r->content == "Hello!");
    REQUIRE(r->toolCalls.empty());
    REQUIRE(r->finishReason == "stop");
    REQUIRE(r->promptTokens == 12);
    REQUIRE(r->completionTokens == 3);
}

TEST_CASE("parseChatCompletion reads tool calls with string or object arguments", "[llm][openai]") {
    const std::string text = R"({
        "choices": [{"message": {"role": "assistant", "content": null, "tool_calls": [
            {"id": "call_1", "type": "function",
             "function": {"name": "moveTo", "arguments": "{\"x\": 1.5, \"z\": -2}"}},
            {"id": "call_2", "type": "function",
             "function": {"name": "say", "arguments": {"text": "hi"}}}
        ]}, "finish_reason": "tool_calls"}]
    })";
    const auto r = roboslop::parseChatCompletion(text);
    REQUIRE(r.has_value());
    REQUIRE(r->content.empty());
    REQUIRE(r->toolCalls.size() == 2);
    REQUIRE(r->toolCalls[0].id == "call_1");
    REQUIRE(r->toolCalls[0].name == "moveTo");
    REQUIRE(nlohmann::json::parse(r->toolCalls[0].argumentsJson)["z"] == -2);
    REQUIRE(nlohmann::json::parse(r->toolCalls[1].argumentsJson)["text"] == "hi");
    REQUIRE(r->finishReason == "tool_calls");
}

TEST_CASE("parseChatCompletion maps the error envelope to ApiError", "[llm][openai]") {
    const auto r = roboslop::parseChatCompletion(
        R"({"error": {"message": "Incorrect API key provided", "type": "invalid_request_error"}})"
    );
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == static_cast<int>(roboslop::LlmError::ApiError));
    REQUIRE(r.error().context == "Incorrect API key provided");
}

TEST_CASE("parseChatCompletion rejects malformed bodies as BadResponse", "[llm][openai]") {
    const auto notJson = roboslop::parseChatCompletion("<html>502</html>");
    REQUIRE_FALSE(notJson.has_value());
    REQUIRE(notJson.error().code == static_cast<int>(roboslop::LlmError::BadResponse));

    const auto noChoices = roboslop::parseChatCompletion(R"({"id": "x"})");
    REQUIRE_FALSE(noChoices.has_value());
    REQUIRE(noChoices.error().code == static_cast<int>(roboslop::LlmError::BadResponse));
}
