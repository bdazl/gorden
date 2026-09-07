module;

#include <cstddef>
#include <deque>
#include <expected>
#include <mutex>
#include <string_view>
#include <utility>
#include <vector>

export module roboslop.llm.backend:scripted;

import roboslop.core.error;
import roboslop.llm;

namespace roboslop {

// Deterministic backend: hands out queued responses in order and
// records every request. Used by tests, and by apps as the fallback
// when no real backend is configured, so the whole
// observation → tool call → validation → simulation chain runs without
// a model. Once the script is exhausted it returns `fallback`, which
// defaults to an empty assistant turn ("nothing to do").
export class ScriptedProvider final : public Provider {
  public:
    ScriptedProvider() = default;

    explicit ScriptedProvider(std::vector<ChatResponse> script, ChatResponse fallback = {})
        : queue(script.begin(), script.end()), fallbackResponse(std::move(fallback)) {}

    auto push(ChatResponse response) -> void {
        const std::scoped_lock lock(mutex);
        queue.push_back(std::move(response));
    }

    [[nodiscard]] auto complete(const ChatRequest& request) -> Result<ChatResponse> override {
        const std::scoped_lock lock(mutex);
        requests.push_back(request);
        if (queue.empty()) {
            return fallbackResponse;
        }
        ChatResponse next = std::move(queue.front());
        queue.pop_front();
        return next;
    }

    [[nodiscard]] auto name() const noexcept -> std::string_view override {
        return "scripted";
    }

    // Every request seen so far, oldest first. Copy so tests can
    // inspect it without racing the worker thread.
    [[nodiscard]] auto recordedRequests() const -> std::vector<ChatRequest> {
        const std::scoped_lock lock(mutex);
        return requests;
    }

    [[nodiscard]] auto remaining() const -> std::size_t {
        const std::scoped_lock lock(mutex);
        return queue.size();
    }

  private:
    mutable std::mutex mutex;
    std::deque<ChatResponse> queue;
    ChatResponse fallbackResponse;
    std::vector<ChatRequest> requests;
};

} // namespace roboslop
