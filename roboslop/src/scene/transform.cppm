module;

#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

export module roboslop.scene.transform;

namespace roboslop {

// TRS-componentent. Identity by default. mat4 byggs on the fly i
// render-systemet via toMatrix; ingen cachning, ingen dirty-flag.
export struct Transform {
    glm::vec3 position{0.0F, 0.0F, 0.0F};
    glm::quat rotation{1.0F, 0.0F, 0.0F, 0.0F}; // (w, x, y, z) identity
    glm::vec3 scale{1.0F, 1.0F, 1.0F};
};

// Composes T * R * S. Right-handed; +Y up; -Z forward.
export [[nodiscard]] auto toMatrix(const Transform& t) noexcept -> glm::mat4 {
    const glm::mat4 translation = glm::translate(glm::mat4(1.0F), t.position);
    const glm::mat4 rotation = glm::mat4_cast(t.rotation);
    const glm::mat4 scale = glm::scale(glm::mat4(1.0F), t.scale);
    return translation * rotation * scale;
}

} // namespace roboslop
