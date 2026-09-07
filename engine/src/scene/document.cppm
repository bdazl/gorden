module;

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

export module roboslop.scene.document;
import roboslop.core.error;
import roboslop.core.json_file;
import roboslop.scene.transform;
import roboslop.render.lighting;

namespace roboslop {

export struct SceneObject {
    std::string id;
    std::string name = "Object";
    std::string geometry = "cube";
    std::string material = "stone";
    Transform transform{};
    // Collider follows geometry and scale. Planes are visual only.
    std::string body = "none";
};

export struct SceneDocument {
    std::vector<SceneObject> objects;
    std::map<std::string, glm::vec3> materials{
        {"stone", {0.65F, 0.7F, 0.75F}}, {"wood", {0.65F, 0.35F, 0.15F}}
    };
    DirectionalLight light{.direction = {-0.3F, -1.0F, -0.2F}};
    Transform camera{.position = {6, 4, 12}};
};

export [[nodiscard]] auto sceneError(std::string context) -> Error {
    return {
        .category = "roboslop.scene",
        .code = 1,
        .message = "invalid scene",
        .context = std::move(context)
    };
}

export [[nodiscard]] auto validateScene(const SceneDocument& scene) -> Result<void> {
    const auto finite = [](const glm::vec3& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    };
    const auto transformValid = [&](const Transform& t) {
        const float q = glm::dot(t.rotation, t.rotation);
        return finite(t.position) && finite(t.scale) && std::isfinite(q) &&
               std::abs(q - 1.0F) < 0.001F && t.scale.x >= 0.01F && t.scale.y >= 0.01F &&
               t.scale.z >= 0.01F && t.scale.x <= 1000 && t.scale.y <= 1000 && t.scale.z <= 1000;
    };
    if (scene.objects.size() > 2000 || scene.materials.empty() || scene.materials.size() > 256) {
        return std::unexpected(sceneError("limit: 2000 objects, 1..256 materials"));
    }
    if (!transformValid(scene.camera) || !finite(scene.light.direction) ||
        glm::dot(scene.light.direction, scene.light.direction) < 0.0001F ||
        !finite(scene.light.color) || !std::isfinite(scene.light.intensity) ||
        scene.light.intensity < 0 || scene.light.intensity > 100) {
        return std::unexpected(sceneError("invalid camera or light"));
    }
    for (const auto& [id, color] : scene.materials) {
        if (id.empty() || !finite(color) || color.x < 0 || color.y < 0 || color.z < 0 ||
            color.x > 1 || color.y > 1 || color.z > 1) {
            return std::unexpected(sceneError("invalid material: " + id));
        }
    }
    std::set<std::string> ids;
    std::size_t bodies = 0;
    for (const auto& object : scene.objects) {
        if (object.body != "none" && ++bodies > 1000) {
            return std::unexpected(sceneError("limit: 1000 physics bodies"));
        }
        if (object.id.empty() || !ids.insert(object.id).second || object.name.empty() ||
            !transformValid(object.transform) || !scene.materials.contains(object.material) ||
            (object.geometry != "cube" && object.geometry != "sphere" &&
             object.geometry != "plane") ||
            (object.body != "none" && object.body != "static" && object.body != "dynamic") ||
            (object.geometry == "plane" && object.body != "none") ||
            (object.geometry == "sphere" && object.body != "none" &&
             (std::abs(object.transform.scale.x - object.transform.scale.y) > 0.001F ||
              std::abs(object.transform.scale.x - object.transform.scale.z) > 0.001F))) {
            return std::unexpected(sceneError(
                "object " + object.id +
                ": check ID, material, transform and collider (sphere requires uniform scale)"
            ));
        }
    }
    return {};
}

namespace detail {
auto vectorJson(const glm::vec3& v) -> nlohmann::json {
    return {v.x, v.y, v.z};
}

auto transformJson(const Transform& t) -> nlohmann::json {
    return {
        {"position", vectorJson(t.position)},
        {"scale", vectorJson(t.scale)},
        {"rotation", {t.rotation.w, t.rotation.x, t.rotation.y, t.rotation.z}}
    };
}

auto readVector(const nlohmann::json& j) -> glm::vec3 {
    if (!j.is_array() || j.size() != 3) {
        return {NAN, NAN, NAN};
    }
    return {j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>()};
}

auto readTransform(const nlohmann::json& j) -> Transform {
    const auto& q = j.at("rotation");
    Transform t{.position = readVector(j.at("position")), .scale = readVector(j.at("scale"))};
    if (!q.is_array() || q.size() != 4) {
        t.rotation = glm::quat{0, 0, 0, 0};
    } else {
        t.rotation = glm::quat{
            q.at(0).get<float>(), q.at(1).get<float>(), q.at(2).get<float>(), q.at(3).get<float>()
        };
    }
    return t;
}
} // namespace detail

export [[nodiscard]] auto sceneToJson(const SceneDocument& scene) -> nlohmann::json {
    nlohmann::json objects = nlohmann::json::array();
    for (const auto& o : scene.objects) {
        objects.push_back(
            {{"id", o.id},
             {"name", o.name},
             {"geometry", o.geometry},
             {"material", o.material},
             {"transform", detail::transformJson(o.transform)},
             {"body", o.body}}
        );
    }
    nlohmann::json materials = nlohmann::json::object();
    for (const auto& [id, color] : scene.materials) {
        materials[id] = detail::vectorJson(color);
    }
    return {
        {"version", 1},
        {"objects", objects},
        {"materials", materials},
        {"camera", detail::transformJson(scene.camera)},
        {"light",
         {{"direction", detail::vectorJson(scene.light.direction)},
          {"color", detail::vectorJson(scene.light.color)},
          {"intensity", scene.light.intensity}}}
    };
}

export [[nodiscard]] auto sceneFromJson(const nlohmann::json& json) -> Result<SceneDocument> {
    // nlohmann's checked conversion throws; translate at this input boundary.
    try {
        if (json.at("version") != 1 || !json.at("objects").is_array() ||
            !json.at("materials").is_object()) {
            return std::unexpected(sceneError("unsupported version or invalid collections"));
        }
        SceneDocument scene;
        scene.materials.clear();
        for (const auto& [id, color] : json.at("materials").items()) {
            scene.materials.emplace(id, detail::readVector(color));
        }
        scene.camera = detail::readTransform(json.at("camera"));
        const auto& light = json.at("light");
        scene.light = {
            .direction = detail::readVector(light.at("direction")),
            .color = detail::readVector(light.at("color")),
            .intensity = light.at("intensity").get<float>()
        };
        for (const auto& o : json.at("objects")) {
            scene.objects.push_back(
                {.id = o.at("id").get<std::string>(),
                 .name = o.at("name").get<std::string>(),
                 .geometry = o.at("geometry").get<std::string>(),
                 .material = o.at("material").get<std::string>(),
                 .transform = detail::readTransform(o.at("transform")),
                 .body = o.at("body").get<std::string>()}
            );
        }
        if (auto valid = validateScene(scene); !valid) {
            return std::unexpected(valid.error());
        }
        return scene;
    } catch (const nlohmann::json::exception& e) {
        return std::unexpected(sceneError(e.what()));
    }
}

export [[nodiscard]] auto loadScene(const std::filesystem::path& path) -> Result<SceneDocument> {
    auto json = readJsonFile(path);
    if (!json) {
        return std::unexpected(json.error());
    }
    return sceneFromJson(*json);
}

export [[nodiscard]] auto saveScene(const std::filesystem::path& path, const SceneDocument& scene)
    -> Result<void> {
    if (auto valid = validateScene(scene); !valid) {
        return valid;
    }
    return writeJsonFile(path, sceneToJson(scene));
}
} // namespace roboslop
