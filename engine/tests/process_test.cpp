import roboslop.platform.process;

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>

#ifndef _WIN32

TEST_CASE("runProcess captures merged output and the exit code", "[platform][process]") {
    const std::array<std::string, 2> args = {"-c", "echo out; echo err 1>&2; exit 3"};
    const auto r = roboslop::runProcess("/bin/sh", args);

    REQUIRE(r.has_value());
    REQUIRE(r->exitCode == 3);
    REQUIRE(r->output.contains("out"));
    REQUIRE(r->output.contains("err"));
}

TEST_CASE("runProcess reports a missing executable via the exit code", "[platform][process]") {
    const std::array<std::string, 0> args = {};
    const auto r = roboslop::runProcess("/nonexistent/roboslop-no-such-binary", args);

    REQUIRE(r.has_value());
    REQUIRE(r->exitCode == 127);
}

#endif
