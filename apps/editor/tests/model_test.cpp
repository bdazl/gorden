import editor.model;
import roboslop.scene.document;
#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

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
