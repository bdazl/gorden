module;

#include <miniaudio.h>
#include <spdlog/spdlog.h>

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <utility>

export module roboslop.audio.device;

import roboslop.core.error;

namespace roboslop {

export enum class AudioError : int {
    EngineInitFailed = 1,
};

export [[nodiscard]] auto toError(AudioError e, std::string ctx = {}) -> Error {
    switch (e) {
    case AudioError::EngineInitFailed:
        return {
            .category = "roboslop.audio.device",
            .code = static_cast<int>(e),
            .message = "ma_engine_init failed",
            .context = std::move(ctx),
        };
    }
    return {
        .category = "roboslop.audio.device",
        .code = 0,
        .message = "unknown AudioError",
        .context = std::move(ctx),
    };
}

// Owns one ma_engine plus its job-system. Single-instance per process
// — matches RenderContext / JoltWorld policy. Move-only with an
// alive flag; the ma_engine lives behind a unique_ptr because
// miniaudio expects a stable address (it stashes the pointer in
// audio device callbacks).
export class AudioDevice {
  public:
    [[nodiscard]] static auto make() -> Result<AudioDevice> {
        auto engine = std::make_unique<ma_engine>();
        const ma_result r = ma_engine_init(nullptr, engine.get());
        if (r != MA_SUCCESS) {
            return std::unexpected(toError(AudioError::EngineInitFailed));
        }
        spdlog::info("roboslop: audio engine initialised");
        return AudioDevice{std::move(engine)};
    }

    AudioDevice(const AudioDevice&) = delete;
    auto operator=(const AudioDevice&) -> AudioDevice& = delete;

    AudioDevice(AudioDevice&& o) noexcept : engine(std::move(o.engine)) {}

    auto operator=(AudioDevice&& o) noexcept -> AudioDevice& {
        if (this != &o) {
            shutdown();
            engine = std::move(o.engine);
        }
        return *this;
    }

    ~AudioDevice() {
        shutdown();
    }

    [[nodiscard]] auto handle() noexcept -> ma_engine* {
        return engine.get();
    }

    auto setListenerPosition(float x, float y, float z) noexcept -> void {
        if (engine) {
            ma_engine_listener_set_position(engine.get(), 0, x, y, z);
        }
    }

    auto setListenerDirection(float x, float y, float z) noexcept -> void {
        if (engine) {
            ma_engine_listener_set_direction(engine.get(), 0, x, y, z);
        }
    }

  private:
    explicit AudioDevice(std::unique_ptr<ma_engine> e) noexcept : engine(std::move(e)) {}

    auto shutdown() noexcept -> void {
        if (engine) {
            ma_engine_uninit(engine.get());
            engine.reset();
        }
    }

    std::unique_ptr<ma_engine> engine;
};

} // namespace roboslop
