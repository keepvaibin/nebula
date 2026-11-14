// The game's sound mixed natively (nebula_game_api.h audio_*): the game's voices and four effect lines, mixed in
// float at the output device's rate. Each voices_mix call is one tick (1/400 s); voices_frame ends a frame and
// hands it to the output.
//
// Per voice: the source (PCM16 or an oscillator) is read at its own rate with the record's
// position bookkeeping (the stream player reads and moves it), resampled to the output rate with a 4-tap
// Kaiser-windowed sinc (cutoff 0.753 of the source's Nyquist, beta 7.05: the response the game's sound was made
// with), through its FIR and IIR filters (their 32 kHz responses carried to the output rate), then into the buses with
// ramped volumes or through the surround panner (quarter-sine pan law). The effect lines are feedback delays with an
// 8-tap FIR; the surround buses are matrix-encoded into left and right one subframe later; center and the inverted
// right join at the output. Volume categories (music, voices, effects, ambience, cutscenes) scale each voice. The
// movie player's sound (movie_audio.h) joins each frame at 32 kHz through a 128-tap windowed sinc.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "galaxy/movie_audio.h"
#include "galaxy/native_audio_bus.h"

#include <nebula_game_api.h>

namespace galaxy::audio {

struct AudioMixerStats {
    std::uint64_t setups{};
    std::uint64_t frames{};
    std::uint64_t subframes{};
    std::uint64_t voice_renders{};
    std::uint64_t pcm_renders{};
    std::uint64_t oscillator_renders{};
    std::uint64_t noise_renders{};
    std::uint64_t fir_renders{};
    std::uint64_t iir_renders{};
    std::uint64_t panner_renders{};
    std::uint64_t effect_subframes{};
    std::uint64_t stream_frames{};
    std::uint64_t unresolved_reads{};
    std::uint64_t mix_ns{};
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
    // The rate the sound data's filters, effect delays and movie streams were made at.
    static constexpr std::uint32_t kAuthoredRate = 32000u;
    static constexpr std::uint32_t kAuthoredTickSamples = kAuthoredRate / kSubframesPerSecond;

    // `rate`: the output rate in Hz (8000..192000).
    explicit AudioMixer(std::uint32_t rate);

    void setup(NebulaVoice* voices, std::uint32_t count, const NebulaEffectLine* effects);
    // The output's rate changed (another device): filters and delays start over at it, the voices play on.
    void set_rate(std::uint32_t rate);
    // One subframe of the first `used` voices at `gain` into the frame being built.
    void mix(float gain, std::uint32_t used = 0xFFFFFFFFu);
    // Ends the frame: the movie's sound for the subframes mixed since the last call is added; returns that output,
    // interleaved (left, right), valid until the next call.
    std::span<const float> end_frame();
    // The surround buses and center on speakers of their own instead of matrix-encoded into left and right. The frame
    // being built starts over.
    void set_discrete(bool on) noexcept;
    [[nodiscard]] bool discrete() const noexcept { return discrete_; }
    // With set_discrete: the frame end_frame returned, its center, surround left and surround right, interleaved.
    [[nodiscard]] std::span<const float> surround() const noexcept { return surround_out_; }
    [[nodiscard]] MovieAudio& movie() noexcept { return movie_; }

    // Volume categories: `master` unused (the output applies it). A voice's category follows the sound the game names
    // on it (sound_route, sound_id), else its source's format; a changed gain ramps over 10 ms.
    void set_category_gains(const NativeAudioBusGains& gains) noexcept;

    [[nodiscard]] std::uint32_t rate() const noexcept { return rate_; }
    [[nodiscard]] bool configured() const noexcept { return voices_ != nullptr; }
    [[nodiscard]] const AudioMixerStats& stats() const noexcept { return stats_; }

private:
    static constexpr std::size_t kMaxSubframe = 512u;  // output samples of one subframe at 192 kHz, rounded up
    static constexpr std::uint32_t kVoiceTaps = 16u;
    // At most 16 source samples per 32 kHz sample, the history before them and the last taps' reach.
    static constexpr std::uint32_t kMaxSourceSpan = 1280u;
    static constexpr std::uint32_t kMaxSourceSamples = kVoiceTaps + kMaxSourceSpan + kVoiceTaps;

    struct Biquad {
        float b0{1.0f}, b1{}, b2{}, a1{}, a2{};
        float x1{}, x2{}, y1{}, y2{};
    };

    struct VoiceState {
        double phase{};                   // the position between source samples
        std::array<float, kVoiceTaps> history{};   // the previous subframe's last source samples
            bool loop_pending{};              // PCM: the end was reached exactly; loop or finish at the next read
        float last{};                     // the last resampled sample, held while paused
        double cycle{};                   // oscillators: position in the waveform's cycle
        float noise{};                    // noise oscillators: the held value and its step
        std::uint32_t noise_step{};
        std::uint32_t noise_seed{0x9E3779B9u};
        // FIR at the output rate, rebuilt when the game's taps or coefficients change.
        std::uint16_t fir_key_mode{0xFFFFu};
        std::array<float, 8> fir_key{};
        std::vector<float> fir;           // weights by delay in output samples (index 0: one sample back)
        std::vector<float> fir_history;   // newest at the back
        // IIR at the output rate, rebuilt when the coefficients change.
        std::array<float, 4> iir_key{};
        bool iir_valid{};
        Biquad iir;
        // Volume category gain.
        float category_gain{1.0f};
        float category_target{1.0f};
        std::uint32_t category_ramp{};
        std::uint8_t category_bus{NEBULA_SOUND_EFFECT};
        bool category_set{};
    };

    struct EffectState {
        std::uint32_t generation{};
        std::uint16_t subframes{};
        std::vector<float> delay;          // the delay line
        std::size_t position{};
        std::vector<float> history_a;      // the FIR's input history (before the sends)
        std::vector<float> history_b;      // (after the sends)
        std::vector<float> fir;            // weights by delay in output samples
        std::array<float, 8> fir_key{};
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
    void render_voice(std::uint32_t index, NebulaVoice& voice);
    void render_source(VoiceState& state, NebulaVoice& voice, float* out);
    void read_pcm(VoiceState& state, NebulaVoice& voice, float* out, std::uint32_t count);
    void render_oscillator(VoiceState& state, const NebulaVoice& voice, float* out);
    void apply_fir(VoiceState& state, const NebulaVoice& voice, float* samples);
    void apply_iir(VoiceState& state, const NebulaVoice& voice, float* samples);
    void apply_category(VoiceState& state, const NebulaVoice& voice, float* samples);
    void mix_sends(NebulaVoice& voice, const float* samples);
    void mix_panner(NebulaVoice& voice, const float* samples);
    // destination += source * (start + ramp over the first 4/5 of the subframe) / 65536
    void ramp_mix(float* destination, const float* source, float start, float delta) const;
    // ramp_'s positions for this subframe's sample count.
    void make_ramp();
    void effects_begin();
    void effects_end();
    [[nodiscard]] const std::byte* wave_bytes(const NebulaVoice& voice, std::uint32_t offset,
                                              std::uint32_t size) const;
    [[nodiscard]] std::uint32_t authored_to_output_delay(double authored_samples) const noexcept;

    std::uint32_t rate_;
    double authored_ratio_;  // output samples per 32 kHz sample
    std::array<InterpolationKernel, 4> voice_kernels_;
    InterpolationKernel stream_kernel_;

    NebulaVoice* voices_{};
    std::uint32_t voice_count_{};
    const NebulaEffectLine* effects_{};

    std::uint64_t subframe_counter_{};
    std::uint32_t samples_{};  // this subframe's output samples
    // The volume ramp's position (0..1) at each output sample, the same for every send of a subframe of ramp_samples_.
    std::array<float, kMaxSubframe> ramp_{};
    std::uint32_t ramp_samples_{};
    std::array<std::array<float, kMaxSubframe>, kBusCount> buses_{};
    std::array<float, kMaxSubframe> voice_out_{};
    std::vector<float> source_;
    std::vector<VoiceState> voice_states_;
    std::array<EffectState, 4> effect_states_{};
    SampleDelay fold_left_, fold_right_;           // the surround buses, one subframe late
    SampleDelay panner_send_left_, panner_send_right_;  // the panner's rear sends: one subframe, half a subframe
    std::vector<float> frame_;                     // the frame's output so far, interleaved (left, right)
    std::vector<float> frame_surround_, surround_out_;
    double stream_place_ = 0.0;
    bool discrete_ = false;

    // The movie player's sound and its last 32 kHz samples, interleaved (left, right).
    MovieAudio movie_;
    std::vector<float> stream_history_;
    std::vector<float> stream_input_;  // a frame's history then its new samples; kept for its capacity
    bool stream_running_{};
    std::uint32_t frame_subframes_{};

    std::array<float, 5> category_gains_{1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    bool category_gains_set_{};
    AudioMixerStats stats_{};
};

}  // namespace galaxy::audio
