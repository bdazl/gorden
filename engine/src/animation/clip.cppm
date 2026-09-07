module;

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

export module roboslop.animation.clip;

import roboslop.animation.skeleton;

namespace roboslop {

// Per-channel keyframe streams for one bone. Each channel can hold any
// number of frames; the times must be strictly increasing.
export struct BoneTrack {
    std::vector<double> positionTimes;
    std::vector<glm::vec3> positions;
    std::vector<double> rotationTimes;
    std::vector<glm::quat> rotations;
    std::vector<double> scaleTimes;
    std::vector<glm::vec3> scales;
};

// A clip is N tracks (one per bone, matching the skeleton order) plus
// a duration. Looped playback wraps via t % duration upstream.
export struct AnimationClip {
    std::vector<BoneTrack> tracks;
    double durationSec = 0.0;
};

namespace detail {

// Find the keyframe pair (i, i+1) whose times bracket `t`. Returns
// the index of the lower frame and the local interpolation parameter
// in [0, 1]. Clamps at the ends.
template <typename T>
[[nodiscard]] auto findKeyframe(std::span<const double> times, double t) noexcept
    -> std::pair<std::size_t, double> {
    if (times.empty() || t <= times.front()) {
        return {0, 0.0};
    }
    if (t >= times.back()) {
        return {times.size() - 1, 0.0};
    }
    const auto upper = std::ranges::upper_bound(times, t);
    const std::size_t hi = static_cast<std::size_t>(upper - times.begin());
    const std::size_t lo = hi - 1;
    const double span = times[hi] - times[lo];
    const double alpha = span > 0.0 ? (t - times[lo]) / span : 0.0;
    return {lo, alpha};
}

} // namespace detail

// Pure: sample the clip at time `t` (seconds, not normalised), writing
// one local-space BoneTransform per bone to `out`. `out.size()` must
// equal `clip.tracks.size()`. Identity for any bone with empty
// channels. Caller-allocated output → no heap traffic.
export auto sampleClip(const AnimationClip& clip, double t, std::span<BoneTransform> out) -> void {
    const std::size_t n = std::min(clip.tracks.size(), out.size());
    for (std::size_t i = 0; i < n; ++i) {
        const auto& trk = clip.tracks[i];
        BoneTransform& bt = out[i];

        if (!trk.positionTimes.empty()) {
            const auto [lo, a] = detail::findKeyframe<glm::vec3>(trk.positionTimes, t);
            if (lo + 1 < trk.positions.size() && a > 0.0) {
                bt.position =
                    glm::mix(trk.positions[lo], trk.positions[lo + 1], static_cast<float>(a));
            } else {
                bt.position = trk.positions[lo];
            }
        }

        if (!trk.rotationTimes.empty()) {
            const auto [lo, a] = detail::findKeyframe<glm::quat>(trk.rotationTimes, t);
            if (lo + 1 < trk.rotations.size() && a > 0.0) {
                bt.rotation =
                    glm::slerp(trk.rotations[lo], trk.rotations[lo + 1], static_cast<float>(a));
            } else {
                bt.rotation = trk.rotations[lo];
            }
        }

        if (!trk.scaleTimes.empty()) {
            const auto [lo, a] = detail::findKeyframe<glm::vec3>(trk.scaleTimes, t);
            if (lo + 1 < trk.scales.size() && a > 0.0) {
                bt.scale = glm::mix(trk.scales[lo], trk.scales[lo + 1], static_cast<float>(a));
            } else {
                bt.scale = trk.scales[lo];
            }
        }
    }
}

} // namespace roboslop
