import roboslop.core.file;
import roboslop.core.json_file;

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace {

auto scratch(const char* name) -> std::filesystem::path {
    const auto dir = std::filesystem::temp_directory_path() / "roboslop-json-test";
    std::filesystem::create_directories(dir);
    return dir / name;
}

} // namespace

TEST_CASE("writeJsonFile then readJsonFile round-trips and creates parents", "[core][json]") {
    const auto path = scratch("nested/settings.json");
    std::filesystem::remove_all(path.parent_path());

    const nlohmann::json doc{{"playerName", "Anna"}, {"count", 3}};
    REQUIRE(roboslop::writeJsonFile(path, doc).has_value());
    REQUIRE_FALSE(std::filesystem::exists(path.string() + ".tmp"));

    const auto back = roboslop::readJsonFile(path);
    REQUIRE(back.has_value());
    REQUIRE((*back)["playerName"] == "Anna");
    REQUIRE((*back)["count"] == 3);
}

TEST_CASE("readJsonFile distinguishes missing from corrupt", "[core][json]") {
    const auto missing = roboslop::readJsonFile(scratch("does-not-exist.json"));
    REQUIRE_FALSE(missing.has_value());
    REQUIRE(missing.error().code == static_cast<int>(roboslop::FileError::Missing));

    const auto corrupt = scratch("corrupt.json");
    {
        std::ofstream out(corrupt);
        out << "{ not json";
    }
    const auto r = roboslop::readJsonFile(corrupt);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == static_cast<int>(roboslop::JsonError::Parse));
}
