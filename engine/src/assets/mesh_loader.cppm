module;

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <bgfx/bgfx.h>

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
            .message = "scene contains no meshes",
            .context = std::move(ctx),
        };
    case MeshLoaderError::EmptyMesh:
        return {
            .category = "roboslop.assets.mesh",
            .code = static_cast<int>(e),
            .message = "mesh has zero vertices or indices",
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

// CPU-side mesh payload. The MeshAsset owns the vertex and index byte
// vectors; consumers copy them into bgfx static buffers via
// makeStaticMeshFromAsset() (in roboslop.render.mesh). Once the bgfx
// buffers exist, the asset can be released — its bytes are not the
// authoritative storage.
export struct MeshAsset {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint16_t> indices;
};

// Loads the first mesh from any Assimp-supported file
// (.obj, .gltf, .glb, .fbx, ...). The default postprocess pipeline
// triangulates, generates smooth normals when missing, flips UVs for
// bgfx's bottom-left origin, and computes tangent space (cheap; will
// be useful as soon as normal-mapped materials land).
export [[nodiscard]] auto loadMeshFile(const std::filesystem::path& path) -> Result<MeshAsset> {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return std::unexpected(toError(MeshLoaderError::FileMissing, path.string()));
    }

    Assimp::Importer importer;
    constexpr unsigned int kFlags = aiProcess_Triangulate | aiProcess_GenSmoothNormals |
                                    aiProcess_FlipUVs | aiProcess_CalcTangentSpace |
                                    aiProcess_JoinIdenticalVertices;

    const aiScene* scene = importer.ReadFile(path.string(), kFlags);
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

    // MVP loads only the first mesh; multi-mesh scenes will get a
    // proper hierarchy walk when the asset pipeline grows.
    const aiMesh* mesh = scene->mMeshes[0];
    if (mesh->mNumVertices == 0U || mesh->mNumFaces == 0U) {
        return std::unexpected(toError(MeshLoaderError::EmptyMesh, path.string()));
    }

    MeshAsset out;
    out.vertices.resize(mesh->mNumVertices);
    for (unsigned i = 0; i < mesh->mNumVertices; ++i) {
        const auto& p = mesh->mVertices[i];
        out.vertices[i].position[0] = p.x;
        out.vertices[i].position[1] = p.y;
        out.vertices[i].position[2] = p.z;
        if (mesh->HasNormals()) {
            const auto& n = mesh->mNormals[i];
            out.vertices[i].normal[0] = n.x;
            out.vertices[i].normal[1] = n.y;
            out.vertices[i].normal[2] = n.z;
        } else {
            out.vertices[i].normal[0] = 0.0F;
            out.vertices[i].normal[1] = 1.0F;
            out.vertices[i].normal[2] = 0.0F;
        }
        if (mesh->HasTextureCoords(0)) {
            const auto& uv = mesh->mTextureCoords[0][i];
            out.vertices[i].uv[0] = uv.x;
            out.vertices[i].uv[1] = uv.y;
        } else {
            out.vertices[i].uv[0] = 0.0F;
            out.vertices[i].uv[1] = 0.0F;
        }
    }

    out.indices.reserve(static_cast<std::size_t>(mesh->mNumFaces) * 3U);
    for (unsigned f = 0; f < mesh->mNumFaces; ++f) {
        const auto& face = mesh->mFaces[f];
        if (face.mNumIndices != 3) {
            continue; // Triangulate should have ensured this, but skip just in case.
        }
        out.indices.push_back(static_cast<std::uint16_t>(face.mIndices[0]));
        out.indices.push_back(static_cast<std::uint16_t>(face.mIndices[1]));
        out.indices.push_back(static_cast<std::uint16_t>(face.mIndices[2]));
    }
    return out;
}

} // namespace roboslop
