module;

#include <chrono>
#include <filesystem>
#include <span>
#include <utility>
#include <vector>

export module shaderlab.watcher;

namespace shaderlab {

// Polls last_write_time of a fixed set of files. Deliberately simple:
// no platform watcher, no recursion. Polling every few hundred ms is
// three stat() calls and is enough for the first Shader Lab slice
// (roadmap M1 open question, decided in docs/decisions.md).
export class ShaderWatcher {
  public:
    using Clock = std::chrono::steady_clock;

    explicit ShaderWatcher(
        std::vector<std::filesystem::path> files,
        std::chrono::milliseconds interval = std::chrono::milliseconds{250}
    )
        : interval(interval) {
        entries.reserve(files.size());
        for (auto& f : files) {
            const auto stamp = stampOf(f);
            entries.push_back(Entry{.path = std::move(f), .stamp = stamp});
        }
    }

    // True when at least one watched file changed since the previous
    // poll that looked at the disk. Calls closer together than
    // `interval` return false without touching the filesystem.
    [[nodiscard]] auto poll(Clock::time_point now) -> bool {
        if (lastPoll != Clock::time_point{} && now - lastPoll < interval) {
            return false;
        }
        lastPoll = now;
        bool changed = false;
        for (auto& e : entries) {
            const auto stamp = stampOf(e.path);
            if (stamp != e.stamp) {
                e.stamp = stamp;
                changed = true;
            }
        }
        return changed;
    }

    [[nodiscard]] auto fileCount() const noexcept -> std::size_t {
        return entries.size();
    }

  private:
    struct Entry {
        std::filesystem::path path;
        std::filesystem::file_time_type stamp;
    };

    // A missing file reads as the epoch, so "deleted" and "recreated"
    // both register as changes.
    [[nodiscard]] static auto stampOf(const std::filesystem::path& p)
        -> std::filesystem::file_time_type {
        std::error_code ec;
        const auto t = std::filesystem::last_write_time(p, ec);
        return ec ? std::filesystem::file_time_type{} : t;
    }

    std::vector<Entry> entries;
    std::chrono::milliseconds interval;
    Clock::time_point lastPoll;
};

} // namespace shaderlab
