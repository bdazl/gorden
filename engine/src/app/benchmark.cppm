module;

#include <spdlog/spdlog.h>

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <utility>

export module roboslop.app.benchmark;

import roboslop.time.frame_stats;

namespace roboslop {

// Runs a fixed number of frames and reports the timings instead of
// looping until the window closes. Frames are stepped at the fixed rate
// rather than wall-clock time and vsync is off, so a run measures how
// fast the engine produces frames rather than how fast the display
// consumes them.
//
// Configured through the environment rather than AppConfig so every
// application is benchmarkable without a flag of its own, and without
// each one having to name the field in its initializer.
export struct BenchmarkConfig {
    // 0 disables the benchmark; the app runs normally.
    std::size_t frames = 0;
    // Frames discarded before measuring: the first frames of a run pay
    // for pipeline and texture creation and would dominate the tail.
    std::size_t warmupFrames = 60;
    // Optional machine-readable dump of the summary.
    std::filesystem::path jsonPath;
};

namespace {

[[nodiscard]] auto envSize(const char* name) -> std::optional<std::size_t> {
    const char* v = std::getenv(name);
    if (v == nullptr || v[0] == '\0') {
        return std::nullopt;
    }
    const char* end = v + std::string::traits_type::length(v);
    std::size_t out = 0;
    const auto [ptr, ec] = std::from_chars(v, end, out);
    if (ec != std::errc{} || ptr != end) {
        spdlog::warn("roboslop: ignoring {}={} (not a number)", name, v);
        return std::nullopt;
    }
    return out;
}

} // namespace

// ROBOSLOP_BENCH_FRAMES enables the mode; ROBOSLOP_BENCH_WARMUP and
// ROBOSLOP_BENCH_JSON tune it.
export [[nodiscard]] auto benchmarkFromEnv() -> BenchmarkConfig {
    BenchmarkConfig bench;
    if (const auto frames = envSize("ROBOSLOP_BENCH_FRAMES")) {
        bench.frames = *frames;
    }
    if (const auto warmup = envSize("ROBOSLOP_BENCH_WARMUP")) {
        bench.warmupFrames = *warmup;
    }
    if (const char* json = std::getenv("ROBOSLOP_BENCH_JSON"); json != nullptr && json[0] != '\0') {
        bench.jsonPath = json;
    }
    return bench;
}

// The fields a run reports, in the order they are printed. Named here so
// the log table and the JSON dump cannot drift apart.
export inline constexpr std::array<std::pair<const char*, double FrameSample::*>, 6>
    BenchmarkFields{{
        {"cpuFrame", &FrameSample::cpuFrameMs},
        {"fixed", &FrameSample::fixedMs},
        {"render", &FrameSample::renderMs},
        {"gpu", &FrameSample::gpuMs},
        {"waitSubmit", &FrameSample::waitSubmitMs},
        {"waitRender", &FrameSample::waitRenderMs},
    }};

// Logs the measured window and, when a path was configured, writes the
// same numbers as JSON. `multiThreaded` records which bgfx threading
// model produced them — the whole point of comparing two runs.
export auto
reportBenchmark(const FrameStats& stats, const BenchmarkConfig& bench, bool multiThreaded) -> void {
    spdlog::info(
        "roboslop: benchmark over {} frames ({})",
        stats.size(),
        multiThreaded ? "bgfx multi-threaded" : "bgfx single-threaded"
    );
    spdlog::info("roboslop:   {:>10}  {:>8} {:>8} {:>8} {:>8}", "ms", "p50", "p95", "p99", "max");
    for (const auto& [name, field] : BenchmarkFields) {
        const auto s = stats.summary(field);
        spdlog::info(
            "roboslop:   {:>10}  {:8.3f} {:8.3f} {:8.3f} {:8.3f}", name, s.p50, s.p95, s.p99, s.max
        );
    }

    if (bench.jsonPath.empty()) {
        return;
    }
    std::ofstream out(bench.jsonPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        spdlog::error("roboslop: benchmark json: cannot open {}", bench.jsonPath.string());
        return;
    }
    out << "{\n";
    out << std::format("  \"frames\": {},\n", stats.size());
    out << std::format("  \"warmupFrames\": {},\n", bench.warmupFrames);
    out << std::format("  \"multiThreaded\": {},\n", multiThreaded);
    if (!stats.empty()) {
        out << std::format("  \"drawCalls\": {},\n", stats.latest().drawCalls);
        out << std::format(
            "  \"backbuffer\": [{}, {}],\n",
            stats.latest().backbufferWidth,
            stats.latest().backbufferHeight
        );
    }
    for (std::size_t i = 0; i < BenchmarkFields.size(); ++i) {
        const auto s = stats.summary(BenchmarkFields[i].second);
        out << std::format(
            "  \"{}\": {{\"min\": {:.6f}, \"max\": {:.6f}, \"mean\": {:.6f}, "
            "\"p50\": {:.6f}, \"p95\": {:.6f}, \"p99\": {:.6f}}}{}\n",
            BenchmarkFields[i].first,
            s.min,
            s.max,
            s.mean,
            s.p50,
            s.p95,
            s.p99,
            i + 1 == BenchmarkFields.size() ? "" : ","
        );
    }
    out << "}\n";
    if (!out) {
        spdlog::error("roboslop: benchmark json: write failed for {}", bench.jsonPath.string());
        return;
    }
    spdlog::info("roboslop: benchmark json written to {}", bench.jsonPath.string());
}

} // namespace roboslop
