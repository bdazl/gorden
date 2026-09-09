import editor.model;
import roboslop.scene.document;
#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

TEST_CASE("Editor undo restores a complete edit and a new edit invalidates redo", "[editor]") {
    editor::History history;
    auto before = history.document;
    history.document.objects.push_back({.id = "a"});
    history.checkpoint(before);
    before = history.document;
    history.document.objects[0].transform.position.x = 4;
    history.document.objects[0].transform.position.x = 8;
    history.checkpoint(before);
    REQUIRE(history.undo());
    REQUIRE(history.document.objects[0].transform.position.x == 0);
    REQUIRE(history.redo());
    REQUIRE(history.document.objects[0].transform.position.x == 8);
    REQUIRE(history.undo());
    before = history.document;
    history.document.objects.clear();
    history.checkpoint(before);
    REQUIRE_FALSE(history.redo());
    REQUIRE(history.undo());
    REQUIRE(history.document.objects[0].id == "a");
}

TEST_CASE("Picking returns nearest geometry and respects transformed bounds", "[editor]") {
    roboslop::SceneDocument scene;
    scene.objects.push_back({.id = "far", .transform = {.position = {0, 0, -5}}});
    scene.objects.push_back(
        {.id = "near", .transform = {.position = {0, 0, 0}, .scale = {2, 1, 1}}}
    );
    REQUIRE(editor::pickObject(scene, {0, 0, 5}, {0, 0, -1}) == "near");
    REQUIRE(editor::pickObject(scene, {0.8F, 0, 5}, {0, 0, -1}) == "near");
    REQUIRE(editor::pickObject(scene, {2, 0, 5}, {0, 0, -1}).empty());
    scene.objects[1].transform.rotation = glm::angleAxis(glm::radians(90.0F), glm::vec3(0, 0, 1));
    REQUIRE(editor::pickObject(scene, {0.8F, 0, 5}, {0, 0, -1}).empty());
}

TEST_CASE("Duplicate IDs can be generated after loading arbitrary object IDs", "[editor]") {
    roboslop::SceneDocument scene;
    scene.objects = {{.id = "object-1"}, {.id = "object-3"}};
    REQUIRE(editor::nextId(scene) == "object-2");
}

TEST_CASE("Picking hits models through their loaded bounds", "[editor]") {
    roboslop::SceneDocument scene;
    scene.objects.push_back(
        {.id = "crate",
         .geometry = "model",
         .model = "models/crate.glb",
         .transform = {.position = {0, 0, 0}, .scale = {2, 2, 2}}}
    );
    // Bounds off-centre, like a model whose origin sits on its floor.
    const editor::ModelBounds bounds{{"models/crate.glb", {.min = {-1, 0, -1}, .max = {1, 1, 1}}}};

    REQUIRE(editor::pickObject(scene, {0, 1, 5}, {0, 0, -1}).empty()); // unknown bounds
    REQUIRE(editor::pickObject(scene, {0, 1, 5}, {0, 0, -1}, bounds) == "crate");
    REQUIRE(editor::pickObject(scene, {0, -0.5F, 5}, {0, 0, -1}, bounds).empty());   // below
    REQUIRE(editor::pickObject(scene, {1.9F, 1, 5}, {0, 0, -1}, bounds) == "crate"); // scaled
    REQUIRE(editor::pickObject(scene, {2.1F, 1, 5}, {0, 0, -1}, bounds).empty());

    scene.objects.push_back({.id = "near", .transform = {.position = {0, 1, 3}}});
    REQUIRE(editor::pickObject(scene, {0, 1, 5}, {0, 0, -1}, bounds) == "near");

    scene.objects.pop_back();
    scene.objects[0].transform.rotation = glm::angleAxis(glm::radians(180.0F), glm::vec3(0, 0, 1));
    REQUIRE(editor::pickObject(scene, {0, 1, -5}, {0, 0, 1}, bounds).empty()); // now below
    REQUIRE(editor::pickObject(scene, {0, -1, -5}, {0, 0, 1}, bounds) == "crate");
}

TEST_CASE("Model listing recursively returns sorted paths relative to the asset root", "[editor]") {
    const auto root = std::filesystem::temp_directory_path() / "roboslop-editor-models-test";
    std::filesystem::remove_all(root);
    REQUIRE(editor::listModels(root).empty());
    std::filesystem::create_directories(root / "models" / "props");
    for (const auto* name : {"zebra.glb", "crate.glb", "crate.blend", "notes.txt"}) {
        std::ofstream{root / "models" / name} << "x";
    }
    for (const auto* name : {"chair.glb", "monitor.glb", "monitor.blend"}) {
        std::ofstream{root / "models" / "props" / name} << "x";
    }
    REQUIRE(
        editor::listModels(root) == std::vector<std::string>{
                                        "models/crate.glb",
                                        "models/props/chair.glb",
                                        "models/props/monitor.glb",
                                        "models/zebra.glb",
                                    }
    );
    std::filesystem::remove_all(root);
}
