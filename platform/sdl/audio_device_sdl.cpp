// SDL's sound output (audio_device.h): a stream with a pull callback; SDL follows the default device and converts to it.
#include "galaxy/audio_device.h"
#include "galaxy/audio_fill.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <optional>

namespace galaxy::host {
namespace {

class SdlAudioDevice final : public AudioDevice {
public:
    SdlAudioDevice() {
        SDL_AudioSpec device{};
        int device_frames = 0;
        if (SDL_GetAudioDeviceFormat(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &device, &device_frames) && device.freq >= 8000 &&
            device.freq <= 192000) {
            rate_ = static_cast<std::uint32_t>(device.freq);
            device_frames_ = static_cast<std::uint32_t>(std::max(device_frames, 0));
        }
        // SDL asks for about a device buffer at a time; four of them, and never less than 4096 frames, in one chunk.
        pull_.emplace(std::max(device_frames_ * 4u, 4096u), 2u);
    }

    ~SdlAudioDevice() override {
        if (stream_ != nullptr) {
            SDL_DestroyAudioStream(stream_);
        }
    }

    [[nodiscard]] std::uint32_t rate() const override { return rate_; }
    // What SDL's stream holds and the device's buffer, at the stream's rate (SDL3 states no deeper latency).
    [[nodiscard]] std::uint32_t queued_frames() const override { return queued_.load(std::memory_order_relaxed); }

    void start(Render render) override {
        render_ = std::move(render);
        const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, static_cast<int>(rate_)};
        stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &SdlAudioDevice::pull, this);
        if (stream_ == nullptr) {
            std::fprintf(stderr, "[audio] no output: %s\n", SDL_GetError());
            fallback_ = open_null_audio_device(rate_);
            fallback_->start(render_);
        } else {
            SDL_ResumeAudioStreamDevice(stream_);
        }
    }

    void set_paused(bool paused) override {
        if (stream_ != nullptr) {
            (void)(paused ? SDL_PauseAudioStreamDevice(stream_) : SDL_ResumeAudioStreamDevice(stream_));
        }
        if (fallback_ != nullptr) {
            fallback_->set_paused(paused);
        }
    }

private:
    // No allocation here: the chunks come from the buffer made with the device (P-037).
    static void SDLCALL pull(void* self, SDL_AudioStream* stream, int additional, int) {
        auto* device = static_cast<SdlAudioDevice*>(self);
        const auto frames = static_cast<std::uint32_t>(std::max(additional, 0)) / 8u;
        device->pull_->pull(
            frames,
            [device, stream](float* out, std::uint32_t n) {
                // A movie's picture follows what the stream and the device still hold, the chunks put before this one
                // included (native_audio.cpp).
                const int held = SDL_GetAudioStreamQueued(stream);
                device->queued_.store(static_cast<std::uint32_t>(std::max(held, 0)) / 8u + device->device_frames_,
                                      std::memory_order_relaxed);
                device->render_(out, n, device->rate_);
            },
            [stream](const float* out, std::uint32_t n) {
                (void)SDL_PutAudioStreamData(stream, out, static_cast<int>(n * 8u));
            });
    }

    std::uint32_t rate_ = 48000u;
    std::uint32_t device_frames_ = 0u;
    std::atomic<std::uint32_t> queued_{0u};
    SDL_AudioStream* stream_ = nullptr;
    std::unique_ptr<AudioDevice> fallback_;
    Render render_;
    std::optional<ChunkedPull> pull_;
};

}  // namespace

std::unique_ptr<AudioDevice> open_audio_device() {
    return std::make_unique<SdlAudioDevice>();
}

void set_audio_thread_name() {}

}  // namespace galaxy::host
