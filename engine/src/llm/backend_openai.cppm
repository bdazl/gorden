module;

#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <utility>

export module roboslop.llm.backend:openai;

import roboslop.core.error;
import roboslop.llm;
import roboslop.llm.openai_wire;
import roboslop.platform.http;

namespace roboslop {

// Any server that speaks the OpenAI chat-completions API: OpenAI itself
// (the default base URL), a llama.cpp server (`http://127.0.0.1:8080/v1`,
// empty key), or Ollama's compatibility endpoint. The API key is held in
// memory only and never logged; callers read it from the environment.
export struct OpenAiConfig {
    std::string baseUrl = "https://api.openai.com/v1";
    std::string apiKey{};
    std::string model = "gpt-4.1-mini";
    long timeoutSeconds = 60;
};

export class OpenAiProvider final : public Provider {
  public:
    explicit OpenAiProvider(OpenAiConfig cfg) : cfg(std::move(cfg)) {}

    [[nodiscard]] auto complete(const ChatRequest& request) -> Result<ChatResponse> override {
        ChatRequest req = request;
        if (req.model.empty()) {
            req.model = cfg.model;
        }

        HttpRequest http{
            .url = cfg.baseUrl + "/chat/completions",
            .method = "POST",
            .headers = {{.name = "Content-Type", .value = "application/json"}},
            .body = buildChatCompletionBody(req),
            .timeoutSeconds = cfg.timeoutSeconds,
        };
        if (!cfg.apiKey.empty()) {
            http.headers.push_back({.name = "Authorization", .value = "Bearer " + cfg.apiKey});
        }

        auto response = httpRequest(http);
        if (!response) {
            return std::unexpected(toError(LlmError::TransportFailed, response.error().context));
        }

        auto parsed = parseChatCompletion(response->body);
        if (!parsed) {
            // Keep the wire layer's classification (ApiError vs
            // BadResponse) but prefix the HTTP status so a 401/429 is
            // obvious in the log.
            Error e = parsed.error();
            e.context = std::format("HTTP {}: {}", response->status, e.context);
            return std::unexpected(e);
        }
        if (response->status < 200 || response->status >= 300) {
            return std::unexpected(toError(
                LlmError::ApiError,
                std::format("HTTP {}: {}", response->status, response->body.substr(0, 256))
            ));
        }
        return parsed;
    }

    [[nodiscard]] auto name() const noexcept -> std::string_view override {
        return "openai-compatible";
    }

    [[nodiscard]] auto model() const noexcept -> const std::string& {
        return cfg.model;
    }

    [[nodiscard]] auto baseUrl() const noexcept -> const std::string& {
        return cfg.baseUrl;
    }

  private:
    OpenAiConfig cfg;
};

} // namespace roboslop
