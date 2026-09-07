module;
#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

export module editor.model;
import roboslop.scene.document;
import roboslop.scene.transform;
import roboslop.render.primitives;

namespace editor {
export class History {
  public:
    roboslop::SceneDocument document;

    auto checkpoint(const roboslop::SceneDocument& before) -> void {
        if (roboslop::sceneToJson(before) == roboslop::sceneToJson(document)) {
            return;
        }
        past.push_back(before);
        if (past.size() > 128) {
            past.erase(past.begin());
        }
        future.clear();
    }

    [[nodiscard]] auto undo() -> bool {
        if (past.empty()) {
            return false;
        }
        future.push_back(document);
        document = std::move(past.back());
        past.pop_back();
        return true;
    }

    [[nodiscard]] auto redo() -> bool {
        if (future.empty()) {
            return false;
        }
        past.push_back(document);
        document = std::move(future.back());
        future.pop_back();
        return true;
    }

    auto reset(roboslop::SceneDocument scene) -> void {
        document = std::move(scene);
        past.clear();
        future.clear();
    }

  private:
    std::vector<roboslop::SceneDocument> past;
    std::vector<roboslop::SceneDocument> future;
};

export [[nodiscard]] auto nextId(const roboslop::SceneDocument& document) -> std::string {
    for (std::uint64_t n = 1;; ++n) {
        const auto id = "object-" + std::to_string(n);
        if (std::ranges::none_of(document.objects, [&](const auto& o) { return o.id == id; })) {
            return id;
        }
    }
}

// Triangle-accurate CPU picking uses the same primitive geometry as the
// renderer. Transform the ray without normalising so t stays world distance.
export [[nodiscard]] auto
pickObject(const roboslop::SceneDocument& scene, glm::vec3 origin, glm::vec3 direction)
    -> std::string {
    float nearest = std::numeric_limits<float>::max();
    std::string selected;
    for (const auto& o : scene.objects) {
        const auto inverse = glm::inverse(roboslop::toMatrix(o.transform));
        const glm::vec3 ro = glm::vec3(inverse * glm::vec4(origin, 1));
        const glm::vec3 rd = glm::vec3(inverse * glm::vec4(direction, 0));
        const auto geometry = o.geometry == "cube"     ? roboslop::cubeGeometry()
                              : o.geometry == "sphere" ? roboslop::sphereGeometry(24, 32, 0.5F)
                                                       : roboslop::planeGeometry(1, 1);
        const auto point = [&](std::uint16_t index) {
            const auto& p = geometry.vertices[index].position;
            return glm::vec3{p[0], p[1], p[2]};
        };
        for (std::size_t i = 0; i < geometry.indices.size(); i += 3) {
            const auto a = point(geometry.indices[i]);
            const auto e1 = point(geometry.indices[i + 1]) - a;
            const auto e2 = point(geometry.indices[i + 2]) - a;
            const auto p = glm::cross(rd, e2);
            const float det = glm::dot(e1, p);
            if (det < 1e-7F) {
                continue;
            }
            const auto t = ro - a;
            const float u = glm::dot(t, p) / det;
            if (u < 0 || u > 1) {
                continue;
            }
            const auto q = glm::cross(t, e1);
            const float v = glm::dot(rd, q) / det;
            if (v < 0 || u + v > 1) {
                continue;
            }
            const float distance = glm::dot(e2, q) / det;
            if (distance > 0 && distance < nearest) {
                nearest = distance;
                selected = o.id;
            }
        }
    }
    return selected;
}
} // namespace editor
