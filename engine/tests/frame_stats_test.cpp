import roboslop.time.frame_stats;

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <vector>

using Catch::Approx;
using roboslop::FrameSample;
using roboslop::FrameStats;

namespace {

auto sampleWithCpu(double cpuMs) -> FrameSample {
    return FrameSample{.cpuFrameMs = cpuMs};
}

} // namespace

TEST_CASE("percentile on an empty span is zero", "[frame_stats]") {
    REQUIRE(roboslop::percentile({}, 0.5) == 0.0);
}

TEST_CASE("percentile uses nearest rank", "[frame_stats]") {
    const std::array<double, 5> values{5.0, 1.0, 4.0, 2.0, 3.0};

    // ceil(p * 5) - 1 indexes the sorted values {1,2,3,4,5}.
    REQUIRE(roboslop::percentile(values, 0.0) == 1.0);
    REQUIRE(roboslop::percentile(values, 0.2) == 1.0);
    REQUIRE(roboslop::percentile(values, 0.5) == 3.0);
    REQUIRE(roboslop::percentile(values, 0.95) == 5.0);
    REQUIRE(roboslop::percentile(values, 1.0) == 5.0);
}

TEST_CASE("percentile of a single sample is that sample", "[frame_stats]") {
    const std::array<double, 1> values{7.5};

    REQUIRE(roboslop::percentile(values, 0.5) == 7.5);
    REQUIRE(roboslop::percentile(values, 0.99) == 7.5);
}

TEST_CASE("summarise reports min, max and mean", "[frame_stats]") {
    const std::array<double, 4> values{2.0, 4.0, 6.0, 8.0};

    const auto s = roboslop::summarise(values);

    REQUIRE(s.min == 2.0);
    REQUIRE(s.max == 8.0);
    REQUIRE(s.mean == Approx(5.0));
}

TEST_CASE("summarise of nothing is all zeroes", "[frame_stats]") {
    const auto s = roboslop::summarise({});

    REQUIRE(s.min == 0.0);
    REQUIRE(s.max == 0.0);
    REQUIRE(s.mean == 0.0);
    REQUIRE(s.p99 == 0.0);
}

TEST_CASE("FrameStats holds samples until capacity", "[frame_stats]") {
    FrameStats stats{4};

    REQUIRE(stats.empty());
    stats.push(sampleWithCpu(1.0));
    stats.push(sampleWithCpu(2.0));

    REQUIRE(stats.size() == 2);
    REQUIRE(stats.at(0).cpuFrameMs == 1.0);
    REQUIRE(stats.latest().cpuFrameMs == 2.0);
}

TEST_CASE("FrameStats drops the oldest sample once full", "[frame_stats]") {
    FrameStats stats{3};

    for (const double v : {1.0, 2.0, 3.0, 4.0, 5.0}) {
        stats.push(sampleWithCpu(v));
    }

    // Capacity 3, so 1 and 2 are gone and at(0) is the oldest survivor.
    REQUIRE(stats.size() == 3);
    REQUIRE(stats.at(0).cpuFrameMs == 3.0);
    REQUIRE(stats.at(1).cpuFrameMs == 4.0);
    REQUIRE(stats.at(2).cpuFrameMs == 5.0);
    REQUIRE(stats.latest().cpuFrameMs == 5.0);
}

TEST_CASE("FrameStats collect returns samples oldest first after wrapping", "[frame_stats]") {
    FrameStats stats{3};
    for (const double v : {10.0, 20.0, 30.0, 40.0}) {
        stats.push(sampleWithCpu(v));
    }

    const std::vector<double> expected{20.0, 30.0, 40.0};

    REQUIRE(stats.collect(&FrameSample::cpuFrameMs) == expected);
}

TEST_CASE("FrameStats summary covers only the held window", "[frame_stats]") {
    FrameStats stats{3};
    for (const double v : {100.0, 1.0, 2.0, 3.0}) {
        stats.push(sampleWithCpu(v));
    }

    const auto s = stats.summary(&FrameSample::cpuFrameMs);

    // The 100 ms spike has aged out of the ring.
    REQUIRE(s.max == 3.0);
    REQUIRE(s.min == 1.0);
    REQUIRE(s.mean == Approx(2.0));
}

TEST_CASE("FrameStats clear empties the ring", "[frame_stats]") {
    FrameStats stats{2};
    stats.push(sampleWithCpu(1.0));
    stats.push(sampleWithCpu(2.0));
    stats.push(sampleWithCpu(3.0));

    stats.clear();

    REQUIRE(stats.empty());
    REQUIRE(stats.collect(&FrameSample::cpuFrameMs).empty());
    REQUIRE(stats.summary(&FrameSample::cpuFrameMs).max == 0.0);
}
