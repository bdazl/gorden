module;

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

export module gorden.agent.tools;

import gorden.agent.observation;
import roboslop.core.error;
import roboslop.llm;

namespace gorden {

// The three high-level actions of the first slice. A ToolCall from the
// model is only a *proposal*; parseToolCall turns its JSON into one of
// these, and validate() decides whether the simulation will honour it.
export struct MoveTo {
    glm::vec3 target{0.0F};
};

export struct Inspect {
    std::string name{};
};

export struct Say {
    std::string text{};
};

export using Command = std::variant<MoveTo, Inspect, Say>;

export enum class ToolError : int {
    UnknownTool = 1,
    BadArguments = 2,
    TooFar = 3,
    OutOfWorld = 4,
    UnknownEntity = 5,
    EmptyText = 6,
    TextTooLong = 7,
};

export [[nodiscard]] auto toError(ToolError e, std::string ctx = {}) -> roboslop::Error {
    const auto make = [&](std::string_view msg) {
        return roboslop::Error{
            .category = "gorden.agent.tools",
            .code = static_cast<int>(e),
            .message = msg,
            .context = std::move(ctx)
        };
    };
    switch (e) {
    case ToolError::UnknownTool:
        return make("unknown tool");
    case ToolError::BadArguments:
        return make("tool arguments are malformed");
    case ToolError::TooFar:
        return make("target is farther than the robot may travel in one move");
    case ToolError::OutOfWorld:
        return make("target is outside the world bounds");
    case ToolError::UnknownEntity:
        return make("no visible entity with that name");
    case ToolError::EmptyText:
        return make("say text is empty");
    case ToolError::TextTooLong:
        return make("say text is too long");
    }
    return make("unknown ToolError");
}

// Tool schemas as sent to the model. Names here are the contract with
// parseToolCall below.
export [[nodiscard]] auto toolSpecs() -> std::vector<roboslop::ToolSpec> {
    return {
        roboslop::ToolSpec{
            .name = "moveTo",
            .description = "Walk to a point on the ground. Coordinates are world metres; "
                           "y is up, so give x and z.",
            .parametersSchemaJson = R"({"type":"object","properties":{
                "x":{"type":"number"},"z":{"type":"number"}},
                "required":["x","z"]})",
        },
        roboslop::ToolSpec{
            .name = "inspect",
            .description = "Look closely at a visible entity by name and learn where it is.",
            .parametersSchemaJson = R"({"type":"object","properties":{
                "name":{"type":"string"}},"required":["name"]})",
        },
        roboslop::ToolSpec{
            .name = "say",
            .description = "Say something out loud to the player. Keep it short.",
            .parametersSchemaJson = R"({"type":"object","properties":{
                "text":{"type":"string"}},"required":["text"]})",
        },
    };
}

export [[nodiscard]] auto parseToolCall(const roboslop::ToolCall& call)
    -> roboslop::Result<Command> {
    const auto args =
        nlohmann::json::parse(call.argumentsJson, nullptr, /*allow_exceptions=*/false);
    if (args.is_discarded() || !args.is_object()) {
        return std::unexpected(toError(ToolError::BadArguments, call.argumentsJson));
    }
    if (call.name == "moveTo") {
        if (!args.contains("x") || !args.contains("z") || !args["x"].is_number() ||
            !args["z"].is_number()) {
            return std::unexpected(toError(ToolError::BadArguments, "moveTo needs numeric x, z"));
        }
        return Command{MoveTo{.target = {args["x"].get<float>(), 0.0F, args["z"].get<float>()}}};
    }
    if (call.name == "inspect") {
        if (!args.contains("name") || !args["name"].is_string()) {
            return std::unexpected(toError(ToolError::BadArguments, "inspect needs a name"));
        }
        return Command{Inspect{.name = args["name"].get<std::string>()}};
    }
    if (call.name == "say") {
        if (!args.contains("text") || !args["text"].is_string()) {
            return std::unexpected(toError(ToolError::BadArguments, "say needs text"));
        }
        return Command{Say{.text = args["text"].get<std::string>()}};
    }
    return std::unexpected(toError(ToolError::UnknownTool, call.name));
}

export struct Rules {
    float maxMoveDistance = 30.0F; // per moveTo, in metres from the robot
    float worldHalfExtent = 12.0F; // the ground plane is 20 m; keep a margin
    std::size_t maxSayLength = 200;
};

// The validation boundary: a Command comes out only if the simulation
// will actually do it. Rejections carry the rule in Error::message so
// the model can be told why.
export [[nodiscard]] auto validate(const Command& cmd, const Observation& obs, const Rules& rules)
    -> roboslop::Result<Command> {
    return std::visit(
        [&](const auto& c) -> roboslop::Result<Command> {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, MoveTo>) {
                if (!std::isfinite(c.target.x) || !std::isfinite(c.target.z)) {
                    return std::unexpected(toError(ToolError::BadArguments, "non-finite target"));
                }
                if (std::abs(c.target.x) > rules.worldHalfExtent ||
                    std::abs(c.target.z) > rules.worldHalfExtent) {
                    return std::unexpected(toError(
                        ToolError::OutOfWorld,
                        std::format("bounds are ±{:.0f} m", rules.worldHalfExtent)
                    ));
                }
                const glm::vec3 flatRobot{obs.robotPosition.x, 0.0F, obs.robotPosition.z};
                const float d = glm::distance(flatRobot, c.target);
                if (d > rules.maxMoveDistance) {
                    return std::unexpected(toError(
                        ToolError::TooFar,
                        std::format("{:.1f} m requested, max {:.0f} m", d, rules.maxMoveDistance)
                    ));
                }
                return Command{c};
            } else if constexpr (std::is_same_v<T, Inspect>) {
                const bool visible = std::ranges::any_of(obs.nearby, [&](const ObservedEntity& e) {
                    return e.name == c.name;
                });
                if (!visible) {
                    return std::unexpected(toError(ToolError::UnknownEntity, c.name));
                }
                return Command{c};
            } else {
                if (c.text.empty()) {
                    return std::unexpected(toError(ToolError::EmptyText));
                }
                if (c.text.size() > rules.maxSayLength) {
                    return std::unexpected(toError(
                        ToolError::TextTooLong,
                        std::format("{} chars, max {}", c.text.size(), rules.maxSayLength)
                    ));
                }
                return Command{c};
            }
        },
        cmd
    );
}

export [[nodiscard]] auto commandName(const Command& cmd) noexcept -> std::string_view {
    return std::visit(
        [](const auto& c) -> std::string_view {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, MoveTo>) {
                return "moveTo";
            } else if constexpr (std::is_same_v<T, Inspect>) {
                return "inspect";
            } else {
                return "say";
            }
        },
        cmd
    );
}

// One-line rendering for the action log.
export [[nodiscard]] auto describeCommand(const Command& cmd) -> std::string {
    return std::visit(
        [](const auto& c) -> std::string {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, MoveTo>) {
                return std::format("moveTo({:.1f}, {:.1f})", c.target.x, c.target.z);
            } else if constexpr (std::is_same_v<T, Inspect>) {
                return std::format("inspect({})", c.name);
            } else {
                return std::format("say(\"{}\")", c.text);
            }
        },
        cmd
    );
}

} // namespace gorden
