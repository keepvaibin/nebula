// SDL's sound output (audio_device.h): a stream with a pull callback; SDL follows the default device and converts to it.
#include "galaxy/audio_device.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <vector>

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
        }
    }

    ~SdlAudioDevice() override {
        if (stream_ != nullptr) {
            SDL_DestroyAudioStream(stream_);
        }
    }

    [[nodiscard]] std::uint32_t rate() const override { return rate_; }

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
    static void SDLCALL pull(void* self, SDL_AudioStream* stream, int additional, int) {
        auto* device = static_cast<SdlAudioDevice*>(self);
        const auto frames = static_cast<std::uint32_t>(additional) / 8u;
        if (frames == 0u) {
            return;
        }
        device->out_.resize(static_cast<std::size_t>(frames) * 2u);
        device->render_(device->out_.data(), frames, device->rate_);
        (void)SDL_PutAudioStreamData(stream, device->out_.data(), static_cast<int>(frames * 8u));
    }

    std::uint32_t rate_ = 48000u;
    SDL_AudioStream* stream_ = nullptr;
    std::unique_ptr<AudioDevice> fallback_;
    Render render_;
    std::vector<float> out_;
};

}  // namespace

std::unique_ptr<AudioDevice> open_audio_device() {
    return std::make_unique<SdlAudioDevice>();
}

void set_audio_thread_name() {}

}  // namespace galaxy::host
