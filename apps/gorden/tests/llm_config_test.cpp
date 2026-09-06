import gorden.llm_config;

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

TEST_CASE("parseLlmConfig reads the known keys and ignores the rest", "[gorden][llm_config]") {
    const auto c = gorden::parseLlmConfig(
        nlohmann::json::parse(
            R"({"apiKey": "sk-x", "model": "m", "baseUrl": "http://h/v1", "extra": 1})"
        )
    );
    REQUIRE(c.apiKey == "sk-x");
    REQUIRE(c.model == "m");
    REQUIRE(c.baseUrl == "http://h/v1");
}

TEST_CASE(
    "parseLlmConfig keeps empty values for missing or malformed keys", "[gorden][llm_config]"
) {
    const auto c = gorden::parseLlmConfig(nlohmann::json::parse(R"({"apiKey": 42})"));
    REQUIRE(c.apiKey.empty());
    REQUIRE(c.model.empty());
    REQUIRE(gorden::parseLlmConfig(nlohmann::json::array()).baseUrl.empty());
}

TEST_CASE(
    "loadLlmConfig: missing file is empty, corrupt file is an error", "[gorden][llm_config]"
) {
    const auto dir = std::filesystem::temp_directory_path() / "roboslop-gorden-llm-config";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto path = dir / "llm.json";

    const auto missing = gorden::loadLlmConfig(path);
    REQUIRE(missing.has_value());
    REQUIRE(missing->apiKey.empty());

    std::ofstream(path) << R"({"apiKey": "sk-file"})";
    const auto loaded = gorden::loadLlmConfig(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->apiKey == "sk-file");

    std::ofstream(path) << "{ not json";
    REQUIRE_FALSE(gorden::loadLlmConfig(path).has_value());
    std::filesystem::remove_all(dir);
}
