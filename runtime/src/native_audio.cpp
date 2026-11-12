// The game's sound on every host (native_audio.h): the native mixer's frames at the master volume with the remote speaker
// added, pulled by the output device (audio_device.h), and given to the host's extensions (host_extension.h).
#include "galaxy/native_audio.h"
#include "galaxy/host_extension.h"
#include "galaxy/launch_args.h"

#include "galaxy/audio_device.h"
#include "galaxy/audio_mixer.h"
#include "galaxy/luma_audio_service.h"
#include "galaxy/runtime_settings.h"
#include "galaxy/wiimote_speaker.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace galaxy::host {
namespace {

// No output: pulled by the clock, as a device would.
class NullAudioDevice final : public AudioDevice {
public:
    explicit NullAudioDevice(std::uint32_t rate) : rate_(rate) {}

    [[nodiscard]] std::uint32_t rate() const override { return rate_; }

    void start(Render render) override {
        std::thread([this, render = std::move(render)] {
            set_audio_thread_name();
            constexpr std::uint32_t kStepsPerSecond = 200u;
            std::vector<float> out;
            auto next = std::chrono::steady_clock::now();
            for (;;) {
                const std::uint32_t rate_now = rate();
                const std::uint32_t frames = rate_now / kStepsPerSecond;
                const std::size_t channels = speakers().size();
                out.resize(frames * channels);
                next += std::chrono::nanoseconds(1000000000ll / kStepsPerSecond);
                std::this_thread::sleep_until(next);
                // A stall past 100 ms (a debugger, a hitch) starts the schedule again rather than catching up in a burst.
                const auto now = std::chrono::steady_clock::now();
                if (next + std::chrono::milliseconds(100) < now) {
                    next = now;
                }
                if (!paused_.load(std::memory_order_relaxed)) {
                    render(out.data(), frames, rate_now);
                }
            }
        }).detach();
    }

    void set_paused(bool paused) override { paused_.store(paused, std::memory_order_relaxed); }

private:
    const std::uint32_t rate_;
    std::atomic<bool> paused_{false};
};

}  // namespace

std::unique_ptr<AudioDevice> open_null_audio_device(std::uint32_t rate) {
    return std::make_unique<NullAudioDevice>(rate);
}

struct NativeGameAudio::State {
    std::unique_ptr<AudioDevice> device;
    std::unique_ptr<galaxy::audio::AudioMixer> mixer;
    float master{1.0f};
    bool master_started{};
    // 1 while heard, 0 while the window is in the background with focus_loss=mute; ramped across a frame on a change.
    float audible{1.0f};
    std::vector<float> speaker_frame;  // the remote speaker's frame on its own, while `audible` ramps
    std::vector<float> mods;           // the mods' voices and streams (luma_audio::mix), before the master
    galaxy::SoundOutput sound_output{galaxy::SoundOutput::Stereo};
    // The frames made and not yet pulled, interleaved; the device's thread only. Center, surround left and right apart
    // from them while the device has rear speakers.
    std::vector<float> pending;
    std::vector<float> pending_surround;
    std::size_t pending_read{};
    std::function<void()> tick;
    std::atomic<bool> running{false};
    bool started{};

    // `count` frames from `at` onto the device's speakers. A surround split between sides and backs; a center with no
    // speaker of its own goes to left and right.
    void place(float* out, std::span<const Speaker> speakers, std::size_t at, std::size_t count) const {
        constexpr float kHalfPower = 0.70710678f;
        const auto has = [speakers](Speaker wanted) { return std::find(speakers.begin(), speakers.end(), wanted) != speakers.end(); };
        const float center_side = has(Speaker::Center) ? 0.0f : kHalfPower;
        const float rear = has(Speaker::SideLeft) && has(Speaker::BackLeft) ? kHalfPower : 1.0f;
        const float* front = pending.data() + at * 2u;
        const float* apart = pending_surround.data() + at * 3u;
        for (std::size_t i = 0; i < count; ++i, front += 2, apart += 3) {
            for (const Speaker speaker : speakers) {
                float value = 0.0f;
                switch (speaker) {
                case Speaker::Left: value = front[0] + center_side * apart[0]; break;
                case Speaker::Right: value = front[1] + center_side * apart[0]; break;
                case Speaker::Center: value = apart[0]; break;
                case Speaker::SideLeft:
                case Speaker::BackLeft: value = rear * apart[1]; break;
                case Speaker::SideRight:
                case Speaker::BackRight: value = rear * apart[2]; break;
                default: break;
                }
                *out++ = std::clamp(value, -1.0f, 1.0f);
            }
        }
    }

    void render(float* out, std::uint32_t frames, std::uint32_t rate) {
        const std::span<const Speaker> speakers = device->speakers();
        const std::size_t channels = speakers.size();
        const bool discrete = channels > 2u;
        // What waits was made for the last device.
        if (mixer->rate() != rate || mixer->discrete() != discrete) {
            if (mixer->rate() != rate) {
                mixer->set_rate(rate);
            }
            mixer->set_discrete(discrete);
            pending.clear();
            pending_surround.clear();
            pending_read = 0u;
        }
        std::size_t filled = 0u;
        while (filled < frames) {
            const std::size_t have = pending.size() / 2u - pending_read;
            if (have == 0u) {
                pending.clear();
                pending_surround.clear();
                pending_read = 0u;
                if (running.load(std::memory_order_acquire)) {
                    tick();
                }
                if (pending.empty()) {
                    break;
                }
                continue;
            }
            const std::size_t take = std::min<std::size_t>(have, frames - filled);
            if (discrete) {
                place(out + filled * channels, speakers, pending_read, take);
            } else {
                std::copy_n(pending.data() + pending_read * 2u, take * 2u, out + filled * 2u);
            }
            pending_read += take;
            filled += take;
        }
        std::fill(out + filled * channels, out + static_cast<std::size_t>(frames) * channels, 0.0f);
        // What the mixer made and has not been played: what waits here, what just went out and what the device holds. A
        // movie's picture follows this.
        const std::uint64_t unplayed = pending.size() / 2u - pending_read + frames + device->queued_frames();
        mixer->movie().set_unplayed(static_cast<std::size_t>(unplayed * galaxy::audio::MovieAudio::kRate / rate));
    }
};

NativeGameAudio::NativeGameAudio() : state_(std::make_unique<State>()) {}
NativeGameAudio::~NativeGameAudio() = default;

void NativeGameAudio::setup(NebulaVoice* voices, std::uint32_t count, const NebulaEffectLine* effects,
                            bool device) {
    State& s = *state_;
    std::uint32_t rate = 48000u;
    if (device) {
        // An extension's NEBULA_EXT_NO_SOUND_DEVICE: the clock pulls, nothing reaches a device.
        s.device = (galaxy::extensions::flags() & NEBULA_EXT_NO_SOUND_DEVICE) != 0u ? open_null_audio_device(rate)
                                                                                    : open_audio_device();
        rate = s.device->rate();
    }
    s.sound_output = galaxy::get_runtime_settings().audio.sound_output;
    if (s.device != nullptr) {
        s.device->set_surround(s.sound_output == galaxy::SoundOutput::Surround);
    }
    s.mixer = std::make_unique<galaxy::audio::AudioMixer>(rate);
    s.mixer->setup(voices, count, effects);
    const galaxy::RuntimeAudioSettings audio = galaxy::get_runtime_settings().audio;
    s.mixer->set_category_gains({1.0f, audio.music, audio.voice, audio.sfx, audio.ambience, audio.cutscene});
}

void NativeGameAudio::start(std::function<void()> tick) {
    State& s = *state_;
    if (s.device != nullptr && !s.started) {
        s.started = true;
        s.tick = std::move(tick);
        s.device->start([&s](float* out, std::uint32_t frames, std::uint32_t rate) { s.render(out, frames, rate); });
    }
    s.running.store(true, std::memory_order_release);
}

void NativeGameAudio::stop() {
    state_->running.store(false, std::memory_order_release);
}

void NativeGameAudio::mix(float gain, std::uint32_t used) {
    if (state_->mixer != nullptr) {
        state_->mixer->mix(gain, used);
    }
}

// The frame: the master volume (ramped across the frame from the last one's), the remote speaker, the output's samples.
void NativeGameAudio::frame() {
    if (state_->mixer == nullptr) {
        return;
    }
    State& s = *state_;
    const std::span<const float> samples = s.mixer->end_frame();
    const std::span<const float> apart = s.mixer->surround();
    const galaxy::RuntimeAudioSettings audio = galaxy::get_runtime_settings().audio;
    s.mixer->set_category_gains({1.0f, audio.music, audio.voice, audio.sfx, audio.ambience, audio.cutscene});
    if (audio.sound_output != s.sound_output) {
        s.sound_output = audio.sound_output;
        if (s.device != nullptr) {
            s.device->set_surround(s.sound_output == galaxy::SoundOutput::Surround);
        }
    }
    // Muted in the background: the mix goes on at full pace and the master ramps to silence, so nothing clicks.
    const bool muted = galaxy::background_muted();
    const float target = (muted || audio.master < 0.0f) ? 0.0f : std::min(audio.master, 4.0f);
    const float start = s.master_started ? s.master : target;
    s.master_started = true;
    s.master = target;
    const float audible = muted ? 0.0f : 1.0f;
    const float audible_start = s.audible;
    s.audible = audible;
    const std::size_t frames = samples.size() / 2u;
    if (frames == 0u) {
        return;
    }
    const std::size_t at = s.pending.size();
    s.pending.resize(at + frames * 2u);
    const std::span<float> out(s.pending.data() + at, frames * 2u);
    // The mods' sound (LumaCore audio, services-audio) joins the game's front left and right before the master volume,
    // each bus at the player's volume for it: effects and ui follow Sound effects.
    const float buses[4] = {audio.sfx, audio.sfx, audio.voice, audio.music};
    s.mods.assign(frames * 2u, 0.0f);
    const bool mods = galaxy::luma_audio::mix(s.mods.data(), frames, s.mixer->rate(), buses);
    for (std::size_t i = 0; i < frames; ++i) {
        const float gain = start + (target - start) * static_cast<float>(i + 1u) / static_cast<float>(frames);
        out[i * 2u] = (mods ? samples[i * 2u] + s.mods[i * 2u] : samples[i * 2u]) * gain;
        out[i * 2u + 1u] = (mods ? samples[i * 2u + 1u] + s.mods[i * 2u + 1u] : samples[i * 2u + 1u]) * gain;
    }
    if (!apart.empty()) {
        const std::size_t apart_at = s.pending_surround.size();
        s.pending_surround.resize(apart_at + frames * 3u);
        for (std::size_t i = 0; i < frames; ++i) {
            const float gain = start + (target - start) * static_cast<float>(i + 1u) / static_cast<float>(frames);
            for (std::size_t k = 0; k < 3u; ++k) {
                s.pending_surround[apart_at + i * 3u + k] = apart[i * 3u + k] * gain;
            }
        }
    }
    // The remote speaker is summed after the master, so the mute fades it on its own; silent, it stops listening and
    // drops what arrived meanwhile.
    galaxy::input::speaker::Speaker& speaker = galaxy::input::speaker::game_speaker();
    if (audible_start == audible) {
        speaker.mix(out, s.mixer->rate(), audible > 0.0f ? audio.wiimote_speaker : 0.0f);
    } else {
        s.speaker_frame.assign(frames * 2u, 0.0f);
        speaker.mix(s.speaker_frame, s.mixer->rate(), audio.wiimote_speaker);
        for (std::size_t i = 0; i < frames; ++i) {
            const float gain = audible_start + (audible - audible_start) * static_cast<float>(i + 1u) / static_cast<float>(frames);
            out[i * 2u] += s.speaker_frame[i * 2u] * gain;
            out[i * 2u + 1u] += s.speaker_frame[i * 2u + 1u] * gain;
        }
    }
    if (audio.sound_output == galaxy::SoundOutput::Mono) {
        for (std::size_t i = 0; i < frames; ++i) {
            const float both = 0.5f * (out[i * 2u] + out[i * 2u + 1u]);
            out[i * 2u] = both;
            out[i * 2u + 1u] = both;
        }
    }
    const bool trace_first = ::galaxy::diagnosing(::galaxy::kDiagnoseFileReads);
    static bool first_sound = false;
    if (trace_first && !first_sound && std::any_of(out.begin(), out.end(), [](float value) { return value != 0.0f; })) {
        first_sound = true;
        std::fprintf(stderr, "[read] first sound\n");
    }
    if (galaxy::extensions::hears_sound()) {
        galaxy::extensions::sound(out.data(), static_cast<std::uint32_t>(frames), 2u, s.mixer->rate());
    }
    for (float& value : out) {
        value = std::clamp(value, -1.0f, 1.0f);
    }
    if (s.device == nullptr) {
        s.pending.clear();  // nothing pulls: the frame goes no further than the extensions
    }
}

int NativeGameAudio::movie_audio(const void* record, std::uint32_t size) {
    if (state_->mixer == nullptr) {
        return 0;
    }
    galaxy::audio::MovieAudio& movie = state_->mixer->movie();
    if (record == nullptr) {
        return static_cast<int>(movie.room());
    }
    return movie.push(static_cast<const std::uint8_t*>(record), size) ? 1 : 0;
}

double NativeGameAudio::movie_audio_position() {
    return state_->mixer != nullptr ? state_->mixer->movie().position() : 0.0;
}

void NativeGameAudio::movie_audio_control(std::uint32_t what, std::int32_t value, std::int32_t milliseconds) {
    if (state_->mixer == nullptr) {
        return;
    }
    galaxy::audio::MovieAudio& movie = state_->mixer->movie();
    switch (what) {
    case NEBULA_MOVIE_AUDIO_OPEN:
        movie.open(value != 0);
        break;
    case NEBULA_MOVIE_AUDIO_CLOSE:
        movie.close();
        break;
    case NEBULA_MOVIE_AUDIO_RESET:
        movie.reset();
        break;
    case NEBULA_MOVIE_AUDIO_VOLUME:
        movie.set_volume(value, milliseconds);
        break;
    case NEBULA_MOVIE_AUDIO_RESUME:
        movie.resume();
        break;
    default:
        break;
    }
}


// The device stopped while the app is out of sight (the player's OS may stop it anyway).
void NativeGameAudio::set_paused(bool paused) {
    if (state_->device != nullptr) {
        state_->device->set_paused(paused);
    }
}

std::string NativeGameAudio::stats_line() const {
    if (state_->mixer == nullptr) {
        return "not set up";
    }
    const galaxy::audio::AudioMixerStats& s = state_->mixer->stats();
    std::ostringstream line;
    line << "rate=" << state_->mixer->rate() << " frames=" << s.frames << " subframes=" << s.subframes
         << " voice-renders=" << s.voice_renders << " pcm=" << s.pcm_renders
         << " oscillators=" << s.oscillator_renders << " noise=" << s.noise_renders << " fir=" << s.fir_renders
         << " iir=" << s.iir_renders << " panner=" << s.panner_renders << " effect-subframes=" << s.effect_subframes
         << " stream-frames=" << s.stream_frames << " unresolved=" << s.unresolved_reads
         << " mix-ms=" << s.mix_ns / 1000000u;
    return line.str();
}

}  // namespace galaxy::host
