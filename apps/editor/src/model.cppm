module;
#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

export module editor.model;
import roboslop.assets.mesh;
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

// Model-space bounds keyed by the scene's model path. The runtime that
// loaded the files fills this; an object whose path is missing has no
// bounds and is skipped by picking and selection drawing.
export using ModelBounds = std::map<std::string, roboslop::Aabb>;

// Object-space bounds: the unit box for primitives, the loaded bounds
// for models.
export [[nodiscard]] auto
objectBounds(const roboslop::SceneObject& object, const ModelBounds& models)
    -> std::optional<roboslop::Aabb> {
    if (object.geometry != "model") {
        return roboslop::Aabb{.min = glm::vec3{-0.5F}, .max = glm::vec3{0.5F}};
    }
    if (const auto it = models.find(object.model); it != models.end()) {
        return it->second;
    }
    return std::nullopt;
}

// Model files the editor can add: `models/**/*.glb` relative to the
// asset root, sorted. Empty when the directory does not exist.
export [[nodiscard]] auto listModels(const std::filesystem::path& assetRoot)
    -> std::vector<std::string> {
    std::vector<std::string> out;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator{assetRoot / "models", ec};
         it != std::filesystem::recursive_directory_iterator{};
         it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        const auto& entry = *it;
        if (entry.is_regular_file(ec) && entry.path().extension() == ".glb") {
            out.push_back(entry.path().lexically_relative(assetRoot).generic_string());
        }
        if (ec) {
            ec.clear();
        }
    }
    std::ranges::sort(out);
    return out;
}

namespace detail {
// The renderer's shared unit primitives, rebuilt on the CPU for picking.
static auto primitiveGeometry(const std::string& name) -> roboslop::Geometry {
    if (name == "cube") {
        return roboslop::cubeGeometry();
    }
    if (name == "sphere") {
        return roboslop::sphereGeometry(24, 32, 0.5F);
    }
    return roboslop::planeGeometry(1, 1);
}

// Slab test in object space with an unnormalised ray; returns the entry
// distance in world units, or nothing on a miss.
static auto rayBox(glm::vec3 ro, glm::vec3 rd, const roboslop::Aabb& box) -> std::optional<float> {
    float enter = 0;
    float exit = std::numeric_limits<float>::max();
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(rd[axis]) < 1e-9F) {
            if (ro[axis] < box.min[axis] || ro[axis] > box.max[axis]) {
                return std::nullopt;
            }
            continue;
        }
        float t0 = (box.min[axis] - ro[axis]) / rd[axis];
        float t1 = (box.max[axis] - ro[axis]) / rd[axis];
        if (t0 > t1) {
            std::swap(t0, t1);
        }
        enter = std::max(enter, t0);
        exit = std::min(exit, t1);
        if (enter > exit) {
            return std::nullopt;
        }
    }
    return enter;
}
} // namespace detail

// CPU picking: triangle-accurate against the renderer's primitive
// geometry, bounds-only for models. Transform the ray without
// normalising so t stays world distance.
export [[nodiscard]] auto pickObject(
    const roboslop::SceneDocument& scene,
    glm::vec3 origin,
    glm::vec3 direction,
    const ModelBounds& models = {}
) -> std::string {
    float nearest = std::numeric_limits<float>::max();
    std::string selected;
    for (const auto& o : scene.objects) {
        const auto inverse = glm::inverse(roboslop::toMatrix(o.transform));
        const glm::vec3 ro = glm::vec3(inverse * glm::vec4(origin, 1));
        const glm::vec3 rd = glm::vec3(inverse * glm::vec4(direction, 0));
        if (o.geometry == "model") {
            const auto bounds = objectBounds(o, models);
            if (!bounds) {
                continue;
            }
            if (const auto hit = detail::rayBox(ro, rd, *bounds); hit && *hit < nearest) {
                nearest = *hit;
                selected = o.id;
            }
            continue;
        }
        const auto geometry = detail::primitiveGeometry(o.geometry);
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
