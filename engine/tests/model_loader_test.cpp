import roboslop.assets.mesh;

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/common.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <ranges>
#include <string>
#include <vector>

namespace {

const std::filesystem::path kAssets{ROBOSLOP_TEST_ASSETS};

auto partsNamed(const roboslop::ModelAsset& model, std::string_view name)
    -> std::vector<const roboslop::ModelPart*> {
    std::vector<const roboslop::ModelPart*> out;
    for (const auto& part : model.parts) {
        if (part.name == name) {
            out.push_back(&part);
        }
    }
    return out;
}

auto modelSpace(const roboslop::ModelPart& part, const roboslop::MeshVertex& v) -> glm::vec3 {
    return glm::vec3(part.transform * glm::vec4(v.position[0], v.position[1], v.position[2], 1));
}

// Writes a .glb holding one indexed grid of `side` x `side` vertices.
auto writeGridGlb(const std::filesystem::path& file, std::uint32_t side) -> void {
    std::vector<float> positions;
    std::vector<std::uint32_t> indices;
    for (std::uint32_t y = 0; y < side; ++y) {
        for (std::uint32_t x = 0; x < side; ++x) {
            positions.insert(positions.end(), {float(x), float(y), 0.0F});
            if (x + 1 < side && y + 1 < side) {
                const auto i = y * side + x;
                indices.insert(indices.end(), {i, i + 1, i + side, i + 1, i + side + 1, i + side});
            }
        }
    }
    const auto posBytes = static_cast<std::uint32_t>(positions.size() * sizeof(float));
    const auto idxBytes = static_cast<std::uint32_t>(indices.size() * sizeof(std::uint32_t));
    const nlohmann::json doc = {
        {"asset", {{"version", "2.0"}}},
        {"scene", 0},
        {"scenes", {{{"nodes", {0}}}}},
        {"nodes", {{{"mesh", 0}, {"name", "Grid"}}}},
        {"meshes", {{{"primitives", {{{"attributes", {{"POSITION", 0}}}, {"indices", 1}}}}}}},
        {"accessors",
         {{{"bufferView", 0},
           {"componentType", 5126},
           {"count", side * side},
           {"type", "VEC3"},
           {"min", {0, 0, 0}},
           {"max", {side - 1, side - 1, 0}}},
          {{"bufferView", 1},
           {"componentType", 5125},
           {"count", indices.size()},
           {"type", "SCALAR"}}}},
        {"bufferViews",
         {{{"buffer", 0}, {"byteOffset", 0}, {"byteLength", posBytes}},
          {{"buffer", 0}, {"byteOffset", posBytes}, {"byteLength", idxBytes}}}},
        {"buffers", {{{"byteLength", posBytes + idxBytes}}}},
    };
    std::string json = doc.dump();
    json.append((4 - json.size() % 4) % 4, ' ');
    const auto binLength = posBytes + idxBytes;
    const auto binPadded = binLength + (4 - binLength % 4) % 4;
    const auto write32 = [](std::ofstream& out, std::uint32_t v) {
        out.write(reinterpret_cast<const char*>(&v), sizeof v);
    };
    std::ofstream out(file, std::ios::binary);
    write32(out, 0x46546C67U); // "glTF"
    write32(out, 2);
    write32(out, static_cast<std::uint32_t>(12 + 8 + json.size() + 8 + binPadded));
    write32(out, static_cast<std::uint32_t>(json.size()));
    write32(out, 0x4E4F534AU); // JSON
    out.write(json.data(), static_cast<std::streamsize>(json.size()));
    write32(out, binPadded);
    write32(out, 0x004E4942U); // BIN
    out.write(reinterpret_cast<const char*>(positions.data()), posBytes);
    out.write(reinterpret_cast<const char*>(indices.data()), idxBytes);
    for (std::uint32_t i = binLength; i < binPadded; ++i) {
        out.put('\0');
    }
}

auto tempDir() -> std::filesystem::path {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto dir =
        std::filesystem::temp_directory_path() / ("roboslop-model-" + std::to_string(unique));
    std::filesystem::create_directories(dir);
    return dir;
}

} // namespace

TEST_CASE(
    "A Blender glb loads every part with its transform, material and packed texture",
    "[assets][model]"
) {
    const auto model = roboslop::loadModelFile(kAssets / "crate.glb");
    REQUIRE(model);
    REQUIRE(model->parts.size() == 4);

    // Blender's +Z up became glTF's +Y up: the ball sits at (3, 0, 0.5) in Blender.
    const auto ball = partsNamed(*model, "Ball");
    REQUIRE(ball.size() == 1);
    CHECK(ball[0]->transform[3][0] == Catch::Approx(3.0F));
    CHECK(ball[0]->transform[3][1] == Catch::Approx(0.5F));
    CHECK(ball[0]->transform[3][2] == Catch::Approx(0.0F).margin(1e-6));
    // The Subsurf modifier was applied on export (12 vertices otherwise).
    CHECK(ball[0]->mesh.vertices.size() > 60);

    // One Blender object with two materials becomes two parts sharing the node.
    const auto crate = partsNamed(*model, "Crate");
    REQUIRE(crate.size() == 2);
    const auto top = std::ranges::find_if(crate, [&](const auto* p) {
        return model->materials.at(p->material).name == "Blue";
    });
    REQUIRE(top != crate.end());
    CHECK((*top)->mesh.vertices.size() == 4);
    CHECK(model->materials.at((*top)->material).baseColor.z == Catch::Approx(0.9F));
    // Node scale (2, 1, 2) and translation stay on the node: the unit cube's
    // corner (0.5, 0.5, 0.5) lands at (1, 1, 1) in model space.
    CHECK(glm::vec3((*top)->transform * glm::vec4(0.5F, 0.5F, 0.5F, 1)) == glm::vec3(1, 1, 1));

    // The sign is parented to the crate with its own rotation and scale:
    // a 2 x 1 upright plane centred 2.5 m up, thin along Z.
    const auto sign = partsNamed(*model, "Sign");
    REQUIRE(sign.size() == 1);
    glm::vec3 lo{1e9F};
    glm::vec3 hi{-1e9F};
    for (const auto& v : sign[0]->mesh.vertices) {
        const auto p = modelSpace(*sign[0], v);
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
    CHECK(lo.x == Catch::Approx(-1.0F));
    CHECK(hi.x == Catch::Approx(1.0F));
    CHECK(lo.y == Catch::Approx(2.0F));
    CHECK(hi.y == Catch::Approx(3.0F));
    CHECK(std::abs(lo.z) < 1e-5F);
    CHECK(std::abs(hi.z) < 1e-5F);

    // Packed PNG comes back as encoded bytes; UVs keep glTF's top-left
    // origin (the local corner at x=-0.5, z=+0.5 has uv (0, 1) in the file).
    const auto& checker = model->materials.at(sign[0]->material);
    CHECK(checker.name == "Checker");
    CHECK(checker.texturePath.empty());
    REQUIRE(checker.textureData.size() > 8);
    CHECK(std::memcmp(checker.textureData.data(), "\x89PNG", 4) == 0);
    const auto corner = std::ranges::find_if(sign[0]->mesh.vertices, [](const auto& v) {
        return v.position[0] < 0 && v.position[2] > 0;
    });
    REQUIRE(corner != sign[0]->mesh.vertices.end());
    CHECK(corner->uv[0] == Catch::Approx(0.0F));
    CHECK(corner->uv[1] == Catch::Approx(1.0F));
}

TEST_CASE("Model bounds cover every transformed part", "[assets][model]") {
    REQUIRE(roboslop::modelBounds({}).max == glm::vec3{0.0F});

    const auto model = roboslop::loadModelFile(kAssets / "crate.glb");
    REQUIRE(model);
    const auto bounds = roboslop::modelBounds(*model);
    glm::vec3 lo{std::numeric_limits<float>::max()};
    glm::vec3 hi{std::numeric_limits<float>::lowest()};
    for (const auto& part : model->parts) {
        for (const auto& v : part.mesh.vertices) {
            lo = glm::min(lo, modelSpace(part, v));
            hi = glm::max(hi, modelSpace(part, v));
        }
    }
    REQUIRE(bounds.min == lo);
    REQUIRE(bounds.max == hi);
    // The ball sits at x = 3 and the crate rests on y = 0, so the box is
    // neither centred nor symmetric.
    REQUIRE(bounds.min.x == Catch::Approx(-1.0F));
    REQUIRE(bounds.max.x > 3.0F);
    REQUIRE(bounds.min.y == Catch::Approx(0.0F));
    REQUIRE(bounds.max.y > 2.0F);
}

TEST_CASE("Models with more than 65535 vertices keep 32-bit indices", "[assets][model]") {
    const auto dir = tempDir();
    const auto file = dir / "grid.glb";
    constexpr std::uint32_t kSide = 260; // 67600 vertices
    writeGridGlb(file, kSide);
    const auto model = roboslop::loadModelFile(file);
    REQUIRE(model);
    REQUIRE(model->parts.size() == 1);
    CHECK(model->parts[0].mesh.vertices.size() == kSide * kSide);
    CHECK(std::ranges::max(model->parts[0].mesh.indices) == kSide * kSide - 1);
    std::filesystem::remove_all(dir);
}

TEST_CASE("Model loading reports missing and unreadable files as values", "[assets][model]") {
    const auto missing = roboslop::loadModelFile(kAssets / "does-not-exist.glb");
    REQUIRE_FALSE(missing);
    CHECK(missing.error().code == static_cast<int>(roboslop::MeshLoaderError::FileMissing));

    const auto dir = tempDir();
    const auto bogus = dir / "bogus.glb";
    std::ofstream(bogus) << "not a model";
    const auto unreadable = roboslop::loadModelFile(bogus);
    REQUIRE_FALSE(unreadable);
    CHECK(unreadable.error().code == static_cast<int>(roboslop::MeshLoaderError::AssimpFailed));
    std::filesystem::remove_all(dir);
}
