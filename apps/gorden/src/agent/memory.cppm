module;

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module gorden.agent.memory;

import roboslop.core.error;

namespace gorden {

// The robot's long-term memory, the layers docs/architecture.md calls
// episodic memory, beliefs and goals. Working memory (the bounded chat
// history) stays in AgentBrain; this is what survives it, and what a
// save game stores.
//
// Everything here is written by the model through validated tools, so
// the robot remembers what it decided was worth remembering — not
// every event the simulation produced.

export struct Episode {
    std::string id;
    std::string text{};
    unsigned think = 0; // which think stored it
    double at = 0.0;    // simulation seconds
};

// The provenance shape from docs/architecture.md: a belief is what the
// robot holds true, with where it came from, so "why does the robot
// believe this?" is answerable.
export struct Belief {
    std::string subject;
    std::string predicate{};
    std::string value{};
    std::string source{}; // "player", "observed", another entity's name
    double learnedAt = 0.0;
    float confidence = 0.5F;
};

export enum class GoalStatus : int {
    Active = 1,
    Done,
    Abandoned,
};

export [[nodiscard]] constexpr auto goalStatusName(GoalStatus s) noexcept -> std::string_view {
    switch (s) {
    case GoalStatus::Active:
        return "active";
    case GoalStatus::Done:
        return "done";
    case GoalStatus::Abandoned:
        return "abandoned";
    }
    return "?";
}

export [[nodiscard]] auto goalStatusFromName(std::string_view name) -> GoalStatus {
    if (name == "done") {
        return GoalStatus::Done;
    }
    if (name == "abandoned") {
        return GoalStatus::Abandoned;
    }
    return GoalStatus::Active;
}

export struct Goal {
    std::string id;
    std::string text{};
    GoalStatus status = GoalStatus::Active;
    double createdAt = 0.0;
};

export struct MemoryLimits {
    std::size_t maxEpisodes = 200; // oldest fall out
    std::size_t maxBeliefs = 100;
    std::size_t maxGoals = 50;
};

namespace detail {

// Lowercase words of at least two characters. Retrieval is word-based
// so "the crate" matches "a crate stood here" without matching "crated".
[[nodiscard]] static auto words(std::string_view text) -> std::vector<std::string> {
    std::vector<std::string> out;
    std::string current;
    for (const char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0) {
            current.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        } else if (!current.empty()) {
            out.push_back(std::exchange(current, {}));
        }
    }
    if (!current.empty()) {
        out.push_back(std::move(current));
    }
    std::erase_if(out, [](const std::string& w) { return w.size() < 2; });
    return out;
}

} // namespace detail

export class AgentMemory {
  public:
    AgentMemory() = default;

    explicit AgentMemory(MemoryLimits limits) : caps(limits) {}

    auto remember(std::string text, unsigned think, double at) -> const Episode& {
        episodeList.push_back({
            .id = std::format("ep-{}", ++counter),
            .text = std::move(text),
            .think = think,
            .at = at,
        });
        if (episodeList.size() > caps.maxEpisodes) {
            episodeList.erase(episodeList.begin());
        }
        return episodeList.back();
    }

    // Keyword retrieval, deliberately trivial and inspectable: score is
    // the number of query words the episode mentions, ties go to the
    // newest. No embeddings until we know what the model actually needs
    // (see docs/roadmap.md, M3).
    [[nodiscard]] auto recall(std::string_view query, std::size_t limit) const
        -> std::vector<Episode> {
        const auto wanted = detail::words(query);
        std::vector<std::pair<std::size_t, std::size_t>> scored; // score, index
        for (std::size_t i = 0; i < episodeList.size(); ++i) {
            const auto have = detail::words(episodeList[i].text);
            std::size_t score = 0;
            for (const auto& w : wanted) {
                score += std::ranges::contains(have, w) ? 1U : 0U;
            }
            if (score > 0) {
                scored.emplace_back(score, i);
            }
        }
        std::ranges::sort(scored, [](const auto& a, const auto& b) {
            return a.first != b.first ? a.first > b.first : a.second > b.second;
        });
        std::vector<Episode> out;
        for (const auto& [score, index] : scored) {
            if (out.size() >= limit) {
                break;
            }
            out.push_back(episodeList[index]);
        }
        return out;
    }

    // One value per (subject, predicate): a new claim revises the old
    // one rather than piling up contradictions.
    auto believe(Belief belief) -> void {
        const auto same = [&](const Belief& b) {
            return b.subject == belief.subject && b.predicate == belief.predicate;
        };
        if (auto it = std::ranges::find_if(beliefList, same); it != beliefList.end()) {
            *it = std::move(belief);
            return;
        }
        beliefList.push_back(std::move(belief));
        if (beliefList.size() > caps.maxBeliefs) {
            beliefList.erase(beliefList.begin());
        }
    }

    auto setGoal(std::string text, double at) -> const Goal& {
        goalList.push_back({
            .id = std::format("goal-{}", ++counter),
            .text = std::move(text),
            .status = GoalStatus::Active,
            .createdAt = at,
        });
        if (goalList.size() > caps.maxGoals) {
            goalList.erase(goalList.begin());
        }
        return goalList.back();
    }

    // False when no goal has that id, so the caller can tell the model.
    auto closeGoal(std::string_view id, GoalStatus status) -> bool {
        auto it = std::ranges::find(goalList, id, &Goal::id);
        if (it == goalList.end()) {
            return false;
        }
        it->status = status;
        return true;
    }

    [[nodiscard]] auto episodes() const noexcept -> const std::vector<Episode>& {
        return episodeList;
    }

    [[nodiscard]] auto beliefs() const noexcept -> const std::vector<Belief>& {
        return beliefList;
    }

    [[nodiscard]] auto goals() const noexcept -> const std::vector<Goal>& {
        return goalList;
    }

    [[nodiscard]] auto activeGoals() const -> std::vector<Goal> {
        std::vector<Goal> out;
        for (const auto& g : goalList) {
            if (g.status == GoalStatus::Active) {
                out.push_back(g);
            }
        }
        return out;
    }

    [[nodiscard]] auto limits() const noexcept -> const MemoryLimits& {
        return caps;
    }

    // Serialisation needs to restore the id counter; nothing else reads it.
    [[nodiscard]] auto idCounter() const noexcept -> unsigned {
        return counter;
    }

    auto
    restore(std::vector<Episode> eps, std::vector<Belief> bel, std::vector<Goal> gls, unsigned ids)
        -> void {
        episodeList = std::move(eps);
        beliefList = std::move(bel);
        goalList = std::move(gls);
        counter = ids;
    }

  private:
    MemoryLimits caps{};
    std::vector<Episode> episodeList{};
    std::vector<Belief> beliefList{};
    std::vector<Goal> goalList{};
    unsigned counter = 0;
};

export [[nodiscard]] auto memoryError(std::string context) -> roboslop::Error {
    return {
        .category = "gorden.agent.memory",
        .code = 1,
        .message = "invalid memory",
        .context = std::move(context)
    };
}

export [[nodiscard]] auto toJson(const AgentMemory& memory) -> nlohmann::json {
    nlohmann::json episodes = nlohmann::json::array();
    for (const auto& e : memory.episodes()) {
        episodes.push_back({{"id", e.id}, {"text", e.text}, {"think", e.think}, {"at", e.at}});
    }
    nlohmann::json beliefs = nlohmann::json::array();
    for (const auto& b : memory.beliefs()) {
        beliefs.push_back({
            {"subject", b.subject},
            {"predicate", b.predicate},
            {"value", b.value},
            {"source", b.source},
            {"learned_at", b.learnedAt},
            {"confidence", b.confidence},
        });
    }
    nlohmann::json goals = nlohmann::json::array();
    for (const auto& g : memory.goals()) {
        goals.push_back({
            {"id", g.id},
            {"text", g.text},
            {"status", goalStatusName(g.status)},
            {"created_at", g.createdAt},
        });
    }
    return {
        {"version", 1},
        {"next_id", memory.idCounter()},
        {"episodes", std::move(episodes)},
        {"beliefs", std::move(beliefs)},
        {"goals", std::move(goals)},
    };
}

export [[nodiscard]] auto memoryFromJson(const nlohmann::json& json)
    -> roboslop::Result<AgentMemory> {
    // nlohmann's checked conversion throws; translate at this input boundary.
    try {
        if (json.at("version") != 1) {
            return std::unexpected(memoryError("unsupported version"));
        }
        std::vector<Episode> episodes;
        for (const auto& e : json.at("episodes")) {
            episodes.push_back({
                .id = e.at("id").get<std::string>(),
                .text = e.at("text").get<std::string>(),
                .think = e.at("think").get<unsigned>(),
                .at = e.at("at").get<double>(),
            });
        }
        std::vector<Belief> beliefs;
        for (const auto& b : json.at("beliefs")) {
            beliefs.push_back({
                .subject = b.at("subject").get<std::string>(),
                .predicate = b.at("predicate").get<std::string>(),
                .value = b.at("value").get<std::string>(),
                .source = b.at("source").get<std::string>(),
                .learnedAt = b.at("learned_at").get<double>(),
                .confidence = b.at("confidence").get<float>(),
            });
        }
        std::vector<Goal> goals;
        for (const auto& g : json.at("goals")) {
            goals.push_back({
                .id = g.at("id").get<std::string>(),
                .text = g.at("text").get<std::string>(),
                .status = goalStatusFromName(g.at("status").get<std::string>()),
                .createdAt = g.at("created_at").get<double>(),
            });
        }
        AgentMemory memory;
        memory.restore(
            std::move(episodes),
            std::move(beliefs),
            std::move(goals),
            json.at("next_id").get<unsigned>()
        );
        return memory;
    } catch (const nlohmann::json::exception& e) {
        return std::unexpected(memoryError(e.what()));
    }
}

} // namespace gorden
