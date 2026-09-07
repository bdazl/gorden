module;

#include <chrono>
#include <cstdint>
#include <expected>
#include <future>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module roboslop.llm;

import roboslop.core.error;

namespace roboslop {

export enum class LlmError : int {
    NoBackend = 1,
    TransportFailed = 2,
    ApiError = 3,
    BadResponse = 4,
};

export [[nodiscard]] auto toError(LlmError e, std::string ctx = {}) -> Error {
    switch (e) {
    case LlmError::NoBackend:
        return {
            .category = "roboslop.llm",
            .code = static_cast<int>(e),
            .message = "no LLM backend configured",
            .context = std::move(ctx)
        };
    case LlmError::TransportFailed:
        return {
            .category = "roboslop.llm",
            .code = static_cast<int>(e),
            .message = "LLM request failed to reach the server",
            .context = std::move(ctx)
        };
    case LlmError::ApiError:
        return {
            .category = "roboslop.llm",
            .code = static_cast<int>(e),
            .message = "LLM server returned an error",
            .context = std::move(ctx)
        };
    case LlmError::BadResponse:
        return {
            .category = "roboslop.llm",
            .code = static_cast<int>(e),
            .message = "LLM response could not be parsed",
            .context = std::move(ctx)
        };
    }
    return {
        .category = "roboslop.llm",
        .code = 0,
        .message = "unknown LlmError",
        .context = std::move(ctx)
    };
}

// Chat-completion vocabulary, backend-neutral. The shapes follow the
// OpenAI chat API because every runtime we care about (OpenAI,
// llama.cpp server, Ollama) speaks it; nothing here depends on JSON.
export enum class Role : int {
    System,
    User,
    Assistant,
    Tool,
};

export [[nodiscard]] constexpr auto roleName(Role r) noexcept -> std::string_view {
    switch (r) {
    case Role::System:
        return "system";
    case Role::User:
        return "user";
    case Role::Assistant:
        return "assistant";
    case Role::Tool:
        return "tool";
    }
    return "user";
}

// A tool the model may call. `parametersSchemaJson` is a JSON-schema
// object as text; the wire layer embeds it verbatim.
export struct ToolSpec {
    std::string name{};
    std::string description{};
    std::string parametersSchemaJson = R"({"type":"object","properties":{}})";
};

// A call the model proposed. Arguments stay as JSON text: parsing and
// validating them is the application's job (the "proposal until
// validated" boundary in docs/architecture.md).
export struct ToolCall {
    std::string id{};
    std::string name{};
    std::string argumentsJson{};
};

export struct ChatMessage {
    Role role = Role::User;
    std::string content{};
    // Assistant messages that proposed tool calls carry them so the
    // conversation can be replayed to the model with the Tool results.
    std::vector<ToolCall> toolCalls{};
    // Tool-role messages: which call this is the result of.
    std::string toolCallId{};
};

export struct ChatRequest {
    std::string model{};
    std::vector<ChatMessage> messages{};
    std::vector<ToolSpec> tools{};
    double temperature = 0.2;
    int maxTokens = 512;
};

export struct ChatResponse {
    std::string content{};
    std::vector<ToolCall> toolCalls{};
    std::string finishReason{};
    std::uint32_t promptTokens = 0;
    std::uint32_t completionTokens = 0;
};

// One inference backend. complete() blocks for the whole round trip and
// is meant to be driven through startCompletion() on a worker thread.
// Implementations must be safe to call from a thread other than the
// one that constructed them (no bgfx, no world access).
export class Provider {
  public:
    Provider() = default;
    Provider(const Provider&) = delete;
    auto operator=(const Provider&) -> Provider& = delete;
    Provider(Provider&&) = delete;
    auto operator=(Provider&&) -> Provider& = delete;
    virtual ~Provider() = default;

    [[nodiscard]] virtual auto complete(const ChatRequest& request) -> Result<ChatResponse> = 0;
    [[nodiscard]] virtual auto name() const noexcept -> std::string_view = 0;
};

// Handle to an in-flight completion. Poll ready() from the frame loop,
// take() once it is ready. Default-constructed / taken handles report
// !active().
export class AsyncCompletion {
  public:
    AsyncCompletion() = default;

    explicit AsyncCompletion(std::future<Result<ChatResponse>> f) noexcept : fut(std::move(f)) {}

    [[nodiscard]] auto active() const noexcept -> bool {
        return fut.valid();
    }

    [[nodiscard]] auto ready() const -> bool {
        return fut.valid() && fut.wait_for(std::chrono::seconds{0}) == std::future_status::ready;
    }

    // Precondition: ready(). Consumes the handle.
    [[nodiscard]] auto take() -> Result<ChatResponse> {
        return fut.get();
    }

  private:
    std::future<Result<ChatResponse>> fut;
};

// Runs provider.complete(request) on a worker thread. The provider must
// outlive the returned handle; the request is copied into the task.
export [[nodiscard]] auto startCompletion(Provider& provider, ChatRequest request)
    -> AsyncCompletion {
    return AsyncCompletion{std::async(std::launch::async, [&provider, r = std::move(request)]() {
        return provider.complete(r);
    })};
}

} // namespace roboslop
