module;
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

export module roboslop.time.frame_stats;

export namespace roboslop {

// One frame's worth of timings, in milliseconds. cpuFrame is the wall
// time the App loop spent on the whole frame; fixed and render are the
// two blocks inside it. gpu, waitSubmit and waitRender come from
// bgfx::getStats() and describe the frame bgfx last completed, which
// trails the CPU frame by a frame or two — they are comparable to each
// other, not to cpuFrame of the same sample.
//
// waitSubmit and waitRender are the pair worth watching when judging a
// threading model: they measure how long each side of the bgfx
// submit/render split waited for the other.
struct FrameSample {
    double cpuFrameMs = 0.0;
    double fixedMs = 0.0;
    double renderMs = 0.0;
    double gpuMs = 0.0;
    double waitSubmitMs = 0.0;
    double waitRenderMs = 0.0;
    std::uint32_t drawCalls = 0;
    std::uint16_t backbufferWidth = 0;
    std::uint16_t backbufferHeight = 0;
};

struct FieldSummary {
    double min = 0.0;
    double max = 0.0;
    double mean = 0.0;
    double p50 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
};

// Nearest-rank percentile over an unsorted span: sorts a copy, then picks
// element ceil(p * n) - 1. p is a fraction in [0, 1]. Returns 0 for an
// empty span rather than reporting an error — callers plot the result.
[[nodiscard]] auto percentile(std::span<const double> values, double p) -> double {
    if (values.empty()) {
        return 0.0;
    }
    std::vector<double> sorted(values.begin(), values.end());
    std::ranges::sort(sorted);
    const auto n = static_cast<double>(sorted.size());
    const auto rank = static_cast<std::size_t>(std::ceil(std::clamp(p, 0.0, 1.0) * n));
    const std::size_t index = rank == 0 ? 0 : rank - 1;
    return sorted[std::min(index, sorted.size() - 1)];
}

[[nodiscard]] auto summarise(std::span<const double> values) -> FieldSummary {
    if (values.empty()) {
        return {};
    }
    const auto [min, max] = std::ranges::minmax(values);
    double sum = 0.0;
    for (const double v : values) {
        sum += v;
    }
    return {
        .min = min,
        .max = max,
        .mean = sum / static_cast<double>(values.size()),
        .p50 = percentile(values, 0.50),
        .p95 = percentile(values, 0.95),
        .p99 = percentile(values, 0.99),
    };
}

// Fixed-capacity ring of recent frames. Oldest sample is dropped once the
// ring is full; at(0) is always the oldest still held, so a plot reads
// left to right in chronological order.
class FrameStats {
  public:
    static constexpr std::size_t DefaultCapacity = 240;

    explicit FrameStats(std::size_t capacity = DefaultCapacity)
        : cap(capacity == 0 ? 1 : capacity) {
        ring.reserve(cap);
    }

    auto push(const FrameSample& sample) -> void {
        // cap, not ring.capacity(): reserve() is free to over-allocate,
        // which would silently widen the window.
        if (ring.size() < cap) {
            ring.push_back(sample);
            return;
        }
        ring[next] = sample;
        next = (next + 1) % ring.size();
        wrapped = true;
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t {
        return ring.size();
    }

    [[nodiscard]] auto empty() const noexcept -> bool {
        return ring.empty();
    }

    [[nodiscard]] auto capacity() const noexcept -> std::size_t {
        return cap;
    }

    // Chronological access: index 0 is the oldest sample still held.
    [[nodiscard]] auto at(std::size_t index) const -> const FrameSample& {
        return ring[wrapped ? (next + index) % ring.size() : index];
    }

    [[nodiscard]] auto latest() const -> const FrameSample& {
        return at(ring.size() - 1);
    }

    // Pulls one field out of every held sample, oldest first. Pointer to
    // member keeps the caller from having to name a field twice.
    [[nodiscard]] auto collect(double FrameSample::* field) const -> std::vector<double> {
        std::vector<double> out;
        out.reserve(ring.size());
        for (std::size_t i = 0; i < ring.size(); ++i) {
            out.push_back(at(i).*field);
        }
        return out;
    }

    [[nodiscard]] auto summary(double FrameSample::* field) const -> FieldSummary {
        const auto values = collect(field);
        return summarise(values);
    }

    auto clear() noexcept -> void {
        ring.clear();
        next = 0;
        wrapped = false;
    }

  private:
    std::vector<FrameSample> ring;
    std::size_t cap;
    std::size_t next = 0;
    bool wrapped = false;
};

} // namespace roboslop
