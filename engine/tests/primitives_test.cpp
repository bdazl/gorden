import roboslop.render.primitives;

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>

TEST_CASE("All primitive triangles agree with outward vertex normals", "[render][primitives]") {
    for (const auto& g :
         {roboslop::cubeGeometry(),
          roboslop::sphereGeometry(8, 16, 0.5F),
          roboslop::planeGeometry(2, 2)}) {
        for (std::size_t i = 0; i < g.indices.size(); i += 3) {
            const auto& a = g.vertices.at(g.indices[i]);
            const auto& b = g.vertices.at(g.indices[i + 1]);
            const auto& c = g.vertices.at(g.indices[i + 2]);
            const glm::vec3 pa{a.position[0], a.position[1], a.position[2]};
            const glm::vec3 pb{b.position[0], b.position[1], b.position[2]};
            const glm::vec3 pc{c.position[0], c.position[1], c.position[2]};
            const auto cross = glm::cross(pb - pa, pc - pa);
            if (glm::dot(cross, cross) < 1e-12F) {
                continue;
            } // Sphere poles.
            REQUIRE(glm::dot(cross, glm::vec3(a.normal[0], a.normal[1], a.normal[2])) > 0);
        }
    }
}

namespace {

auto length(std::span<const float, 3> v) -> float {
    return std::sqrt((v[0] * v[0]) + (v[1] * v[1]) + (v[2] * v[2]));
}

} // namespace

TEST_CASE("sphereGeometry has the expected vertex and index counts", "[render][primitives]") {
    const auto g = roboslop::sphereGeometry(8, 16, 0.5F);
    REQUIRE(g.vertices.size() == std::size_t{8 + 1} * (16 + 1));
    REQUIRE(g.indices.size() == std::size_t{8} * 16 * 6);
}

TEST_CASE("sphereGeometry indices stay in range", "[render][primitives]") {
    const auto g = roboslop::sphereGeometry(4, 6, 1.0F);
    for (const auto i : g.indices) {
        REQUIRE(i < g.vertices.size());
    }
}

TEST_CASE("sphereGeometry vertices lie on the sphere with unit normals", "[render][primitives]") {
    const float radius = 2.0F;
    const auto g = roboslop::sphereGeometry(6, 12, radius);
    for (const auto& v : g.vertices) {
        REQUIRE(length(v.position) == Catch::Approx(radius).margin(1e-4F));
        REQUIRE(length(v.normal) == Catch::Approx(1.0F).margin(1e-4F));
        REQUIRE(v.uv[0] >= 0.0F);
        REQUIRE(v.uv[0] <= 1.0F);
        REQUIRE(v.uv[1] >= 0.0F);
        REQUIRE(v.uv[1] <= 1.0F);
    }
}

TEST_CASE("sphereGeometry clamps degenerate resolutions", "[render][primitives]") {
    const auto g = roboslop::sphereGeometry(0, 0, 1.0F);
    REQUIRE(g.vertices.size() == std::size_t{2 + 1} * (3 + 1));
    REQUIRE(g.indices.size() == std::size_t{2} * 3 * 6);
}

TEST_CASE("planeGeometry spans the requested size with +Y normals", "[render][primitives]") {
    const auto g = roboslop::planeGeometry(4.0F, 2);
    REQUIRE(g.vertices.size() == 9);
    REQUIRE(g.indices.size() == std::size_t{2} * 2 * 6);

    float minX = 1e9F;
    float maxX = -1e9F;
    float minZ = 1e9F;
    float maxZ = -1e9F;
    for (const auto& v : g.vertices) {
        REQUIRE(v.position[1] == Catch::Approx(0.0F));
        REQUIRE(v.normal[0] == Catch::Approx(0.0F));
        REQUIRE(v.normal[1] == Catch::Approx(1.0F));
        REQUIRE(v.normal[2] == Catch::Approx(0.0F));
        minX = std::min(minX, v.position[0]);
        maxX = std::max(maxX, v.position[0]);
        minZ = std::min(minZ, v.position[2]);
        maxZ = std::max(maxZ, v.position[2]);
    }
    REQUIRE(minX == Catch::Approx(-2.0F));
    REQUIRE(maxX == Catch::Approx(2.0F));
    REQUIRE(minZ == Catch::Approx(-2.0F));
    REQUIRE(maxZ == Catch::Approx(2.0F));

    for (const auto i : g.indices) {
        REQUIRE(i < g.vertices.size());
    }
}
