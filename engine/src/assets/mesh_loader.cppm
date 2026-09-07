module;

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <glm/common.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

export module roboslop.assets.mesh;

import roboslop.core.error;

namespace roboslop {

export enum class MeshLoaderError : int {
    FileMissing = 1,
    AssimpFailed = 2,
    NoMeshes = 3,
    EmptyMesh = 4,
    UnsupportedTexture = 5,
};

export [[nodiscard]] auto toError(MeshLoaderError e, std::string ctx = {}) -> Error {
    switch (e) {
    case MeshLoaderError::FileMissing:
        return {
            .category = "roboslop.assets.mesh",
            .code = static_cast<int>(e),
            .message = "mesh file not found",
            .context = std::move(ctx),
        };
    case MeshLoaderError::AssimpFailed:
        return {
            .category = "roboslop.assets.mesh",
            .code = static_cast<int>(e),
            .message = "Assimp ReadFile failed",
            .context = std::move(ctx),
        };
    case MeshLoaderError::NoMeshes:
        return {
            .category = "roboslop.assets.mesh",
            .code = static_cast<int>(e),
            .message = "file contains no meshes",
            .context = std::move(ctx),
        };
    case MeshLoaderError::EmptyMesh:
        return {
            .category = "roboslop.assets.mesh",
            .code = static_cast<int>(e),
            .message = "mesh has no vertices or faces",
            .context = std::move(ctx),
        };
    case MeshLoaderError::UnsupportedTexture:
        return {
            .category = "roboslop.assets.mesh",
            .code = static_cast<int>(e),
            .message = "embedded texture is not an encoded image",
            .context = std::move(ctx),
        };
    }
    return {
        .category = "roboslop.assets.mesh",
        .code = 0,
        .message = "unknown MeshLoaderError",
        .context = std::move(ctx),
    };
}

// Interleaved Position + Normal + UV vertex. Matches
// vertexLayoutPosNormalUv() in roboslop.render.mesh; consumers can
// reinterpret this as bytes when building the bgfx vertex buffer.
export struct MeshVertex {
    float position[3];
    float normal[3];
    float uv[2];
};

// CPU-side mesh payload. Indices are 32-bit because imported models
// routinely exceed 65535 vertices; makeStaticMesh has a matching
// overload. Once the bgfx buffers exist the asset can be released.
export struct MeshAsset {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
};

// Base colour and, optionally, one base colour texture. A textured
// material sets exactly one of the two texture fields: `texturePath`
// is relative to the model file's directory, `textureData` holds an
// encoded image (PNG/JPEG) packed inside the model file, as .glb does.
export struct ModelMaterial {
    std::string name;
    glm::vec3 baseColor{1.0F, 1.0F, 1.0F};
    std::string texturePath;
    std::vector<std::byte> textureData;
};

// One drawable piece of a model: a node's mesh with the node's full
// transform. Authoring tools split a mesh per material, so one object
// can produce several parts that share a name.
export struct ModelPart {
    std::string name;
    glm::mat4 transform{1.0F};  // part space -> model space
    std::uint32_t material = 0; // index into ModelAsset::materials
    MeshAsset mesh;
};

export struct ModelAsset {
    std::vector<ModelMaterial> materials;
    std::vector<ModelPart> parts;
};

export struct Aabb {
    glm::vec3 min{0.0F};
    glm::vec3 max{0.0F};
};

// Model-space bounds over every part with its transform applied. An
// empty model yields a zero box at the origin. Scene picking and
// bounds-derived colliders both start from this.
export [[nodiscard]] auto modelBounds(const ModelAsset& model) -> Aabb {
    Aabb box;
    bool first = true;
    for (const auto& part : model.parts) {
        for (const auto& v : part.mesh.vertices) {
            const auto p = glm::vec3(
                part.transform * glm::vec4(v.position[0], v.position[1], v.position[2], 1)
            );
            if (first) {
                box.min = box.max = p;
                first = false;
            } else {
                box.min = glm::min(box.min, p);
                box.max = glm::max(box.max, p);
            }
        }
    }
    return box;
}

namespace detail {

// Assimp matrices are row-major; glm's are column-major.
static auto toGlm(const aiMatrix4x4& m) -> glm::mat4 {
    return glm::mat4{
        m.a1,
        m.b1,
        m.c1,
        m.d1,
        m.a2,
        m.b2,
        m.c2,
        m.d2,
        m.a3,
        m.b3,
        m.c3,
        m.d3,
        m.a4,
        m.b4,
        m.c4,
        m.d4,
    };
}

static auto convertMesh(const aiMesh& mesh, const std::string& context) -> Result<MeshAsset> {
    if (mesh.mNumVertices == 0U || mesh.mNumFaces == 0U) {
        return std::unexpected(toError(MeshLoaderError::EmptyMesh, context));
    }
    MeshAsset out;
    out.vertices.resize(mesh.mNumVertices);
    for (unsigned i = 0; i < mesh.mNumVertices; ++i) {
        const auto& p = mesh.mVertices[i];
        out.vertices[i].position[0] = p.x;
        out.vertices[i].position[1] = p.y;
        out.vertices[i].position[2] = p.z;
        if (mesh.HasNormals()) {
            const auto& n = mesh.mNormals[i];
            out.vertices[i].normal[0] = n.x;
            out.vertices[i].normal[1] = n.y;
            out.vertices[i].normal[2] = n.z;
        } else {
            out.vertices[i].normal[0] = 0.0F;
            out.vertices[i].normal[1] = 1.0F;
            out.vertices[i].normal[2] = 0.0F;
        }
        if (mesh.HasTextureCoords(0)) {
            const auto& uv = mesh.mTextureCoords[0][i];
            out.vertices[i].uv[0] = uv.x;
            out.vertices[i].uv[1] = uv.y;
        } else {
            out.vertices[i].uv[0] = 0.0F;
            out.vertices[i].uv[1] = 0.0F;
        }
    }
    out.indices.reserve(static_cast<std::size_t>(mesh.mNumFaces) * 3U);
    for (unsigned f = 0; f < mesh.mNumFaces; ++f) {
        const auto& face = mesh.mFaces[f];
        if (face.mNumIndices != 3) {
            continue; // Triangulate should have ensured this, but skip just in case.
        }
        out.indices.push_back(face.mIndices[0]);
        out.indices.push_back(face.mIndices[1]);
        out.indices.push_back(face.mIndices[2]);
    }
    return out;
}

static auto
convertMaterial(const aiScene& scene, const aiMaterial& material, const std::string& context)
    -> Result<ModelMaterial> {
    ModelMaterial out;
    aiString name;
    if (material.Get(AI_MATKEY_NAME, name) == AI_SUCCESS) {
        out.name = name.C_Str();
    }
    aiColor4D color;
    if (material.Get(AI_MATKEY_BASE_COLOR, color) == AI_SUCCESS ||
        material.Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS) {
        out.baseColor = {color.r, color.g, color.b};
    }
    aiString path;
    if (material.GetTexture(aiTextureType_BASE_COLOR, 0, &path) != AI_SUCCESS &&
        material.GetTexture(aiTextureType_DIFFUSE, 0, &path) != AI_SUCCESS) {
        return out;
    }
    if (const aiTexture* embedded = scene.GetEmbeddedTexture(path.C_Str()); embedded != nullptr) {
        // mHeight == 0 marks an encoded image whose byte size is mWidth.
        if (embedded->mHeight != 0) {
            return std::unexpected(
                toError(MeshLoaderError::UnsupportedTexture, context + ": " + out.name)
            );
        }
        const auto* bytes = reinterpret_cast<const std::byte*>(embedded->pcData);
        out.textureData.assign(bytes, bytes + embedded->mWidth);
    } else {
        out.texturePath = path.C_Str();
    }
    return out;
}

static auto collectParts(
    const aiScene& scene,
    const aiNode& node,
    const aiMatrix4x4& parent,
    const std::string& context,
    std::vector<ModelPart>& parts
) -> Result<void> {
    const aiMatrix4x4 world = parent * node.mTransformation;
    for (unsigned i = 0; i < node.mNumMeshes; ++i) {
        const aiMesh& mesh = *scene.mMeshes[node.mMeshes[i]];
        auto converted = convertMesh(mesh, context + ": " + node.mName.C_Str());
        if (!converted) {
            return std::unexpected(converted.error());
        }
        parts.push_back(
            {.name = node.mName.C_Str(),
             .transform = toGlm(world),
             .material = mesh.mMaterialIndex,
             .mesh = std::move(*converted)}
        );
    }
    for (unsigned c = 0; c < node.mNumChildren; ++c) {
        if (auto r = collectParts(scene, *node.mChildren[c], world, context, parts); !r) {
            return r;
        }
    }
    return {};
}

} // namespace detail

// Loads every mesh in an Assimp-supported file (.glb/.gltf is the
// authored format, see docs/models.md) with its node transform and
// material. The postprocess pipeline triangulates, generates smooth
// normals when missing, flips UVs so the file's top-left origin
// survives Assimp's bottom-left convention (what bgfx and stb_image
// expect), computes tangent space, and merges duplicate vertices.
export [[nodiscard]] auto loadModelFile(const std::filesystem::path& path) -> Result<ModelAsset> {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return std::unexpected(toError(MeshLoaderError::FileMissing, path.string()));
    }

    Assimp::Importer importer;
    constexpr unsigned int Flags = aiProcess_Triangulate | aiProcess_GenSmoothNormals |
                                   aiProcess_FlipUVs | aiProcess_CalcTangentSpace |
                                   aiProcess_JoinIdenticalVertices;

    const aiScene* scene = importer.ReadFile(path.string(), Flags);
    if (scene == nullptr || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0U ||
        scene->mRootNode == nullptr) {
        std::string ctx = path.string();
        if (const char* err = importer.GetErrorString(); err != nullptr && err[0] != '\0') {
            ctx += ": ";
            ctx += err;
        }
        return std::unexpected(toError(MeshLoaderError::AssimpFailed, std::move(ctx)));
    }
    if (scene->mNumMeshes == 0U) {
        return std::unexpected(toError(MeshLoaderError::NoMeshes, path.string()));
    }

    ModelAsset out;
    out.materials.reserve(scene->mNumMaterials);
    for (unsigned i = 0; i < scene->mNumMaterials; ++i) {
        auto material = detail::convertMaterial(*scene, *scene->mMaterials[i], path.string());
        if (!material) {
            return std::unexpected(material.error());
        }
        out.materials.push_back(std::move(*material));
    }
    if (auto r = detail::collectParts(
            *scene, *scene->mRootNode, aiMatrix4x4{}, path.string(), out.parts
        );
        !r) {
        return std::unexpected(r.error());
    }
    if (out.parts.empty()) {
        return std::unexpected(toError(MeshLoaderError::NoMeshes, path.string()));
    }
    return out;
}

} // namespace roboslop
