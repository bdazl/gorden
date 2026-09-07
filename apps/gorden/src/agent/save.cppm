module;

#include <nlohmann/json.hpp>

#include <algorithm>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

export module gorden.save;

import gorden.agent.brain;
import gorden.agent.memory;
import gorden.agent.observation;
import gorden.agent.robot;
import roboslop.core.error;
import roboslop.core.paths;
import roboslop.ecs;
import roboslop.render.asset_cache;
import roboslop.scene.document;
import roboslop.scene.runtime;
import roboslop.scene.savegame;
import roboslop.scene.transform;

namespace gorden {

// Gorden's half of a save file. The engine stores the scene and the
// transforms of its objects (see docs/save-format.md); everything that
// is Gorden's own — the robot, the player and the robot's memory —
// goes in the save's opaque `app` object, versioned here.
constexpr int AppPayloadVersion = 1;

export [[nodiscard]] auto savePath(std::string_view slot) -> std::filesystem::path {
    return roboslop::stateDir() / "gorden" / "saves" / (std::string{slot} + ".json");
}

// `world` is taken by mutable reference only to dodge a clang 22 crash
// when the const forEach overload is instantiated here; nothing in this
// function writes to the world.
export [[nodiscard]] auto
captureSave(roboslop::World& world, const AgentBrain& brain, std::string scene)
    -> roboslop::SaveGame {
    roboslop::SaveGame save{.scene = std::move(scene)};
    world.forEach<roboslop::SceneIdentity, roboslop::Transform>(
        [&save](
            roboslop::Entity entity,
            const roboslop::SceneIdentity& identity,
            const roboslop::Transform& transform
        ) {
            (void)entity;
            save.objects.push_back({.id = identity.id, .transform = transform});
        }
    );
    save.app = {
        {"version", AppPayloadVersion},
        {"robot", roboslop::transformToJson(world.get<roboslop::Transform>(brain.robotEntity()))},
        {"player", roboslop::transformToJson(world.get<roboslop::Transform>(brain.playerEntity()))},
        {"sim_time", brain.simTime()},
        {"memory", toJson(brain.memory())},
    };
    return save;
}

export [[nodiscard]] auto saveError(std::string context) -> roboslop::Error {
    return {
        .category = "gorden.save",
        .code = 1,
        .message = "invalid save payload",
        .context = std::move(context)
    };
}

// Rebuilds the scene from the authored document with the saved
// transforms written over it, then restores Gorden's own state. Going
// through SceneRuntime::replace rather than writing transforms in place
// is what keeps the physics bodies consistent with where the objects
// end up.
export [[nodiscard]] auto applySave(
    roboslop::World& world,
    roboslop::AssetCache& assets,
    roboslop::SceneRuntime& runtime,
    roboslop::SceneDocument document,
    AgentBrain& brain,
    const roboslop::SaveGame& save
) -> roboslop::Result<void> {
    for (const auto& saved : save.objects) {
        auto it = std::ranges::find(document.objects, saved.id, &roboslop::SceneObject::id);
        if (it == document.objects.end()) {
            // The scene was edited since the save was written; the
            // object is simply gone, which is not an error.
            continue;
        }
        it->transform = saved.transform;
    }
    if (auto replaced = runtime.replace(world, assets, document, true); !replaced) {
        return replaced;
    }
    // replace() destroyed the old scene entities, so the new ones need
    // the perception name the observation looks for, exactly as the
    // app does after its first instantiation.
    world.forEach<roboslop::SceneIdentity>([&world](auto entity, const auto& identity) {
        world.emplace<Named>(entity, Named{.name = identity.name});
    });

    try {
        if (save.app.value("version", 0) != AppPayloadVersion) {
            return std::unexpected(saveError("unsupported gorden payload version"));
        }
        world.get<roboslop::Transform>(brain.robotEntity()) =
            roboslop::transformFromJson(save.app.at("robot"));
        world.get<roboslop::Transform>(brain.playerEntity()) =
            roboslop::transformFromJson(save.app.at("player"));
        auto memory = memoryFromJson(save.app.at("memory"));
        if (!memory) {
            return std::unexpected(memory.error());
        }
        brain.setMemory(std::move(*memory), save.app.at("sim_time").get<double>());
    } catch (const nlohmann::json::exception& e) {
        return std::unexpected(saveError(e.what()));
    }

    // A move from before the load must not resume towards a target the
    // robot no longer has a reason to walk to.
    if (auto* motion = world.tryGet<RobotMotion>(brain.robotEntity()); motion != nullptr) {
        motion->target.reset();
        motion->arrived = false;
    }
    return {};
}

} // namespace gorden
