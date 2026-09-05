module;
#include <algorithm>
#include <chrono>
export module roboslop.time.clock;

export namespace roboslop {

// Monotonic wall-clock for frame timing. tickFrame() returns the elapsed
// seconds since the previous call (or since construction / reset).
class Clock {
  public:
    using Steady = std::chrono::steady_clock;

    Clock() noexcept : last(Steady::now()) {}

    [[nodiscard]] auto tickFrame() noexcept -> double {
        const auto now = Steady::now();
        const auto dt = std::chrono::duration<double>(now - last).count();
        last = now;
        return dt;
    }

    auto reset() noexcept -> void {
        last = Steady::now();
    }

  private:
    Steady::time_point last;
};

// Semi-fixed timestep accumulator. Feed wall-clock frame deltas via
// advance(); it returns how many fixed sub-steps should run this frame.
// The leftover fraction is exposed as alpha() in [0, 1) for render
// interpolation between fixed states. Frames longer than MaxFrameTime are
// clamped so a debugger pause does not trigger a spiral-of-death.
class FixedTimestep {
  public:
    static constexpr double DefaultRateHz = 60.0;
    static constexpr double MaxFrameTime = 0.25;

    constexpr FixedTimestep() noexcept = default;

    constexpr explicit FixedTimestep(double rateHz) noexcept : fixedDt(1.0 / rateHz) {}

    [[nodiscard]] constexpr auto advance(double frameDt) noexcept -> int {
        accumulator += std::min(frameDt, MaxFrameTime);
        int steps = 0;
        while (accumulator >= fixedDt) {
            accumulator -= fixedDt;
            ++steps;
        }
        return steps;
    }

    [[nodiscard]] constexpr auto fixedDelta() const noexcept -> double {
        return fixedDt;
    }

    [[nodiscard]] constexpr auto alpha() const noexcept -> double {
        return accumulator / fixedDt;
    }

  private:
    double fixedDt = 1.0 / DefaultRateHz;
    double accumulator = 0.0;
};

} // namespace roboslop
