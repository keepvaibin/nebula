// The game's sound on every host (native_audio.h): the native mixer's frames at the master volume with the remote speaker
// added, pulled by the output device (audio_device.h), and the WAV dump the tests record.
#include "galaxy/native_audio.h"
#include "galaxy/run_options.h"

#include "galaxy/audio_device.h"
#include "galaxy/audio_mixer.h"
#include "galaxy/path_text.h"
#include "galaxy/platform_time.h"
#include "galaxy/runtime_settings.h"
#include "galaxy/wiimote_speaker.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace galaxy::host {
namespace {

// GALAXY_AUDIO_WAV_DUMP=<path>: the output as a 16-bit stereo WAV file, at most 15 seconds. A sample rate change
// starts a new file beside it (<name>.rate-<hz>.part-<n>.wav).
class AudioWavDump {
public:
    ~AudioWavDump() {
        finish();
    }

    AudioWavDump(const AudioWavDump&) = delete;
    AudioWavDump& operator=(const AudioWavDump&) = delete;

    AudioWavDump() = default;

    bool append(std::span<const std::byte> pcm_le, std::uint32_t sample_rate) {
        return append_locked(pcm_le, sample_rate);
    }

    bool finish() {
        return finish_locked();
    }

private:
    // 15 s, or GALAXY_AUDIO_WAV_DUMP_SECONDS (up to an hour) for a longer recording.
    static std::uint32_t max_seconds() {
        static const std::uint32_t seconds = [] {
            unsigned long value = 15ul;
            if (const char* const text = ::galaxy::run_options().audio_wav_dump_seconds) {
                const unsigned long parsed = std::strtoul(text, nullptr, 10);
                if (parsed > 0ul && parsed <= 3600ul) value = parsed;
            }
            return static_cast<std::uint32_t>(value);
        }();
        return seconds;
    }

    std::uint32_t max_bytes() const {
        return sample_rate_ * 4u * max_seconds();
    }

    bool append_locked(std::span<const std::byte> pcm_le, std::uint32_t sample_rate) {
        if (out_.is_open() && sample_rate_ != sample_rate) {
            if (!finish_locked()) {
                return false;
            }
            data_bytes_ = 0;
            open_attempted_ = false;
            ++segment_index_;
        }
        if (!ensure_open(sample_rate)) {
            return false;
        }
        if (pcm_le.empty()) {
            return true;
        }
        if (data_bytes_ >= max_bytes()) {
            return true;
        }
        std::size_t write_size = std::min<std::size_t>(
            pcm_le.size(),
            static_cast<std::size_t>(max_bytes() - data_bytes_));
        write_size &= ~std::size_t{3};
        if (write_size == 0) {
            return true;
        }
        out_.write(
            reinterpret_cast<const char*>(pcm_le.data()),
            static_cast<std::streamsize>(write_size));
        if (!out_) {
            std::cerr << "[audio] WAV dump write failed: " << galaxy::path_text(path_) << '\n';
            finish_locked();
            disabled_ = true;
            return false;
        }
        data_bytes_ += static_cast<std::uint32_t>(write_size);
        return true;
    }

    bool finish_locked() {
        if (!out_.is_open()) {
            return !disabled_;
        }
        out_.seekp(4, std::ios::beg);
        write_u32_le(out_, 36u + data_bytes_);
        out_.seekp(40, std::ios::beg);
        write_u32_le(out_, data_bytes_);
        out_.flush();
        const bool contents_written = static_cast<bool>(out_);
        out_.close();
        if (!contents_written || !out_) {
            disabled_ = true;
            std::cerr << "[audio] WAV dump finalization failed: " << galaxy::path_text(path_) << '\n';
            return false;
        }
        std::cout << "[audio] WAV dump wrote " << data_bytes_
                  << " bytes at " << sample_rate_ << " Hz: " << galaxy::path_text(path_)
                  << '\n';
        return true;
    }

    static void write_u16_le(std::ostream& out, std::uint16_t value) {
        const char bytes[2] = {
            static_cast<char>(value & 0xFFu),
            static_cast<char>((value >> 8) & 0xFFu),
        };
        out.write(bytes, sizeof(bytes));
    }

    static void write_u32_le(std::ostream& out, std::uint32_t value) {
        const char bytes[4] = {
            static_cast<char>(value & 0xFFu),
            static_cast<char>((value >> 8) & 0xFFu),
            static_cast<char>((value >> 16) & 0xFFu),
            static_cast<char>((value >> 24) & 0xFFu),
        };
        out.write(bytes, sizeof(bytes));
    }

    bool ensure_open(std::uint32_t sample_rate) {
        if (disabled_) {
            return false;
        }
        if (out_.is_open()) {
            return true;
        }
        if (open_attempted_) {
            return false;
        }
        open_attempted_ = true;

#if defined(_WIN32)
        const wchar_t* const path = ::galaxy::run_options().audio_wav_dump_w;
        if (path == nullptr || path[0] == L'\0') {
#else
        const char* const path = ::galaxy::run_options().audio_wav_dump;
        if (path == nullptr || path[0] == '\0') {
#endif
            disabled_ = true;
            return false;
        }

        if (base_path_.empty()) {
            base_path_ = std::filesystem::path(path);
        }
        path_ = base_path_;
        if (segment_index_ != 0u) {
            const std::wstring extension = base_path_.extension().wstring();
            const std::wstring filename =
                base_path_.stem().wstring() + L".rate-" +
                std::to_wstring(sample_rate) + L".part-" +
                std::to_wstring(segment_index_) + extension;
            path_ = base_path_.parent_path() / filename;
        }
        std::error_code ec;
        const std::filesystem::path parent = path_.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, ec);
            if (ec) {
                std::cerr << "[audio] WAV dump directory failed: "
                          << galaxy::path_text(parent) << " error=" << ec.message() << '\n';
                disabled_ = true;
                return false;
            }
        }

        sample_rate_ = sample_rate;
        out_.open(path_, std::ios::binary | std::ios::trunc);
        if (!out_) {
            std::cerr << "[audio] WAV dump open failed: " << galaxy::path_text(path_) << '\n';
            disabled_ = true;
            return false;
        }

        out_.write("RIFF", 4);
        write_u32_le(out_, 36u);
        out_.write("WAVEfmt ", 8);
        write_u32_le(out_, 16u);
        write_u16_le(out_, 1u);
        write_u16_le(out_, 2u);
        write_u32_le(out_, sample_rate);
        write_u32_le(out_, sample_rate * 4u);
        write_u16_le(out_, 4u);
        write_u16_le(out_, 16u);
        out_.write("data", 4);
        write_u32_le(out_, 0u);
        if (!out_) {
            std::cerr << "[audio] WAV dump header failed: " << galaxy::path_text(path_) << '\n';
            finish_locked();
            disabled_ = true;
            return false;
        }

        // The first sample's time, to line a recording's audio up with its snapshots ("[snapshot] ... qpc=").
        std::cout << "[audio] WAV dump enabled: " << galaxy::path_text(path_)
                  << " max-bytes=" << max_bytes() << " qpc=" << galaxy::qpc_now() << '\n';
        return true;
    }

    std::ofstream out_;
    std::filesystem::path base_path_;
    std::filesystem::path path_;
    std::uint32_t data_bytes_{};
    std::uint32_t sample_rate_{};
    std::uint32_t segment_index_{};
    bool disabled_{};
    bool open_attempted_{};
};

// A test's devices, one after another (GALAXY_AUDIO_TEST_DEVICE_CYCLE=<seconds each>): other rates, more speakers.
struct TestDevice {
    std::uint32_t rate;
    std::span<const Speaker> speakers;
};
constexpr Speaker kTestStereo[] = {Speaker::Left, Speaker::Right};
constexpr Speaker kTestQuad[] = {Speaker::Left, Speaker::Right, Speaker::BackLeft, Speaker::BackRight};
constexpr Speaker kTest51[] = {Speaker::Left, Speaker::Right, Speaker::Center, Speaker::Other, Speaker::BackLeft, Speaker::BackRight};
constexpr Speaker kTest71[] = {Speaker::Left, Speaker::Right, Speaker::Center, Speaker::Other, Speaker::BackLeft, Speaker::BackRight,
                               Speaker::SideLeft, Speaker::SideRight};
constexpr TestDevice kTestDevices[] = {{48000u, kTestStereo}, {44100u, kTestStereo}, {96000u, kTest51},
                                       {48000u, kTest71},     {32000u, kTestQuad},   {192000u, kTest51}};

// No output: pulled by the clock, as a device would.
class NullAudioDevice final : public AudioDevice {
public:
    explicit NullAudioDevice(std::uint32_t rate) : rate_(rate) {}

    [[nodiscard]] std::uint32_t rate() const override { return rate_.load(std::memory_order_relaxed); }

    [[nodiscard]] std::span<const Speaker> speakers() const noexcept override {
        return surround_.load(std::memory_order_relaxed) ? speakers_ : AudioDevice::speakers();
    }

    void set_surround(bool on) override { surround_.store(on, std::memory_order_relaxed); }

    void start(Render render) override {
        std::thread([this, render = std::move(render)] {
            set_audio_thread_name();
            constexpr std::uint32_t kStepsPerSecond = 200u;
            std::vector<float> out;
            const char* const cycle = ::galaxy::run_options().audio_test_device_cycle;
            const unsigned steps_each = cycle != nullptr ? static_cast<unsigned>(std::strtoul(cycle, nullptr, 10)) * kStepsPerSecond : 0u;
            unsigned steps = 0u;
            unsigned device = 0u;
            std::vector<double> power;
            auto next = std::chrono::steady_clock::now();
            for (;;) {
                if (steps_each != 0u && ++steps >= steps_each) {
                    std::ostringstream line;
                    line << "[audio] test device " << rate() << " Hz, levels";
                    for (const double value : power) {
                        line << ' ' << std::sqrt(value / steps);
                    }
                    std::cout << line.str() << std::endl;
                    power.clear();
                    steps = 0u;
                    device = (device + 1u) % static_cast<unsigned>(std::size(kTestDevices));
                    rate_.store(kTestDevices[device].rate, std::memory_order_relaxed);
                    speakers_ = kTestDevices[device].speakers;
                }
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
                    if (steps_each != 0u) {
                        power.resize(channels);
                        for (std::size_t i = 0; i < out.size(); ++i) {
                            power[i % channels] += static_cast<double>(out[i]) * out[i] / frames;
                        }
                    }
                }
            }
        }).detach();
    }

    void set_paused(bool paused) override { paused_.store(paused, std::memory_order_relaxed); }

private:
    std::atomic<std::uint32_t> rate_;
    std::atomic<bool> paused_{false};
    std::atomic<bool> surround_{false};
    std::span<const Speaker> speakers_{kTestStereo};
};

}  // namespace

std::unique_ptr<AudioDevice> open_null_audio_device(std::uint32_t rate) {
    return std::make_unique<NullAudioDevice>(rate);
}

struct NativeGameAudio::State {
    std::unique_ptr<AudioDevice> device;
    std::unique_ptr<galaxy::audio::AudioMixer> mixer;
    AudioWavDump wav;
    float master{1.0f};
    bool master_started{};
    galaxy::SoundOutput sound_output{galaxy::SoundOutput::Stereo};
    std::vector<std::byte> wav_block;
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
        // GALAXY_AUDIO_WAV_DUMP_ONLY: the clock pulls, nothing reaches a device.
        s.device = ::galaxy::run_option_flag(::galaxy::run_options().audio_wav_dump_only, false) ? open_null_audio_device(rate) : open_audio_device();
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

void NativeGameAudio::mix(float gain) {
    if (state_->mixer != nullptr) {
        state_->mixer->mix(gain);
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
    const float target = audio.master >= 0.0f ? std::min(audio.master, 4.0f) : 0.0f;
    const float start = s.master_started ? s.master : target;
    s.master_started = true;
    s.master = target;
    const std::size_t frames = samples.size() / 2u;
    if (frames == 0u) {
        return;
    }
    const std::size_t at = s.pending.size();
    s.pending.resize(at + frames * 2u);
    const std::span<float> out(s.pending.data() + at, frames * 2u);
    for (std::size_t i = 0; i < frames; ++i) {
        const float gain = start + (target - start) * static_cast<float>(i + 1u) / static_cast<float>(frames);
        out[i * 2u] = samples[i * 2u] * gain;
        out[i * 2u + 1u] = samples[i * 2u + 1u] * gain;
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
    galaxy::input::speaker::game_speaker().mix(out, s.mixer->rate(), audio.wiimote_speaker);
    if (audio.sound_output == galaxy::SoundOutput::Mono) {
        for (std::size_t i = 0; i < frames; ++i) {
            const float both = 0.5f * (out[i * 2u] + out[i * 2u + 1u]);
            out[i * 2u] = both;
            out[i * 2u + 1u] = both;
        }
    }
    static const bool trace_first = ::galaxy::run_options().trace_file_reads != nullptr;
    static bool first_sound = false;
    if (trace_first && !first_sound && std::any_of(out.begin(), out.end(), [](float value) { return value != 0.0f; })) {
        first_sound = true;
        std::fprintf(stderr, "[read] first sound\n");
    }
    static const bool dump = ::galaxy::run_options().audio_wav_dump != nullptr;
    if (dump) {
        s.wav_block.resize(frames * 4u);
        for (std::size_t i = 0; i < frames * 2u; ++i) {
            const float value = std::clamp(out[i] * 32768.0f, -32768.0f, 32767.0f);
            const auto bits = static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lrint(value)));
            s.wav_block[i * 2u] = static_cast<std::byte>(bits & 0xFFu);
            s.wav_block[i * 2u + 1u] = static_cast<std::byte>(bits >> 8u);
        }
        (void)s.wav.append(s.wav_block, s.mixer->rate());
    }
    for (float& value : out) {
        value = std::clamp(value, -1.0f, 1.0f);
    }
    if (s.device == nullptr) {
        s.pending.clear();  // nothing pulls: the frame goes no further than the dump
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
