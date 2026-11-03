// The platform's sound output: it pulls stereo float samples on its own thread, as the device needs them.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>

namespace galaxy::host {

class AudioDevice {
public:
    // `frames` frames (left, right) into `out` at `rate` Hz, which changes when the device does. On the device's thread.
    using Render = std::function<void(float* out, std::uint32_t frames, std::uint32_t rate)>;

    virtual ~AudioDevice() = default;
    [[nodiscard]] virtual std::uint32_t rate() const = 0;
    virtual void start(Render render) = 0;
    virtual void set_paused(bool paused) = 0;
    // The frames given to the device that it has not played yet, as of the last call of the render function.
    [[nodiscard]] virtual std::uint32_t queued_frames() const { return 0u; }
};

// The system's default output, followed when it changes; with none, the clock pulls until one appears.
std::unique_ptr<AudioDevice> open_audio_device();
// No output: the clock pulls at `rate`.
std::unique_ptr<AudioDevice> open_null_audio_device(std::uint32_t rate);
// Names the calling thread (the device's) for profilers and the CPU report.
void set_audio_thread_name();

}  // namespace galaxy::host
