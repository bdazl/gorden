module;

#include <bgfx/bgfx.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <expected>
#include <filesystem>
#include <future>
#include <string>
#include <utility>
#include <vector>

export module shaderlab.reload;

import roboslop.assets.shader_compiler;
import roboslop.core.error;
import roboslop.ecs;
import roboslop.render.asset_cache;
import roboslop.render.frontend;
import roboslop.render.shader;

namespace shaderlab {

export enum class ReloadStatus : int {
    Idle,      // nothing compiled since start
    Compiling, // a shaderc job is in flight
    Ok,        // last compile succeeded and the program was swapped
    Failed,    // last compile failed; the previous program is still live
};

export [[nodiscard]] constexpr auto statusName(ReloadStatus s) noexcept -> const char* {
    switch (s) {
    case ReloadStatus::Idle:
        return "Idle";
    case ReloadStatus::Compiling:
        return "Compiling";
    case ReloadStatus::Ok:
        return "Ok";
    case ReloadStatus::Failed:
        return "Failed";
    }
    return "?";
}

// The shader pair Shader Lab edits, plus everything shaderc needs to
// rebuild it. `cacheVs`/`cacheFs` are the AssetCache key the running
// program was loaded under, so the swap replaces the right entry.
export struct LabShaderSet {
    std::filesystem::path vsSource;
    std::filesystem::path fsSource;
    std::filesystem::path varyingDef;
    std::vector<std::filesystem::path> includeDirs;
    std::string profile;
    std::filesystem::path scratchDir;
    std::string cacheVs;
    std::string cacheFs;
};

// Drives compile → makeProgram → replaceProgram → rebindProgram.
//
// requestCompile() runs shaderc on a worker thread (std::async) and
// never blocks the frame. pump() is called every frame from the render
// thread: when the job is done it creates the bgfx program, swaps it
// into the AssetCache, and rewrites the handles held by entities. On
// any failure the previous program stays live and the diagnostics are
// kept for the UI. A change that arrives mid-compile is remembered and
// compiled right after the current job finishes.
export class ShaderReloader {
  public:
    ShaderReloader(roboslop::ShaderCompiler compiler, LabShaderSet set) noexcept
        : compiler(std::move(compiler)), set(std::move(set)) {}

    ShaderReloader(const ShaderReloader&) = delete;
    auto operator=(const ShaderReloader&) -> ShaderReloader& = delete;
    ShaderReloader(ShaderReloader&&) noexcept = default;
    auto operator=(ShaderReloader&&) noexcept -> ShaderReloader& = default;
    ~ShaderReloader() = default;

    // The program currently bound to lab entities; set once after the
    // initial (build-time compiled) load, updated by every swap.
    auto setCurrentProgram(roboslop::ProgramHandle h) noexcept -> void {
        current = h;
    }

    [[nodiscard]] auto currentProgram() const noexcept -> roboslop::ProgramHandle {
        return current;
    }

    // Returns false when a compile is already running; the request is
    // queued and honoured by pump() once that job completes.
    auto requestCompile() -> bool {
        if (pending.valid()) {
            queued = true;
            return false;
        }
        status = ReloadStatus::Compiling;
        pending =
            std::async(std::launch::async, [c = compiler, s = set]() { return runJob(c, s); });
        return true;
    }

    auto pump(roboslop::World& world, roboslop::AssetCache& assets) -> void {
        if (!pending.valid() ||
            pending.wait_for(std::chrono::seconds{0}) != std::future_status::ready) {
            return;
        }
        Job job = pending.get();
        lastCompileMillis = job.millis;
        ++generation;

        if (!job.vs) {
            fail(job.vs.error());
        } else if (!job.fs) {
            fail(job.fs.error());
        } else {
            auto program = roboslop::makeProgram(*job.vs, *job.fs, set.cacheVs + "+" + set.cacheFs);
            if (!program) {
                fail(program.error());
            } else {
                const auto old = current;
                current = assets.replaceProgram(set.cacheVs, set.cacheFs, std::move(*program));
                rebound = roboslop::rebindProgram(world, old.value, current.value);
                status = ReloadStatus::Ok;
                diagnosticsText.clear();
                spdlog::info(
                    "shaderlab: reload #{} ok in {:.0f} ms ({} handles rebound)",
                    generation,
                    job.millis,
                    rebound
                );
            }
        }

        if (queued) {
            queued = false;
            requestCompile();
        }
    }

    [[nodiscard]] auto currentStatus() const noexcept -> ReloadStatus {
        return status;
    }

    [[nodiscard]] auto diagnostics() const noexcept -> const std::string& {
        return diagnosticsText;
    }

    [[nodiscard]] auto lastCompileMs() const noexcept -> double {
        return lastCompileMillis;
    }

    // Number of finished compile jobs (successful or not).
    [[nodiscard]] auto compileGeneration() const noexcept -> unsigned {
        return generation;
    }

    [[nodiscard]] auto lastRebindCount() const noexcept -> std::size_t {
        return rebound;
    }

    [[nodiscard]] auto shaderSet() const noexcept -> const LabShaderSet& {
        return set;
    }

  private:
    struct Job {
        roboslop::Result<std::vector<char>> vs;
        roboslop::Result<std::vector<char>> fs;
        double millis = 0.0;
    };

    // Worker-thread body. Touches no bgfx state, only files and the
    // shaderc child process.
    [[nodiscard]] static auto runJob(const roboslop::ShaderCompiler& c, const LabShaderSet& s)
        -> Job {
        const auto t0 = std::chrono::steady_clock::now();
        Job job{
            .vs = c.compile(
                roboslop::ShaderCompileRequest{
                    .source = s.vsSource,
                    .varyingDef = s.varyingDef,
                    .stage = roboslop::ShaderStage::Vertex,
                    .profile = s.profile,
                    .includeDirs = s.includeDirs,
                    .outputFile = s.scratchDir / (s.cacheVs + ".bin"),
                    .debug = false,
                }
            ),
            .fs = std::unexpected(roboslop::Error{}),
            .millis = 0.0,
        };
        // Skip the fragment stage when the vertex stage already failed:
        // one set of diagnostics at a time reads better in the panel.
        if (job.vs) {
            job.fs = c.compile(
                roboslop::ShaderCompileRequest{
                    .source = s.fsSource,
                    .varyingDef = s.varyingDef,
                    .stage = roboslop::ShaderStage::Fragment,
                    .profile = s.profile,
                    .includeDirs = s.includeDirs,
                    .outputFile = s.scratchDir / (s.cacheFs + ".bin"),
                    .debug = false,
                }
            );
        }
        const auto t1 = std::chrono::steady_clock::now();
        job.millis = std::chrono::duration<double, std::milli>(t1 - t0).count();
        return job;
    }

    auto fail(const roboslop::Error& e) -> void {
        status = ReloadStatus::Failed;
        diagnosticsText = std::string{e.message};
        if (!e.context.empty()) {
            diagnosticsText += "\n" + e.context;
        }
        spdlog::warn("shaderlab: reload #{} failed: {}", generation, diagnosticsText);
    }

    roboslop::ShaderCompiler compiler;
    LabShaderSet set;
    roboslop::ProgramHandle current{};
    std::future<Job> pending;
    bool queued = false;
    ReloadStatus status = ReloadStatus::Idle;
    std::string diagnosticsText;
    double lastCompileMillis = 0.0;
    unsigned generation = 0;
    std::size_t rebound = 0;
};

} // namespace shaderlab
