import roboslop.assets.shader_compiler;

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace {

auto indexOf(const std::vector<std::string>& v, const std::string& s) -> long {
    const auto it = std::ranges::find(v, s);
    return it == v.end() ? -1 : static_cast<long>(it - v.begin());
}

// The argument following `flag`, or "" when the flag is absent.
auto valueAfter(const std::vector<std::string>& v, const std::string& flag) -> std::string {
    const auto i = indexOf(v, flag);
    if (i < 0) {
        return {};
    }
    return v.at(static_cast<std::size_t>(i) + 1);
}

} // namespace

TEST_CASE("shadercArguments mirrors the CMake shaderc command line", "[assets][shader]") {
    const roboslop::ShaderCompileRequest req{
        .source = "/src/fs_lab.sc",
        .varyingDef = "/src/varying.def.sc",
        .stage = roboslop::ShaderStage::Fragment,
        .profile = "spirv",
        .includeDirs = {"/bgfx/src", "/src"},
        .outputFile = "/out/fs_lab.bin",
    };
    const auto args = roboslop::shadercArguments(req);

    REQUIRE(valueAfter(args, "-f") == "/src/fs_lab.sc");

    REQUIRE(valueAfter(args, "-o") == "/out/fs_lab.bin");

    REQUIRE(valueAfter(args, "--type") == "fragment");

    REQUIRE(valueAfter(args, "--profile") == "spirv");

    REQUIRE(valueAfter(args, "--varyingdef") == "/src/varying.def.sc");

    REQUIRE(valueAfter(args, "--platform") == roboslop::shadercPlatform());

    // Two include dirs → two -i flags, in order.
    REQUIRE(std::ranges::count(args, "-i") == 2);
    const auto i = static_cast<std::size_t>(indexOf(args, "-i"));
    REQUIRE(args.at(i + 1) == "/bgfx/src");
    REQUIRE(args.at(i + 3) == "/src");

    REQUIRE(indexOf(args, "--debug") == -1);
}

TEST_CASE("shadercArguments adds --debug on request", "[assets][shader]") {
    const roboslop::ShaderCompileRequest req{
        .source = "/src/vs.sc",
        .varyingDef = "/src/varying.def.sc",
        .stage = roboslop::ShaderStage::Vertex,
        .profile = "430",
        .includeDirs = {},
        .outputFile = "/out/vs.bin",
        .debug = true,
    };
    const auto args = roboslop::shadercArguments(req);
    REQUIRE(indexOf(args, "--debug") >= 0);
    REQUIRE(valueAfter(args, "--type") == "vertex");
}

TEST_CASE("shadercStageName covers every stage", "[assets][shader]") {
    REQUIRE(roboslop::shadercStageName(roboslop::ShaderStage::Vertex) == "vertex");
    REQUIRE(roboslop::shadercStageName(roboslop::ShaderStage::Fragment) == "fragment");
    REQUIRE(roboslop::shadercStageName(roboslop::ShaderStage::Compute) == "compute");
}
