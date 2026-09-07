module;

#include <nlohmann/json.hpp>

#include <cstdint>
#include <exception>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

export module roboslop.llm.openai_wire;

import roboslop.core.error;
import roboslop.llm;

namespace roboslop {

// Pure translation between the backend-neutral ChatRequest/ChatResponse
// and the OpenAI chat-completions JSON that OpenAI, llama.cpp server,
// and Ollama all accept. No I/O here so it is unit-testable.

export [[nodiscard]] auto buildChatCompletionBody(const ChatRequest& req) -> std::string {
    using Json = nlohmann::json;
    Json body;
    body["model"] = req.model;
    body["temperature"] = req.temperature;
    body["max_tokens"] = req.maxTokens;

    Json messages = Json::array();
    for (const auto& m : req.messages) {
        Json jm;
        jm["role"] = roleName(m.role);
        jm["content"] = m.content;
        if (m.role == Role::Tool) {
            jm["tool_call_id"] = m.toolCallId;
        }
        if (m.role == Role::Assistant && !m.toolCalls.empty()) {
            Json calls = Json::array();
            for (const auto& c : m.toolCalls) {
                calls.push_back({
                    {"id", c.id},
                    {"type", "function"},
                    {"function", {{"name", c.name}, {"arguments", c.argumentsJson}}},
                });
            }
            jm["tool_calls"] = std::move(calls);
            if (m.content.empty()) {
                jm["content"] = nullptr;
            }
        }
        messages.push_back(std::move(jm));
    }
    body["messages"] = std::move(messages);

    if (!req.tools.empty()) {
        Json tools = Json::array();
        for (const auto& t : req.tools) {
            Json params = Json::parse(t.parametersSchemaJson, nullptr, /*allow_exceptions=*/false);
            if (params.is_discarded()) {
                params = Json::object({{"type", "object"}, {"properties", Json::object()}});
            }
            tools.push_back({
                {"type", "function"},
                {"function",
                 {{"name", t.name}, {"description", t.description}, {"parameters", params}}},
            });
        }
        body["tools"] = std::move(tools);
        body["tool_choice"] = "auto";
    }
    return body.dump();
}

// Parses a chat-completions response. The {"error": {...}} envelope
// (returned with 4xx/5xx, but also seen with 200 from some local
// servers) maps to LlmError::ApiError; anything structurally
// unexpected maps to BadResponse with the offending text as context.
export [[nodiscard]] auto parseChatCompletion(std::string_view text) -> Result<ChatResponse> {
    using Json = nlohmann::json;
    const Json doc = Json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object()) {
        return std::unexpected(toError(LlmError::BadResponse, std::string{text.substr(0, 256)}));
    }
    if (const auto err = doc.find("error"); err != doc.end()) {
        std::string msg;
        if (err->is_object() && err->contains("message") && (*err)["message"].is_string()) {
            msg = (*err)["message"].get<std::string>();
        } else {
            msg = err->dump();
        }
        return std::unexpected(toError(LlmError::ApiError, std::move(msg)));
    }

    const auto choices = doc.find("choices");
    if (choices == doc.end() || !choices->is_array() || choices->empty()) {
        return std::unexpected(toError(LlmError::BadResponse, "no choices"));
    }
    const Json& choice = (*choices)[0];
    const auto message = choice.find("message");
    if (message == choice.end() || !message->is_object()) {
        return std::unexpected(toError(LlmError::BadResponse, "choice without message"));
    }

    ChatResponse out;
    if (const auto content = message->find("content");
        content != message->end() && content->is_string()) {
        out.content = content->get<std::string>();
    }
    if (const auto calls = message->find("tool_calls");
        calls != message->end() && calls->is_array()) {
        for (const Json& c : *calls) {
            ToolCall call;
            if (c.contains("id") && c["id"].is_string()) {
                call.id = c["id"].get<std::string>();
            }
            const auto fn = c.find("function");
            if (fn == c.end() || !fn->is_object()) {
                return std::unexpected(
                    toError(LlmError::BadResponse, "tool_call without function")
                );
            }
            if (fn->contains("name") && (*fn)["name"].is_string()) {
                call.name = (*fn)["name"].get<std::string>();
            }
            if (fn->contains("arguments")) {
                const Json& args = (*fn)["arguments"];
                // OpenAI sends arguments as a JSON string; some local
                // servers send an object. Normalise to text.
                call.argumentsJson = args.is_string() ? args.get<std::string>() : args.dump();
            }
            out.toolCalls.push_back(std::move(call));
        }
    }
    if (const auto fr = choice.find("finish_reason"); fr != choice.end() && fr->is_string()) {
        out.finishReason = fr->get<std::string>();
    }
    if (const auto usage = doc.find("usage"); usage != doc.end() && usage->is_object()) {
        if (usage->contains("prompt_tokens") && (*usage)["prompt_tokens"].is_number_unsigned()) {
            out.promptTokens = (*usage)["prompt_tokens"].get<std::uint32_t>();
        }
        if (usage->contains("completion_tokens") &&
            (*usage)["completion_tokens"].is_number_unsigned()) {
            out.completionTokens = (*usage)["completion_tokens"].get<std::uint32_t>();
        }
    }
    return out;
}

} // namespace roboslop
