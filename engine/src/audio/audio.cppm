module;

#include <miniaudio.h>

// entt's sparse-set iterator's non-member operator!= is invisible
// across the module boundary for single-component views (see
// docs/decisions.md, 2026-05-17 ECS-facade entry).
#include <entt/entt.hpp>

export module roboslop.audio;

import roboslop.audio.device;
import roboslop.ecs;
import roboslop.sched;
import roboslop.scene.transform;

namespace roboslop {

// Empty tag — marks the entity whose Transform drives the 3D listener
// pose each frame. One listener per world; if more than one is
// tagged, the first one wins.
export struct AudioListener {};

// 3D positional source. `sound` is heap-allocated by the engine so its
// address is stable for miniaudio (which stashes the pointer in its
// own graph). gain is linear amplitude (1.0 = unity).
export struct AudioSource {
    ma_sound* sound = nullptr;
    float gain = 1.0F;
};

// Install the AudioDevice in the world's ctx storage so the audio
// systems can reach it without a typed dependency on roboslop.sched.
export auto installAudioDevice(World& world, AudioDevice& device) -> void {
    world.registry().ctx().emplace<AudioDevice*>(&device);
}

namespace detail {

[[nodiscard]] static inline auto audioDeviceFrom(World& world) -> AudioDevice& {
    return *world.registry().ctx().get<AudioDevice*>();
}

} // namespace detail

// Walks the listener-tagged entity once and pushes its transform to
// miniaudio's listener 0. Listener forward is the entity's local -Z
// axis (engine convention).
export auto updateAudioListener(World& world) -> void {
    auto& device = detail::audioDeviceFrom(world);
    auto& reg = world.registry();
    for (const auto e : reg.view<const AudioListener, const Transform>()) {
        const auto& t = reg.get<const Transform>(e);
        device.setListenerPosition(t.position.x, t.position.y, t.position.z);
        // Forward = rotation * -Z. For MVP, take the unrotated axis;
        // free-fly camera carries pitch/yaw on the FreeFlyCamera
        // controller, not the Transform's rotation.
        device.setListenerDirection(0.0F, 0.0F, -1.0F);
        return;
    }
}

// Pushes per-source pose to miniaudio. Skips sources whose ma_sound
// is null — common during the first frame before assets resolve.
export auto updateAudioSources(World& world) -> void {
    auto& reg = world.registry();
    for (const auto e : reg.view<const AudioSource, const Transform>()) {
        const auto& src = reg.get<const AudioSource>(e);
        const auto& t = reg.get<const Transform>(e);
        if (src.sound == nullptr) {
            continue;
        }
        ma_sound_set_position(src.sound, t.position.x, t.position.y, t.position.z);
        ma_sound_set_volume(src.sound, src.gain);
    }
}

// Registers the listener and source update systems into the supplied
// fixed-update SystemGraph. Both read "transforms" and write "audio",
// so they serialise but stay independent of physics/rendering.
export auto registerAudioSystems(SystemGraph& graph) -> void {
    graph.add({
        .name = "audioListener",
        .reads = {"transforms"},
        .writes = {"audio"},
        .run = [](SystemCtx& c) { updateAudioListener(*c.world); },
    });
    graph.add({
        .name = "audioSources",
        .reads = {"transforms"},
        .writes = {"audio"},
        .run = [](SystemCtx& c) { updateAudioSources(*c.world); },
    });
}

} // namespace roboslop
