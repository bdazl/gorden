module;

#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

export module roboslop.animation.skeleton;

namespace roboslop {

// A skeleton is a flat array of bones; the hierarchy is captured by
// `parents[i]` (index of bone i's parent, or -1 for the root).
// `inverseBindPoses[i]` transforms mesh-space vertices into bone i's
// local space, so a skinned vertex is
//   sum over weights w_b * (boneWorldMat[b] * inverseBindPoses[b] * v).
// Bones are ordered so a parent always precedes its children — the
// skinning system walks the array in order and uses each parent's
// already-resolved world matrix.
export struct Skeleton {
    std::vector<glm::mat4> inverseBindPoses;
    std::vector<std::int32_t> parents; // -1 for root
    std::vector<std::string> boneNames;
};

// Local-space TRS for one bone at a moment in time. Sampling a clip
// produces one of these per bone, in the same order as the skeleton.
export struct BoneTransform {
    glm::vec3 position{0.0F};
    glm::quat rotation{1.0F, 0.0F, 0.0F, 0.0F};
    glm::vec3 scale{1.0F};
};

// Pure: composes a BoneTransform's TRS into a single mat4.
export [[nodiscard]] auto toMatrix(const BoneTransform& t) noexcept -> glm::mat4;

} // namespace roboslop

module :private;

#include <glm/ext/matrix_transform.hpp>

namespace roboslop {

auto toMatrix(const BoneTransform& t) noexcept -> glm::mat4 {
    const glm::mat4 trans = glm::translate(glm::mat4(1.0F), t.position);
    const glm::mat4 rot = glm::mat4_cast(t.rotation);
    const glm::mat4 scl = glm::scale(glm::mat4(1.0F), t.scale);
    return trans * rot * scl;
}

} // namespace roboslop
