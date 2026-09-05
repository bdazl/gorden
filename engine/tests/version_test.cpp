import roboslop.core.version;

#include <catch2/catch_test_macros.hpp>

#include <cctype>

TEST_CASE("version string is non-empty and starts with a digit", "[core]") {
    const auto v = roboslop::version();
    REQUIRE(!v.empty());
    REQUIRE(std::isdigit(static_cast<unsigned char>(v.front())) != 0);
}
