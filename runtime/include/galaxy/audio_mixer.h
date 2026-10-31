// The game's sound mixed natively (nebula_game_api.h audio_*): the game's 64 voices and four effect lines, mixed in
// float at the output device's rate. Each audio_mix call is one subframe (1/400 s); audio_frame ends a frame with the
// movie player's 32 kHz samples and hands the frame to the output.
//
// Per voice: the source (ADPCM blocks, PCM8/PCM16, or an oscillator) is read at its own rate with the record's
// position bookkeeping (the stream player reads and moves it), resampled to the output rate with a 4-tap
// Kaiser-windowed sinc (cutoff 0.753 of the source's Nyquist, beta 7.05: the response the game's sound was made
// with), through its FIR and IIR filters (their 32 kHz responses carried to the output rate), then into the buses with
// ramped volumes or through the surround panner (quarter-sine pan law). The effect lines are feedback delays with an
// 8-tap FIR; the surround buses are matrix-encoded into left and right one subframe later; center and the inverted
// right join at the output. Volume categories (music, voices, effects, ambience, cutscenes) scale each voice.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "galaxy/native_audio_bus.h"

#include <nebula_game_api.h>

namespace galaxy::audio {

struct AudioMixerStats {
    std::uint64_t setups{};
    std::uint64_t frames{};
    std::uint64_t subframes{};
    std::uint64_t voice_renders{};
    std::uint64_t adpcm_blocks{};
    std::uint64_t pcm_renders{};
    std::uint64_t oscillator_renders{};
    std::uint64_t noise_renders{};
    std::uint64_t fir_renders{};
    std::uint64_t iir_renders{};
    std::uint64_t panner_renders{};
    std::uint64_t effect_subframes{};
    std::uint64_t stream_frames{};
    std::uint64_t unresolved_reads{};
    std::uint64_t category_decisions{};
    std::uint64_t category_unnamed{};
    std::uint64_t mix_ns{};
};

// A voice's volume category and the owner it was decided from.
struct AudioVoiceCategory {
    NativeAudioBusDecision decision{};
    JAudioChannelOwner owner{};
};

// A first-in first-out delay of whole samples (the surround fold and the panner's effect sends).
class SampleDelay {
public:
    void reset(std::size_t delay);
    // Writes `count` samples and reads the `count` samples `delay` behind them.
    void process(const float* in, float* out, std::size_t count);

private:
    std::vector<float> ring_;
    std::size_t delay_{};
    std::size_t read_{};
};

// Polyphase windowed-sinc interpolation: `taps` weights per phase, each phase normalised to unity gain.
class InterpolationKernel {
public:
    InterpolationKernel(std::uint32_t taps, double cutoff, double beta, std::uint32_t phases);
    // The weights for an output at fraction `fraction` (0..1) past tap taps/2 - 1.
    [[nodiscard]] const float* weights(double fraction) const noexcept;
    [[nodiscard]] std::uint32_t taps() const noexcept { return taps_; }

private:
    std::uint32_t taps_;
    std::uint32_t phases_;
    std::vector<float> table_;
};

class AudioMixer {
public:
    static constexpr std::uint32_t kSubframesPerSecond = 400u;
    static constexpr std::uint32_t kGameRate = 32000u;
    static constexpr std::uint32_t kGameSubframeSamples = 80u;

    // `rate`: the output rate in Hz (8000..192000).
    explicit AudioMixer(std::uint32_t rate);

    void setup(NebulaAudioVoice* voices, std::uint32_t count, const NebulaAudioEffect* effects,
               const std::byte* sound_memory, std::uint32_t sound_memory_size);
    // One subframe at `gain` into the frame being built.
    void mix(float gain);
    // Ends the frame: `stream_frames` pairs (right, left) at 32 kHz are added over the subframes mixed since the last
    // call; returns that output, interleaved (left, right), valid until the next call.
    std::span<const float> end_frame(const std::int16_t* stream, std::uint32_t stream_frames);

    // Volume categories: `master` unused (the output applies it). A voice's category follows the sound the game names
    // on it (sound_route, sound_id), else its source's format; a changed gain ramps over 10 ms.
    void set_category_gains(const NativeAudioBusGains& gains) noexcept;

    [[nodiscard]] std::uint32_t rate() const noexcept { return rate_; }
    [[nodiscard]] bool configured() const noexcept { return voices_ != nullptr; }
    [[nodiscard]] const AudioVoiceCategory& voice_category(std::uint32_t voice) const noexcept {
        return categories_[voice < categories_.size() ? voice : 0u];
    }
    [[nodiscard]] const AudioMixerStats& stats() const noexcept { return stats_; }

private:
    static constexpr std::size_t kMaxSubframe = 512u;  // output samples of one subframe at 192 kHz, rounded up
    static constexpr std::size_t kVoices = 64u;
    static constexpr std::uint32_t kMaxSourceSamples = 4u + 0x7FFFu * 80u / 4096u + 1u;

    struct Biquad {
        float b0{1.0f}, b1{}, b2{}, a1{}, a2{};
        float x1{}, x2{}, y1{}, y2{};
    };

    struct VoiceState {
        std::uint32_t phase{};            // 12-bit position between source samples
        std::array<float, 4> history{};   // the previous subframe's last source samples
        std::array<std::int16_t, 16> block{};  // the decoded ADPCM block; [14], [15] the predictor's history
        bool loop_pending{};              // PCM: the end was reached exactly; loop or finish at the next read
        float last{};                     // the last resampled sample, held while paused
        double cycle{};                   // oscillators: position in the waveform's cycle
        float noise{};                    // noise oscillators: the held value and its step
        std::uint32_t noise_step{};
        std::uint32_t noise_seed{0x9E3779B9u};
        // FIR at the output rate, rebuilt when the game's taps or coefficients change.
        std::uint16_t fir_key_mode{0xFFFFu};
        std::array<std::int16_t, 8> fir_key{};
        std::vector<float> fir;           // weights by delay in output samples (index 0: one sample back)
        std::vector<float> fir_history;   // newest at the back
        // IIR at the output rate, rebuilt when the coefficients change.
        std::array<std::int16_t, 4> iir_key{};
        bool iir_valid{};
        Biquad iir;
        // Volume category gain.
        float category_gain{1.0f};
        float category_target{1.0f};
        std::uint32_t category_ramp{};
        NativeAudioBus category_bus{NativeAudioBus::Sfx};
        bool category_set{};
        std::uint32_t category_key_id{0xFFFFFFFFu};
        std::uint16_t category_key_route{0xFFFFu};
        std::uint16_t category_key_format{0xFFFFu};
    };

    struct EffectState {
        std::uint32_t generation{};
        std::uint16_t subframes{};
        std::vector<float> delay;          // the delay line
        std::size_t position{};
        std::vector<float> history_a;      // the FIR's input history (before the sends)
        std::vector<float> history_b;      // (after the sends)
        std::vector<float> fir;            // weights by delay in output samples
        std::array<std::int16_t, 8> fir_key{};
        std::array<float, kMaxSubframe> input{};  // what goes into the delay at the end of the subframe
    };

    enum Bus : std::uint32_t {
        kLeft = NEBULA_AUDIO_BUS_LEFT,
        kRight = NEBULA_AUDIO_BUS_RIGHT,
        kEffect0 = NEBULA_AUDIO_BUS_EFFECT0,
        kSurroundRight = NEBULA_AUDIO_BUS_SURROUND_RIGHT,
        kSurroundLeft = NEBULA_AUDIO_BUS_SURROUND_LEFT,
        kRightInverted = NEBULA_AUDIO_BUS_RIGHT_INVERTED,
        kDiscard = NEBULA_AUDIO_BUS_DISCARD,
        kCenter = NEBULA_AUDIO_BUS_CENTER,
        kPannerSendLeft = NEBULA_AUDIO_BUSES,  // the panner's rear effect sends
        kPannerSendRight,
        kBusCount,
    };

    [[nodiscard]] float* bus(std::uint32_t index) noexcept;
    void classify(std::uint32_t index, const NebulaAudioVoice& voice);
    void render_voice(std::uint32_t index, NebulaAudioVoice& voice);
    void render_source(VoiceState& state, NebulaAudioVoice& voice, float* out);
    void decode_adpcm(VoiceState& state, NebulaAudioVoice& voice, float* out, std::uint32_t count);
    void decode_adpcm_blocks(VoiceState& state, NebulaAudioVoice& voice, std::int16_t* out, std::uint32_t blocks);
    void read_pcm(VoiceState& state, NebulaAudioVoice& voice, float* out, std::uint32_t count, std::uint32_t bytes);
    void render_oscillator(VoiceState& state, const NebulaAudioVoice& voice, float* out);
    void apply_fir(VoiceState& state, const NebulaAudioVoice& voice, float* samples);
    void apply_iir(VoiceState& state, const NebulaAudioVoice& voice, float* samples);
    void apply_category(std::uint32_t index, VoiceState& state, const NebulaAudioVoice& voice, float* samples);
    void mix_sends(NebulaAudioVoice& voice, const float* samples);
    void mix_panner(NebulaAudioVoice& voice, const float* samples);
    // destination += source * (start + ramp over the first 4/5 of the subframe) / 65536
    void ramp_mix(float* destination, const float* source, float start, float delta) const;
    void effects_begin();
    void effects_end();
    [[nodiscard]] const std::byte* wave_bytes(std::uint32_t offset, std::uint32_t size) const;
    [[nodiscard]] std::uint32_t game_to_output_delay(double game_samples) const noexcept;

    std::uint32_t rate_;
    double game_ratio_;  // output samples per 32 kHz sample
    InterpolationKernel voice_kernel_;
    InterpolationKernel stream_kernel_;

    NebulaAudioVoice* voices_{};
    std::uint32_t voice_count_{};
    const NebulaAudioEffect* effects_{};
    const std::byte* sound_{};
    std::uint32_t sound_size_{};

    std::uint64_t subframe_counter_{};
    std::uint32_t samples_{};  // this subframe's output samples
    std::array<std::array<float, kMaxSubframe>, kBusCount> buses_{};
    std::array<float, kMaxSubframe> voice_out_{};
    std::vector<float> source_;
    std::array<VoiceState, kVoices> voice_states_{};
    std::array<EffectState, 4> effect_states_{};
    SampleDelay fold_left_, fold_right_;           // the surround buses, one subframe late
    SampleDelay panner_send_left_, panner_send_right_;  // the panner's rear sends: one subframe, half a subframe
    std::vector<float> frame_;                     // the frame's output so far, interleaved (left, right)

    // The movie player's stream: its last samples and the position between them.
    std::vector<float> stream_history_;  // interleaved (left, right)
    double stream_position_{};
    bool stream_running_{};

    std::array<float, 5> category_gains_{1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    bool category_gains_set_{};
    std::array<AudioVoiceCategory, kVoices> categories_{};
    AudioMixerStats stats_{};
};

}  // namespace galaxy::audio
