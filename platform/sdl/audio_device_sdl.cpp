// SDL's sound output (audio_device.h): a stream with a pull callback; SDL follows the default device and converts to it.
// The stream takes the device's channel count, in SDL's order for that count (SDL_audio.h), so a device with more than
// two speakers gets each of them; when the default device changes, SDL gives the stream the new device's format (its
// device end, SDL_audio.c UpdateAudioStreamFormatsPhysical), which the callback reads and follows.
#include "galaxy/audio_device.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <span>
#include <vector>

namespace galaxy::host {
namespace {

using galaxy::audio::Speaker;

// SDL's channel order for each count (SDL_audio.h): 2 FL FR; 3 FL FR LFE; 4 FL FR BL BR; 5 FL FR LFE BL BR;
// 6 FL FR FC LFE BL BR; 7 FL FR FC LFE BC SL SR; 8 FL FR FC LFE BL BR SL SR. One channel is played as the front pair
// (SDL mixes it down).
constexpr Speaker kL = Speaker::Left, kR = Speaker::Right, kC = Speaker::Center, kLfe = Speaker::LowFrequency,
                  kBL = Speaker::BackLeft, kBR = Speaker::BackRight, kSL = Speaker::SideLeft, kSR = Speaker::SideRight,
                  kBC = Speaker::Other;
constexpr std::array<std::array<Speaker, 8>, 9> kOrder{{
    {kL, kR},
    {kL, kR},
    {kL, kR},
    {kL, kR, kLfe},
    {kL, kR, kBL, kBR},
    {kL, kR, kLfe, kBL, kBR},
    {kL, kR, kC, kLfe, kBL, kBR},
    {kL, kR, kC, kLfe, kBC, kSL, kSR},
    {kL, kR, kC, kLfe, kBL, kBR, kSL, kSR},
}};

[[nodiscard]] int playable_channels(int device_channels) noexcept {
    return std::clamp(device_channels, 2, 8);
}

[[nodiscard]] std::span<const Speaker> order_of(int channels) noexcept {
    const int count = playable_channels(channels);
    return std::span<const Speaker>(kOrder[static_cast<std::size_t>(count)].data(), static_cast<std::size_t>(count));
}

class SdlAudioDevice final : public AudioDevice {
public:
    SdlAudioDevice() {
        SDL_AudioSpec device{};
        int device_frames = 0;
        if (SDL_GetAudioDeviceFormat(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &device, &device_frames)) {
            if (device.freq >= 8000 && device.freq <= 192000) {
                rate_ = static_cast<std::uint32_t>(device.freq);
            }
            set_device_channels(device.channels);
        }
    }

    ~SdlAudioDevice() override {
        if (stream_ != nullptr) {
            SDL_DestroyAudioStream(stream_);
        }
    }

    [[nodiscard]] std::uint32_t rate() const override { return rate_; }

    [[nodiscard]] std::span<const Speaker> speakers() const noexcept override { return order_of(channels_); }

    [[nodiscard]] std::uint8_t reported() const noexcept override {
        return galaxy::audio::speaker_kinds(order_of(device_channels_.load(std::memory_order_relaxed)));
    }

    void start(Render render) override {
        render_ = std::move(render);
        channels_ = playable_channels(device_channels_.load(std::memory_order_relaxed));
        const SDL_AudioSpec spec{SDL_AUDIO_F32, channels_, static_cast<int>(rate_)};
        out_.reserve(static_cast<std::size_t>(rate_ / 10u) * static_cast<std::size_t>(channels_));
        stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &SdlAudioDevice::pull, this);
        if (stream_ == nullptr) {
            std::fprintf(stderr, "[audio] no output: %s\n", SDL_GetError());
            channels_ = 2;
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
    void set_device_channels(int channels) noexcept {
        device_channels_.store(playable_channels(channels), std::memory_order_relaxed);
    }

    static void SDLCALL pull(void* self, SDL_AudioStream* stream, int additional, int) {
        auto* device = static_cast<SdlAudioDevice*>(self);
        // The device's end of the stream is the device's format; another device behind the default (a headset plugged in)
        // changes it, and the stream's input takes its channel count (the stream's lock is held here and is recursive).
        SDL_AudioSpec input{};
        SDL_AudioSpec output{};
        if (SDL_GetAudioStreamFormat(stream, &input, &output) && playable_channels(output.channels) != device->channels_) {
            device->set_device_channels(output.channels);
            input.channels = playable_channels(output.channels);
            if (SDL_SetAudioStreamFormat(stream, &input, nullptr)) {
                device->channels_ = input.channels;
            }
        }
        const auto bytes_per_frame = static_cast<std::uint32_t>(device->channels_) * 4u;
        const auto frames = static_cast<std::uint32_t>(additional) / bytes_per_frame;
        if (frames == 0u) {
            return;
        }
        device->out_.resize(static_cast<std::size_t>(frames) * static_cast<std::size_t>(device->channels_));
        device->render_(device->out_.data(), frames, device->rate_);
        (void)SDL_PutAudioStreamData(stream, device->out_.data(), static_cast<int>(frames * bytes_per_frame));
    }

    std::uint32_t rate_ = 48000u;
    std::atomic<int> device_channels_{2};  // the device's channel count as SDL reports it (2 to 8)
    int channels_ = 2;                     // the stream's input: what render() writes (the device's thread)
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
