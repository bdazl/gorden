module;

#include <entt/entt.hpp>

export module roboslop.animation.state;

import roboslop.animation.clip;
import roboslop.ecs;
import roboslop.sched;

namespace roboslop {

// Plays one clip. timeSec advances each fixed step at `speed`; the
// looping form wraps via fmod by the clip's duration. The clip
// pointer is non-owning — the asset cache (or game code) owns the
// AnimationClip.
export struct AnimationState {
    const AnimationClip* clip = nullptr;
    double timeSec = 0.0;
    float speed = 1.0F;
    bool looping = true;
};

// Advances every AnimationState's clock by dt. Sampling and skinning
// happen in a later system once the GPU-skinning path lands.
export auto tickAnimations(SystemCtx& c) -> void {
    auto& reg = c.world->registry();
    for (const auto e : reg.view<AnimationState>()) {
        auto& s = reg.get<AnimationState>(e);
        if (s.clip == nullptr) {
            continue;
        }
        s.timeSec += c.dt * static_cast<double>(s.speed);
        if (s.looping && s.clip->durationSec > 0.0) {
            while (s.timeSec >= s.clip->durationSec) {
                s.timeSec -= s.clip->durationSec;
            }
            while (s.timeSec < 0.0) {
                s.timeSec += s.clip->durationSec;
            }
        }
    }
}

// Game opts in via this helper. Single system today; the GPU skinning
// pass joins the render graph in a follow-up.
export auto registerAnimationSystems(SystemGraph& graph) -> void {
    graph.add({
        .name = "tickAnimations",
        .reads = {},
        .writes = {"animationState"},
        .run = tickAnimations,
    });
}

} // namespace roboslop
