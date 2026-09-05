module;

#include <spdlog/spdlog.h>

#include <cstddef>
#include <deque>
#include <expected>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

export module gorden.agent.brain;

import gorden.agent.observation;
import gorden.agent.robot;
import gorden.agent.tools;
import roboslop.core.error;
import roboslop.ecs;
import roboslop.llm;
import roboslop.scene.transform;

namespace gorden {

// What the simulation reports back to the agent. Events are the only
// thing that makes the robot think: there is no think tick.
export enum class AgentEventKind : int {
    PlayerMessage,
    ToolRejected,
    MoveCompleted,
    InspectResult,
    Said,
    ProviderError,
};

export [[nodiscard]] constexpr auto eventKindName(AgentEventKind k) noexcept -> std::string_view {
    switch (k) {
    case AgentEventKind::PlayerMessage:
        return "player_message";
    case AgentEventKind::ToolRejected:
        return "tool_rejected";
    case AgentEventKind::MoveCompleted:
        return "move_completed";
    case AgentEventKind::InspectResult:
        return "inspect_result";
    case AgentEventKind::Said:
        return "said";
    case AgentEventKind::ProviderError:
        return "provider_error";
    }
    return "?";
}

export struct AgentEvent {
    AgentEventKind kind = AgentEventKind::PlayerMessage;
    std::string text{};
};

export struct TranscriptLine {
    std::string who; // "player" or "robot"
    std::string text{};
};

export struct BrainConfig {
    std::string model;           // empty → provider default
    std::size_t maxHistory = 24; // messages kept in working memory
    int maxChainedThinks = 4;    // per player message
    float observeRadius = 25.0F;
    Rules rules;
    std::string robotName = "Gorden";
    std::string playerName = "Player";
    // `{robot}` and `{player}` are replaced with the names above.
    std::string systemPrompt =
        "You are {robot}, the robot companion of {player} in a small yard. You perceive "
        "the world only through the JSON observation in each user message. Act through "
        "the tools; use say to talk to {player} in one or two short sentences. "
        "Coordinates are metres, y is up. When a move completes or a tool is rejected you "
        "get a new observation; do not repeat a rejected action.";
};

export [[nodiscard]] auto renderSystemPrompt(const BrainConfig& cfg) -> std::string {
    std::string out = cfg.systemPrompt;
    for (const auto& [key, value] :
         {std::pair{std::string{"{robot}"}, cfg.robotName},
          std::pair{std::string{"{player}"}, cfg.playerName}}) {
        for (auto pos = out.find(key); pos != std::string::npos; pos = out.find(key, pos)) {
            out.replace(pos, key.size(), value);
            pos += value.size();
        }
    }
    return out;
}

// The agent loop for one robot:
//
//   events → observation → provider (async) → tool calls → validation
//          → commands applied to the world → events ...
//
// pump() runs on the main thread each fixed step. Inference never
// blocks it: a think is started with startCompletion and collected on a
// later pump. Everything that happened is written to `actionLog` — the
// validated sequence the M3 replay work will consume.
export class AgentBrain {
  public:
    AgentBrain(
        std::unique_ptr<roboslop::Provider> provider,
        BrainConfig config,
        roboslop::Entity robot,
        roboslop::Entity player
    )
        : provider(std::move(provider)), cfg(std::move(config)), robot(robot), player(player) {}

    AgentBrain(const AgentBrain&) = delete;
    auto operator=(const AgentBrain&) -> AgentBrain& = delete;
    AgentBrain(AgentBrain&&) noexcept = default;
    auto operator=(AgentBrain&&) noexcept -> AgentBrain& = default;
    ~AgentBrain() = default;

    auto playerSays(std::string text) -> void {
        transcriptLines.push_back({.who = "player", .text = text});
        chainedThinks = 0;
        capLogged = false;
        queueEvent({.kind = AgentEventKind::PlayerMessage, .text = std::move(text)});
    }

    auto pump(roboslop::World& world) -> void {
        collectRobotEvents(world);
        if (pending.active()) {
            if (pending.ready()) {
                finishThink(world, pending.take());
            }
            return;
        }
        if (!events.empty()) {
            if (chainedThinks >= cfg.maxChainedThinks) {
                // Keep the events; they ride along with the next
                // player message instead of triggering yet another
                // round trip.
                if (!capLogged) {
                    log(std::format(
                        "think cap ({}) reached; waiting for the player", cfg.maxChainedThinks
                    ));
                    capLogged = true;
                }
                return;
            }
            startThink(world);
        }
    }

    // Live rename (Settings window). Takes effect from the next think;
    // the transcript keeps role keys ("player"/"robot"), the UI maps
    // them to names.
    auto setNames(std::string robotName, std::string playerName) -> void {
        cfg.robotName = std::move(robotName);
        cfg.playerName = std::move(playerName);
    }

    [[nodiscard]] auto config() const noexcept -> const BrainConfig& {
        return cfg;
    }

    [[nodiscard]] auto robotEntity() const noexcept -> roboslop::Entity {
        return robot;
    }

    [[nodiscard]] auto playerEntity() const noexcept -> roboslop::Entity {
        return player;
    }

    [[nodiscard]] auto thinking() const noexcept -> bool {
        return pending.active();
    }

    [[nodiscard]] auto providerName() const noexcept -> std::string_view {
        return provider->name();
    }

    [[nodiscard]] auto transcript() const noexcept -> const std::vector<TranscriptLine>& {
        return transcriptLines;
    }

    [[nodiscard]] auto actionLog() const noexcept -> const std::vector<std::string>& {
        return actionLogLines;
    }

    [[nodiscard]] auto pendingEventCount() const noexcept -> std::size_t {
        return events.size();
    }

    [[nodiscard]] auto thinkCount() const noexcept -> unsigned {
        return thinks;
    }

  private:
    auto queueEvent(AgentEvent e) -> void {
        log(std::format("event {}: {}", eventKindName(e.kind), e.text));
        events.push_back(std::move(e));
    }

    auto collectRobotEvents(roboslop::World& world) -> void {
        if (auto* m = world.tryGet<RobotMotion>(robot); m != nullptr && m->arrived) {
            m->arrived = false;
            const auto& t = world.get<roboslop::Transform>(robot);
            queueEvent({
                .kind = AgentEventKind::MoveCompleted,
                .text = std::format("arrived at ({:.1f}, {:.1f})", t.position.x, t.position.z),
            });
        }
    }

    auto startThink(roboslop::World& world) -> void {
        Observation obs = buildObservation(world, robot, player, cfg.observeRadius);
        if (const auto* m = world.tryGet<RobotMotion>(robot); m != nullptr) {
            obs.robotMoving = m->target.has_value();
        }
        for (auto& e : events) {
            if (e.kind == AgentEventKind::PlayerMessage) {
                obs.playerMessage = e.text;
            } else {
                obs.recentEvents.push_back(std::format("{}: {}", eventKindName(e.kind), e.text));
            }
        }
        events.clear();

        history.push_back({.role = roboslop::Role::User, .content = observationToJson(obs)});
        trimHistory();

        roboslop::ChatRequest req{.model = cfg.model, .tools = toolSpecs()};
        req.messages.reserve(history.size() + 1);
        req.messages.push_back(
            {.role = roboslop::Role::System, .content = renderSystemPrompt(cfg)}
        );
        req.messages.insert(req.messages.end(), history.begin(), history.end());

        ++thinks;
        ++chainedThinks;
        log(std::format(
            "think #{} started ({} nearby, {} events)",
            thinks,
            obs.nearby.size(),
            obs.recentEvents.size()
        ));
        pending = roboslop::startCompletion(*provider, std::move(req));
    }

    auto finishThink(roboslop::World& world, roboslop::Result<roboslop::ChatResponse> result)
        -> void {
        if (!result) {
            const auto& e = result.error();
            const std::string text = std::format("{} ({})", e.message, e.context);
            log(std::format("provider error: {}", text));
            spdlog::warn("gorden: provider error: {}", text);
            // Not queued as an event on purpose: an error must not
            // trigger another think and loop against a dead server.
            transcriptLines.push_back({.who = "robot", .text = "[provider error: " + text + "]"});
            // Drop the observation we sent so the next think re-sends
            // a fresh one instead of two users in a row.
            if (!history.empty() && history.back().role == roboslop::Role::User) {
                history.pop_back();
            }
            return;
        }

        const roboslop::ChatResponse& resp = *result;
        history.push_back({
            .role = roboslop::Role::Assistant,
            .content = resp.content,
            .toolCalls = resp.toolCalls,
        });
        if (!resp.content.empty()) {
            // Plain assistant text is treated as speech too, so a model
            // that answers without calling `say` is still heard.
            transcriptLines.push_back({.who = "robot", .text = resp.content});
            log(std::format("assistant text: {}", resp.content));
        }

        Observation obs = buildObservation(world, robot, player, cfg.observeRadius);
        for (const auto& call : resp.toolCalls) {
            auto parsed = parseToolCall(call);
            auto validated = parsed ? validate(*parsed, obs, cfg.rules)
                                    : roboslop::Result<Command>{std::unexpected(parsed.error())};
            std::string toolResult{};
            if (!validated) {
                const auto& e = validated.error();
                toolResult = std::format("rejected: {} ({})", e.message, e.context);
                log(std::format(
                    "proposed {} {} → REJECTED: {} {}",
                    call.name,
                    call.argumentsJson,
                    e.message,
                    e.context
                ));
                queueEvent(
                    {.kind = AgentEventKind::ToolRejected,
                     .text = std::format("{}: {}", call.name, e.message)}
                );
            } else {
                log(std::format("proposed {} → accepted", describeCommand(*validated)));
                toolResult = apply(world, *validated, obs);
            }
            history.push_back({
                .role = roboslop::Role::Tool,
                .content = toolResult,
                .toolCallId = call.id,
            });
        }
        trimHistory();
    }

    // Executes a validated command and returns the tool-result text
    // the model sees. Side effects on the world happen here and only
    // here.
    auto apply(roboslop::World& world, const Command& cmd, const Observation& obs) -> std::string {
        return std::visit(
            [&](const auto& c) -> std::string {
                using T = std::decay_t<decltype(c)>;
                if constexpr (std::is_same_v<T, MoveTo>) {
                    auto& m = world.get<RobotMotion>(robot);
                    m.target = c.target;
                    m.arrived = false;
                    // No event now: MoveCompleted arrives from the
                    // locomotion system when the robot gets there.
                    return "accepted: moving; you will be told when you arrive";
                } else if constexpr (std::is_same_v<T, Inspect>) {
                    std::string text = "not visible";
                    for (const auto& e : obs.nearby) {
                        if (e.name == c.name) {
                            text = std::format(
                                "{} is at ({:.1f}, {:.1f}), {:.1f} m away",
                                e.name,
                                e.position.x,
                                e.position.z,
                                e.distance
                            );
                            break;
                        }
                    }
                    queueEvent({.kind = AgentEventKind::InspectResult, .text = text});
                    return text;
                } else {
                    transcriptLines.push_back({.who = "robot", .text = c.text});
                    log(std::format("said: {}", c.text));
                    // Speech needs no follow-up think; it is not queued
                    // as an event.
                    return "said";
                }
            },
            cmd
        );
    }

    // Working memory is bounded. Drop oldest messages, then make sure
    // the window does not start with orphaned tool results (the API
    // rejects a Tool message whose assistant call was trimmed away).
    auto trimHistory() -> void {
        while (history.size() > cfg.maxHistory) {
            history.pop_front();
        }
        while (!history.empty() && history.front().role == roboslop::Role::Tool) {
            history.pop_front();
        }
    }

    auto log(std::string line) -> void {
        spdlog::info("gorden agent: {}", line);
        actionLogLines.push_back(std::move(line));
    }

    std::unique_ptr<roboslop::Provider> provider;
    BrainConfig cfg;
    roboslop::Entity robot{};
    roboslop::Entity player{};
    std::deque<roboslop::ChatMessage> history;
    std::vector<AgentEvent> events{};
    roboslop::AsyncCompletion pending;
    std::vector<TranscriptLine> transcriptLines{};
    std::vector<std::string> actionLogLines{};
    unsigned thinks = 0;
    int chainedThinks = 0;
    bool capLogged = false;
};

} // namespace gorden
