module;

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <vector>

export module roboslop.render.primitives;

import roboslop.assets.mesh;
import roboslop.render.mesh;

namespace roboslop {

// CPU-side procedural geometry in the pos/normal/uv layout the textured
// and lab shaders consume. Pure data so the generators are unit-testable
// without a bgfx device; makeGeometryMesh uploads it.
export struct Geometry {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint16_t> indices;
};

// UV sphere centred at the origin. `rings` latitude bands (>= 2),
// `segments` longitude slices (>= 3). Vertices per ring are duplicated
// at the seam so UVs wrap cleanly; normals point outward. Index count is
// rings * segments * 6, which stays under the 16-bit limit for any
// reasonable lab sphere (e.g. 32 x 64 → 12 288 indices).
export [[nodiscard]] auto sphereGeometry(std::uint16_t rings, std::uint16_t segments, float radius)
    -> Geometry {
    Geometry g;
    const std::size_t r = rings < 2 ? 2 : rings;
    const std::size_t s = segments < 3 ? 3 : segments;
    g.vertices.reserve((r + 1) * (s + 1));
    g.indices.reserve(r * s * 6);

    for (std::size_t y = 0; y <= r; ++y) {
        const float v = static_cast<float>(y) / static_cast<float>(r);
        const float phi = v * std::numbers::pi_v<float>; // 0 at the +Y pole
        const float sinPhi = std::sin(phi);
        const float cosPhi = std::cos(phi);
        for (std::size_t x = 0; x <= s; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(s);
            const float theta = u * 2.0F * std::numbers::pi_v<float>;
            const float nx = sinPhi * std::cos(theta);
            const float ny = cosPhi;
            const float nz = sinPhi * std::sin(theta);
            g.vertices.push_back(
                MeshVertex{
                    .position = {nx * radius, ny * radius, nz * radius},
                    .normal = {nx, ny, nz},
                    .uv = {u, 1.0F - v},
                }
            );
        }
    }

    const auto stride = static_cast<std::uint16_t>(s + 1);
    for (std::size_t y = 0; y < r; ++y) {
        for (std::size_t x = 0; x < s; ++x) {
            const auto a = static_cast<std::uint16_t>(y * stride + x);
            const auto b = static_cast<std::uint16_t>(a + stride);
            // Counter-clockwise when viewed from outside (bgfx default
            // culls CW).
            g.indices.push_back(a);
            g.indices.push_back(static_cast<std::uint16_t>(a + 1));
            g.indices.push_back(b);
            g.indices.push_back(static_cast<std::uint16_t>(a + 1));
            g.indices.push_back(static_cast<std::uint16_t>(b + 1));
            g.indices.push_back(b);
        }
    }
    return g;
}

// Square plane in the XZ plane at y = 0, normal +Y, `size` on a side,
// `subdivisions` cells per side (>= 1). UV spans [0, 1] across the plane.
export [[nodiscard]] auto planeGeometry(float size, std::uint16_t subdivisions) -> Geometry {
    Geometry g;
    const std::size_t n = subdivisions < 1 ? 1 : subdivisions;
    g.vertices.reserve((n + 1) * (n + 1));
    g.indices.reserve(n * n * 6);

    const float half = size * 0.5F;
    for (std::size_t z = 0; z <= n; ++z) {
        const float v = static_cast<float>(z) / static_cast<float>(n);
        for (std::size_t x = 0; x <= n; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(n);
            g.vertices.push_back(
                MeshVertex{
                    .position = {-half + u * size, 0.0F, -half + v * size},
                    .normal = {0.0F, 1.0F, 0.0F},
                    .uv = {u, v},
                }
            );
        }
    }

    const auto stride = static_cast<std::uint16_t>(n + 1);
    for (std::size_t z = 0; z < n; ++z) {
        for (std::size_t x = 0; x < n; ++x) {
            const auto a = static_cast<std::uint16_t>(z * stride + x);
            const auto b = static_cast<std::uint16_t>(a + stride);
            // Counter-clockwise when viewed from +Y.
            g.indices.push_back(a);
            g.indices.push_back(b);
            g.indices.push_back(static_cast<std::uint16_t>(a + 1));
            g.indices.push_back(static_cast<std::uint16_t>(a + 1));
            g.indices.push_back(b);
            g.indices.push_back(static_cast<std::uint16_t>(b + 1));
        }
    }
    return g;
}

// Uploads a Geometry as static bgfx buffers using the pos/normal/uv
// layout. Caller fills in Mesh.program.
export [[nodiscard]] auto makeGeometryMesh(const Geometry& g) -> Mesh {
    return makeStaticMesh(
        std::as_bytes(std::span{g.vertices}), std::span{g.indices}, vertexLayoutPosNormalUv()
    );
}

} // namespace roboslop
