module;
#include <bgfx/bgfx.h>
#include <glm/vec3.hpp>

#include <array>
#include <cstdint>
#include <expected>
#include <map>
#include <string>
#include <vector>

export module roboslop.scene.runtime;
import roboslop.core.error;
import roboslop.ecs;
import roboslop.physics;
import roboslop.physics.components;
import roboslop.render.asset_cache;
import roboslop.render.mesh;
import roboslop.render.material;
import roboslop.render.primitives;
import roboslop.render.lighting;
import roboslop.scene.document;
import roboslop.scene.transform;

namespace roboslop {
export struct SceneIdentity {
    std::string id;
    std::string name;
};

// Owns shared primitive buffers and solid material textures. Lives in the
// world's context (destroyed before bgfx shutdown). Entity handles only borrow.
export class SceneRuntime {
  public:
    SceneRuntime() = default;
    SceneRuntime(const SceneRuntime&) = delete;
    auto operator=(const SceneRuntime&) -> SceneRuntime& = delete;

    ~SceneRuntime() {
        destroyTextures();
        for (const auto& [name, mesh] : meshes) {
            (void)name;
            bgfx::destroy(mesh.vb);
            bgfx::destroy(mesh.ib);
        }
    }

    auto clear(World& world) -> void {
        for (const auto entity : entities) {
            if (world.valid(entity)) {
                releasePhysicsBody(world, entity);
                world.destroy(entity);
            }
        }
        entities.clear();
        destroyTextures();
    }

    [[nodiscard]] auto
    replace(World& world, AssetCache& assets, const SceneDocument& document, bool simulate)
        -> Result<void> {
        if (auto valid = validateScene(document); !valid) {
            return valid;
        }
        auto program = assets.program("vs_scene", "fs_scene");
        if (!program) {
            return std::unexpected(program.error());
        }
        if (meshes.empty()) {
            meshes.emplace("cube", makeGeometryMesh(cubeGeometry()));
            meshes.emplace("sphere", makeGeometryMesh(sphereGeometry(24, 32, 0.5F)));
            meshes.emplace("plane", makeGeometryMesh(planeGeometry(1, 1)));
        }
        clear(world);
        const auto sampler = assets.sampler("s_albedo");
        for (const auto& [id, color] : document.materials) {
            const std::array<std::uint8_t, 4> pixel{
                static_cast<std::uint8_t>(color.x * 255),
                static_cast<std::uint8_t>(color.y * 255),
                static_cast<std::uint8_t>(color.z * 255),
                255
            };
            textures.emplace(
                id,
                bgfx::createTexture2D(
                    1,
                    1,
                    false,
                    1,
                    bgfx::TextureFormat::RGBA8,
                    0,
                    bgfx::copy(pixel.data(), static_cast<std::uint32_t>(pixel.size()))
                )
            );
        }
        for (const auto& object : document.objects) {
            const auto entity = world.create();
            entities.push_back(entity);
            world.emplace<SceneIdentity>(entity, SceneIdentity{object.id, object.name});
            world.emplace<Transform>(entity, object.transform);
            auto mesh = meshes.at(object.geometry);
            mesh.program = program->value;
            world.emplace<Mesh>(entity, mesh);
            world.emplace<Material>(
                entity,
                Material{
                    .program = *program, .albedo = textures.at(object.material), .sAlbedo = sampler
                }
            );
            if (simulate && object.body != "none") {
                BodyDesc body;
                body.motion = object.body == "static" ? BodyMotion::Static : BodyMotion::Dynamic;
                if (object.geometry == "sphere") {
                    body.shape = SphereShape{object.transform.scale.x * 0.5F};
                } else {
                    body.shape = BoxShape{object.transform.scale * 0.5F};
                }
                world.emplace<BodyDesc>(entity, body);
            }
        }
        const auto light = world.create();
        entities.push_back(light);
        world.emplace<DirectionalLight>(light, document.light);
        return {};
    }

  private:
    auto destroyTextures() -> void {
        for (const auto& [id, handle] : textures) {
            (void)id;
            bgfx::destroy(handle);
        }
        textures.clear();
    }

    std::vector<Entity> entities;
    std::map<std::string, Mesh> meshes;
    std::map<std::string, bgfx::TextureHandle> textures;
};
} // namespace roboslop
