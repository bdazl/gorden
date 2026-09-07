module;

#include <taskflow/taskflow.hpp>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

export module roboslop.sched;

import roboslop.ecs;
import roboslop.platform.input;

namespace roboslop {

// Per-call frame context passed to every system. All pointers are
// nullable so unit tests can drive graphs without GLFW / bgfx / Jolt
// initialised. dt is the fixed sub-step delta in the fixed-update
// stage and the render frame delta in the render stage; alpha is the
// [0,1) accumulator fraction for visual interpolation between fixed
// states. Engine-owned subsystems (JoltWorld, ...) are surfaced via
// World::registry().ctx() rather than typed members here, so this
// module does not depend on physics / audio / anim.
export enum class FrameStage : int {
    FixedUpdate,
    Render,
};

export struct SystemCtx {
    World* world = nullptr;
    Input* input = nullptr;
    double dt = 0.0;
    double alpha = 0.0;
    FrameStage stage = FrameStage::FixedUpdate;
};

// Declarative system entry: a name (debug only), the resources the body
// reads and writes (opaque string ids — the scheduler does not introspect
// them), and the callable. Resource ids are user-defined conventions, e.g.
// "transforms", "physicsState", "drawItems". Edges in the graph are
// derived from add-order pairs that share any of write/write, write/read,
// or read/write conflicts; the earlier add wins. Add-order-forward edges
// make cycles impossible by construction.
export struct SystemDesc {
    std::string_view name;
    std::vector<std::string_view> reads;
    std::vector<std::string_view> writes;
    std::function<void(SystemCtx&)> run;
};

namespace detail {

[[nodiscard]] static inline auto
anyIntersect(std::span<const std::string_view> a, std::span<const std::string_view> b) noexcept
    -> bool {
    for (const auto& x : a) {
        for (const auto& y : b) {
            if (x == y) {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] static inline auto conflicts(const SystemDesc& a, const SystemDesc& b) noexcept
    -> bool {
    return anyIntersect(a.writes, b.writes) || anyIntersect(a.writes, b.reads) ||
           anyIntersect(a.reads, b.writes);
}

} // namespace detail

// A compiled bundle of systems with dependency edges materialised into a
// tf::Taskflow. Lifecycle:
//   1. construct;
//   2. call add() N times;
//   3. call compile() once — builds the taskflow with tasks closing over
//      this->currentCtx;
//   4. run via Scheduler::run(graph, ctx) zero or more times per frame.
//
// The taskflow is built once and reused — per-frame execution does no
// taskflow allocation. SystemCtx is threaded through a member pointer
// that Scheduler::run sets before executor.run() and clears after.
export class SystemGraph {
  public:
    SystemGraph() = default;

    SystemGraph(const SystemGraph&) = delete;
    auto operator=(const SystemGraph&) -> SystemGraph& = delete;
    SystemGraph(SystemGraph&&) noexcept = default;
    auto operator=(SystemGraph&&) noexcept -> SystemGraph& = default;
    ~SystemGraph() = default;

    auto add(SystemDesc desc) -> void {
        descs.push_back(std::move(desc));
        compiled = false;
    }

    auto compile() -> void {
        taskflow.clear();
        tasks.clear();
        tasks.reserve(descs.size());

        // Tasks must close over a stable pointer the graph updates per
        // frame, not over a per-frame SystemCtx that's gone before
        // execution. The lambda derefs currentCtx; Scheduler::run
        // guarantees it's non-null while taskflow runs.
        // Capture the index, not a reference into `descs`: a later add()
        // reallocates the vector.
        for (std::size_t i = 0; i < descs.size(); ++i) {
            tasks.push_back(taskflow.emplace(
                                        [this, i] { descs[i].run(*currentCtx); }
            ).name(std::string{descs[i].name}));
        }

        for (std::size_t i = 0; i < descs.size(); ++i) {
            for (std::size_t j = i + 1; j < descs.size(); ++j) {
                if (detail::conflicts(descs[i], descs[j])) {
                    tasks[i].precede(tasks[j]);
                }
            }
        }

        compiled = true;
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t {
        return descs.size();
    }

    [[nodiscard]] auto isCompiled() const noexcept -> bool {
        return compiled;
    }

    // Scheduler-only: pre/post run hooks for the context pointer.
    auto setContext(SystemCtx* ctx) noexcept -> void {
        currentCtx = ctx;
    }

    [[nodiscard]] auto taskflowRef() noexcept -> tf::Taskflow& {
        return taskflow;
    }

  private:
    std::vector<SystemDesc> descs;
    tf::Taskflow taskflow;
    std::vector<tf::Task> tasks;
    SystemCtx* currentCtx = nullptr;
    bool compiled = false;
};

// Pure helper for unit tests: derive the predecessor count of each
// system from add-order conflict rules. Mirrors compile()'s edge logic
// without touching Taskflow.
export [[nodiscard]] auto deriveInDegrees(std::span<const SystemDesc> descs)
    -> std::vector<std::size_t> {
    std::vector<std::size_t> in(descs.size(), 0);
    for (std::size_t i = 0; i < descs.size(); ++i) {
        for (std::size_t j = i + 1; j < descs.size(); ++j) {
            if (detail::conflicts(descs[i], descs[j])) {
                ++in[j];
            }
        }
    }
    return in;
}

// Picks a sane default worker count: one less than hardware concurrency
// so the main thread does not contend with workers, clamped to [1, 8].
// hardware_concurrency() returning 0 is treated as unknown → fall back
// to 1 worker.
export [[nodiscard]] auto defaultWorkerCount() noexcept -> unsigned {
    const unsigned hw = std::thread::hardware_concurrency();
    if (hw == 0) {
        return 1;
    }
    const unsigned want = hw > 1 ? hw - 1 : 1;
    return std::min(want, 8U);
}

// Owns one tf::Executor for the application. Worker count is decided at
// construction and not resized later. The executor itself is not movable
// (it owns OS threads), so it lives behind a unique_ptr — Scheduler then
// composes cleanly into App without forcing App to be non-movable.
export class Scheduler {
  public:
    explicit Scheduler(unsigned workerCount = defaultWorkerCount())
        : executor(std::make_unique<tf::Executor>(workerCount)) {}

    Scheduler(const Scheduler&) = delete;
    auto operator=(const Scheduler&) -> Scheduler& = delete;
    Scheduler(Scheduler&&) noexcept = default;
    auto operator=(Scheduler&&) noexcept -> Scheduler& = default;
    ~Scheduler() = default;

    // Synchronous: returns when every system in the graph has run for
    // this ctx. Caller's frame body resumes after the .wait().
    auto run(SystemGraph& graph, SystemCtx& ctx) -> void {
        graph.setContext(&ctx);
        executor->run(graph.taskflowRef()).wait();
        graph.setContext(nullptr);
    }

    [[nodiscard]] auto workerCount() const noexcept -> std::size_t {
        return executor->num_workers();
    }

  private:
    std::unique_ptr<tf::Executor> executor;
};

} // namespace roboslop
