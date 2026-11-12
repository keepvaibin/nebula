#include "galaxy/audio_mixer.h"
#include "galaxy/loss_report.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace galaxy::audio {
namespace {

constexpr double kPi = 3.14159265358979323846;

// Mix constants (15-bit fractions): the surround matrix and the center's -3 dB.
constexpr float kUnit = 32767.0f / 32768.0f;
constexpr float kFold = 26500.0f / 32768.0f;
constexpr float kMatrix = 18400.0f / 32768.0f;
constexpr float kCenterGain = 23170.0f / 32768.0f;

// A forced stop's next gain: half, down to nothing below the 16-bit output's step.
[[nodiscard]] float halved(float gain) noexcept {
    return gain < 1.0f / 32768.0f ? 0.0f : gain * 0.5f;
}

[[nodiscard]] double bessel_i0(double x) noexcept {
    double sum = 1.0;
    double term = 1.0;
    for (int k = 1; k < 40; ++k) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
    }
    return sum;
}

// The 64-step waveform tables of the table oscillators, one cycle each.
[[nodiscard]] float table_wave(bool triangle, std::uint32_t step) noexcept {
    if (triangle) {
        const int i = static_cast<int>(step);
        const int value = i <= 16 ? i : (i <= 48 ? 32 - i : i - 64);
        return static_cast<float>(value) / 16.0f * kUnit;
    }
    return static_cast<float>(std::sin(2.0 * kPi * step / 64.0)) * kUnit;
}

// The panner's quarter-sine law: gain for position 0..1.
[[nodiscard]] float pan_gain(float position) noexcept {
    return static_cast<float>(std::sin(kPi * position / 2.0));
}

// Weights by delay in output samples (index 0: the current sample) for taps at `delays` (in 32 kHz samples, may be
// fractional at the output rate), each split linearly between its two neighbouring output samples.
[[nodiscard]] std::vector<float> delays_to_weights(const float* coefficients, const double* delays, std::size_t count,
                                                   double ratio) {
    double longest = 0.0;
    for (std::size_t k = 0; k < count; ++k) {
        longest = std::max(longest, delays[k] * ratio);
    }
    std::vector<float> weights(static_cast<std::size_t>(std::ceil(longest)) + 2u, 0.0f);
    for (std::size_t k = 0; k < count; ++k) {
        const double at = delays[k] * ratio;
        const auto whole = static_cast<std::size_t>(at);
        const double fraction = at - static_cast<double>(whole);
        weights[whole] += static_cast<float>(coefficients[k] * (1.0 - fraction));
        weights[whole + 1u] += static_cast<float>(coefficients[k] * fraction);
    }
    while (weights.size() > 1u && weights.back() == 0.0f) {
        weights.pop_back();
    }
    return weights;
}

// y[n] = sum weights[m] x[n - m], with `history` holding the previous inputs (newest at the back, resized to the
// weights' reach).
void run_fir(const std::vector<float>& weights, std::vector<float>& history, float* samples, std::size_t count) {
    const std::size_t reach = weights.size();
    if (history.size() != reach) {
        history.assign(reach, 0.0f);
    }
    // Only [0, reach + count) is read, all of it written first: no fill.
    std::array<float, 1024> x;
    std::copy(history.begin(), history.end(), x.begin());
    std::copy_n(samples, count, x.begin() + static_cast<std::ptrdiff_t>(reach));
    for (std::size_t n = 0; n < count; ++n) {
        float sum = 0.0f;
        const float* now = x.data() + reach + n;
        for (std::size_t m = 0; m < reach; ++m) {
            sum += weights[m] * now[-static_cast<std::ptrdiff_t>(m)];
        }
        samples[n] = sum;
    }
    std::copy_n(x.begin() + static_cast<std::ptrdiff_t>(count), reach, history.begin());
}

}  // namespace

void SampleDelay::reset(std::size_t delay) {
    delay_ = delay;
    ring_.assign(delay + 1024u, 0.0f);
    read_ = 0u;
}

void SampleDelay::process(const float* in, float* out, std::size_t count) {
    // The two places step and wrap (each wraps at most once a call: delay_ + count stays under the ring's size).
    const std::size_t size = ring_.size();
    std::size_t read = read_;
    std::size_t write = (read_ + delay_) % size;
    for (std::size_t i = 0; i < count; ++i) {
        out[i] = ring_[read];
        ring_[write] = in[i];
        read = read + 1u == size ? 0u : read + 1u;
        write = write + 1u == size ? 0u : write + 1u;
    }
    read_ = read;
}

InterpolationKernel::InterpolationKernel(std::uint32_t taps, double cutoff, double beta, std::uint32_t phases)
    : taps_(taps), phases_(phases), table_(static_cast<std::size_t>(taps) * phases) {
    const double half = taps / 2.0;
    const double i0_beta = bessel_i0(beta);
    for (std::uint32_t row = 0; row < phases; ++row) {
        const double fraction = (row + 0.5) / phases;
        double sum = 0.0;
        std::vector<double> weights(taps);
        for (std::uint32_t k = 0; k < taps; ++k) {
            const double t = static_cast<double>(k) - (half - 1.0) - fraction;
            const double x = kPi * cutoff * t;
            const double sinc = std::abs(x) < 1e-12 ? 1.0 : std::sin(x) / x;
            const double edge = t / half;
            const double window = std::abs(edge) < 1.0 ? bessel_i0(beta * std::sqrt(1.0 - edge * edge)) / i0_beta : 0.0;
            weights[k] = sinc * window;
            sum += weights[k];
        }
        for (std::uint32_t k = 0; k < taps; ++k) {
            table_[static_cast<std::size_t>(row) * taps + k] = static_cast<float>(weights[k] / sum);
        }
    }
}

const float* InterpolationKernel::weights(double fraction) const noexcept {
    const auto row = std::min<std::uint32_t>(static_cast<std::uint32_t>(fraction * phases_), phases_ - 1u);
    return table_.data() + static_cast<std::size_t>(row) * taps_;
}

AudioMixer::AudioMixer(std::uint32_t rate)
    : rate_(std::clamp<std::uint32_t>(rate, 8000u, 192000u)),
      authored_ratio_(static_cast<double>(rate_) / kAuthoredRate),
      voice_kernels_{InterpolationKernel(kVoiceTaps, 0.96, 8.0, 1024u), InterpolationKernel(kVoiceTaps, 0.72, 8.0, 1024u),
                     InterpolationKernel(kVoiceTaps, 0.48, 8.0, 1024u), InterpolationKernel(kVoiceTaps, 0.24, 8.0, 1024u)},
      // The movie's 32 kHz sound: 128 taps, cut off at 16 kHz (its third-octave band within 0.3 dB of a brick wall).
      stream_kernel_(128u, std::min(1.0, authored_ratio_), 7.0, 512u),
      source_(kMaxSourceSamples, 0.0f) {
    const std::size_t subframe = (rate_ + kSubframesPerSecond - 1u) / kSubframesPerSecond;
    fold_left_.reset(subframe);
    fold_right_.reset(subframe);
    panner_send_left_.reset(subframe);
    panner_send_right_.reset(subframe / 2u);
    frame_.reserve(subframe * 2u * 16u);
}

void AudioMixer::set_rate(std::uint32_t rate) {
    rate_ = std::clamp<std::uint32_t>(rate, 8000u, 192000u);
    authored_ratio_ = static_cast<double>(rate_) / kAuthoredRate;
    stream_kernel_ = InterpolationKernel(128u, std::min(1.0, authored_ratio_), 7.0, 512u);
    const std::size_t subframe = (rate_ + kSubframesPerSecond - 1u) / kSubframesPerSecond;
    fold_left_.reset(subframe);
    fold_right_.reset(subframe);
    panner_send_left_.reset(subframe);
    panner_send_right_.reset(subframe / 2u);
    subframe_counter_ = 0u;
    frame_.clear();
    frame_surround_.clear();
    stream_history_.clear();
    stream_running_ = false;
    for (VoiceState& state : voice_states_) {
        state.fir_key_mode = 0xFFFFu;
        state.fir_history.clear();
        state.iir_valid = false;
    }
    for (EffectState& state : effect_states_) {
        state.subframes = 0xFFFFu;
        state.fir.clear();
    }
}

void AudioMixer::setup(NebulaVoice* voices, std::uint32_t count, const NebulaEffectLine* effects) {
    ++stats_.setups;
    voices_ = voices;
    voice_count_ = count;
    voice_states_.resize(count);
    effects_ = effects;
}

void AudioMixer::set_category_gains(const NativeAudioBusGains& gains) noexcept {
    const auto clean = [](float gain) { return gain >= 0.0f ? std::min(gain, 4.0f) : 0.0f; };
    category_gains_ = {clean(gains.music), clean(gains.voice), clean(gains.sfx), clean(gains.ambience),
                       clean(gains.cutscene)};
    category_gains_set_ = true;
}

std::uint32_t AudioMixer::authored_to_output_delay(double authored_samples) const noexcept {
    return static_cast<std::uint32_t>(std::lround(authored_samples * authored_ratio_));
}

float* AudioMixer::bus(std::uint32_t index) noexcept {
    if (index >= kEffect0 && index < kEffect0 + 4u) {
        return effect_states_[index - kEffect0].input.data();
    }
    return index != NEBULA_AUDIO_BUS_NONE && index < kBusCount ? buses_[index].data() : nullptr;
}

void AudioMixer::set_discrete(bool on) noexcept {
    discrete_ = on;
    frame_.clear();
    frame_surround_.clear();
}

void AudioMixer::mix(float gain, std::uint32_t used) {
    if (!configured()) {
        return;
    }
    const auto begin = std::chrono::steady_clock::now();
    ++stats_.subframes;
    ++frame_subframes_;
    samples_ = static_cast<std::uint32_t>((subframe_counter_ + 1u) * rate_ / kSubframesPerSecond -
                                          subframe_counter_ * rate_ / kSubframesPerSecond);
    ++subframe_counter_;
    make_ramp();
    const std::size_t n = samples_;
    for (auto& b : buses_) {
        std::fill_n(b.begin(), n, 0.0f);
    }
    effects_begin();

    const std::uint32_t mixed = std::min(used, voice_count_);
    for (std::uint32_t index = 0; index < mixed; ++index) {
        NebulaVoice& voice = voices_[index];
        if (voice.active != 0u && voice.finished == 0u) {
            render_voice(index, voice);
        }
    }

    // The surround buses one subframe late, matrix-encoded into left and right unless they have their own speakers.
    // Written for [0, n) by the delays before any read: no fill.
    std::array<float, kMaxSubframe> late_left;
    std::array<float, kMaxSubframe> late_right;
    fold_left_.process(buses_[kSurroundLeft].data(), late_left.data(), n);
    fold_right_.process(buses_[kSurroundRight].data(), late_right.data(), n);
    float* left = buses_[kLeft].data();
    float* right = buses_[kRight].data();
    const float* center = buses_[kCenter].data();
    const float* inverted = buses_[kRightInverted].data();
    for (std::size_t i = 0; discrete_ && i < n; ++i) {
        right[i] -= inverted[i];
        frame_.push_back(left[i] * gain);
        frame_.push_back(right[i] * gain);
        frame_surround_.push_back(center[i] * gain);
        frame_surround_.push_back(late_left[i] * gain);
        frame_surround_.push_back(late_right[i] * gain);
    }
    for (std::size_t i = 0; !discrete_ && i < n; ++i) {
        left[i] += kFold * (kUnit * late_left[i] + kMatrix * late_right[i]) + kCenterGain * center[i];
        right[i] += kFold * (-kMatrix * late_left[i] - kUnit * late_right[i]) + kCenterGain * center[i] - inverted[i];
        frame_.push_back(left[i] * gain);
        frame_.push_back(right[i] * gain);
    }
    effects_end();
    stats_.mix_ns += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - begin).count());
}

std::span<const float> AudioMixer::end_frame() {
    ++stats_.frames;
    static thread_local std::vector<float> out;
    out.swap(frame_);
    frame_.clear();
    surround_out_.swap(frame_surround_);
    frame_surround_.clear();
    const std::size_t outputs = out.size() / 2u;
    const std::uint32_t stream_frames = frame_subframes_ * kAuthoredTickSamples;
    frame_subframes_ = 0u;
    if (!movie_.active() || stream_frames == 0u || outputs == 0u) {
        stream_running_ = false;
        return out;
    }
    ++stats_.stream_frames;
    std::vector<float> stream(static_cast<std::size_t>(stream_frames) * 2u, 0.0f);
    movie_.pull(stream.data(), stream_frames);
    // History: taps - 1 earlier samples' room, then the new ones; outputs from position taps/2 - 1.
    const std::size_t taps = stream_kernel_.taps();
    const std::size_t keep = taps + 1u;
    if (!stream_running_ || stream_history_.size() != keep * 2u) {
        stream_history_.assign(keep * 2u, 0.0f);
        stream_running_ = true;
        stream_place_ = 0.0;
    }
    std::vector<float> x(stream_history_);
    x.insert(x.end(), stream.begin(), stream.end());
    // The output's rate exactly: a block's whole outputs do not fall on whole 32 kHz samples, the remainder carries over.
    const double step = 1.0 / authored_ratio_;
    const double origin = static_cast<double>(taps / 2u) + stream_place_;
    for (std::size_t j = 0; j < outputs; ++j) {
        const double at = origin + static_cast<double>(j) * step;
        const auto whole = static_cast<std::size_t>(at);
        const float* w = stream_kernel_.weights(at - static_cast<double>(whole));
        // Under 32 kHz the carried remainder can be below -1 (a step over one sample), putting the first tap before the
        // history: the taps then start at its first sample.
        const std::size_t lead = taps / 2u - 1u;
        const std::size_t first = whole >= lead ? whole - lead : 0u;
        float l = 0.0f;
        float r = 0.0f;
        for (std::size_t k = 0; k < taps; ++k) {
            l += w[k] * x[(first + k) * 2u];
            r += w[k] * x[(first + k) * 2u + 1u];
        }
        out[j * 2u] += l;
        out[j * 2u + 1u] += r;
    }
    stream_place_ += static_cast<double>(outputs) * step - static_cast<double>(stream_frames);
    std::copy(x.end() - static_cast<std::ptrdiff_t>(keep * 2u), x.end(), stream_history_.begin());
    return out;
}

void AudioMixer::render_voice(std::uint32_t index, NebulaVoice& voice) {
    ++stats_.voice_renders;
    VoiceState& state = voice_states_[index];
    float* out = voice_out_.data();
    const bool paused = voice.paused != 0u;
    const bool starting = voice.starting != 0u;
    if (starting) {
        state.phase = 0.0;
        state.history = {};
        state.last = 0.0f;
        state.cycle = 0.0;
        state.loop_pending = false;
        state.fir_history.clear();
        state.iir.x1 = state.iir.x2 = state.iir.y1 = state.iir.y2 = 0.0f;
    }
    if (paused) {
        std::fill_n(out, samples_, state.last);
    } else {
        render_source(state, voice, out);
    }
    apply_fir(state, voice, out);
    apply_iir(state, voice, out);
    if (category_gains_set_) {
        apply_category(state, voice, out);
    }
    if (voice.panner != 0u) {
        mix_panner(voice, out);
    } else {
        mix_sends(voice, out);
    }
    if (!paused) {
        voice.starting = 0u;
    }
}

void AudioMixer::apply_category(VoiceState& state, const NebulaVoice& voice, float* samples) {
    const std::uint8_t bus = voice.category < category_gains_.size() ? voice.category : std::uint8_t{NEBULA_SOUND_EFFECT};
    const float target = category_gains_[bus];
    if (!state.category_set || state.category_bus != bus) {
        // A voice's first sound, or a new owner on another bus, starts at its own gain: nothing of a muted bus leaks
        // through a ramp from the previous owner's.
        state.category_gain = target;
        state.category_ramp = 0u;
        state.category_bus = bus;
        state.category_set = true;
    } else if (state.category_target != target) {
        state.category_ramp = std::max<std::uint32_t>(rate_ / 100u, 1u);
    }
    state.category_target = target;
    std::uint32_t i = 0;
    for (; i < samples_ && state.category_ramp != 0u; ++i) {
        state.category_gain += (state.category_target - state.category_gain) / static_cast<float>(state.category_ramp);
        --state.category_ramp;
        samples[i] *= state.category_gain;
    }
    // The ramp's end: one gain for the rest, and a unit gain changes no sample.
    const float gain = state.category_gain;
    if (gain != 1.0f) {
        for (; i < samples_; ++i) {
            samples[i] *= gain;
        }
    }
}

void AudioMixer::render_source(VoiceState& state, NebulaVoice& voice, float* out) {
    const std::uint32_t n = samples_;
    if (voice.format == NEBULA_AUDIO_OSCILLATOR) {
        render_oscillator(state, voice, out);
        return;
    }
    if (voice.format != NEBULA_AUDIO_PCM16) {
        std::fill_n(out, n, 0.0f);
        return;
    }
    // The source samples this subframe covers, after the previous subframe's last taps.
    const double span = std::min(static_cast<double>(voice.rate) / kSubframesPerSecond, static_cast<double>(kMaxSourceSpan));
    const double end_position = state.phase + span;
    const auto needed = static_cast<std::uint32_t>(end_position);
    float* x = source_.data();
    std::copy(state.history.begin(), state.history.end(), x);
    ++stats_.pcm_renders;
    read_pcm(state, voice, x + kVoiceTaps, needed);
    // Output i reads at source position phase + i * span / n, the taps around it. The whole source band passes; a step of
    // more than one source sample an output takes a lower cutoff, so a raised pitch does not fold back.
    const double start = state.phase;
    const double step = span / n;
    const InterpolationKernel& kernel = voice_kernels_[step <= 1.04 ? 0u : step <= 1.39 ? 1u : step <= 2.08 ? 2u : 3u];
    for (std::uint32_t i = 0; i < n; ++i) {
        const double at = start + i * step;
        const auto whole = static_cast<std::uint32_t>(at);
        const float* w = kernel.weights(at - whole);
        const float* s = x + whole;
#if defined(__clang__) || defined(__GNUC__)
        // The four running sums as one vector, lane j the scalar form's sum[j]: the same multiply-adds in the same order,
        // which the compiler fuses or not exactly as it does the scalar ones (it fused two lanes and left two scalar).
        using Lanes = float __attribute__((vector_size(16)));
        Lanes sum{};
        for (std::uint32_t k = 0; k < kVoiceTaps; k += 4u) {
            Lanes a;
            Lanes b;
            std::memcpy(&a, s + k, sizeof(a));
            std::memcpy(&b, w + k, sizeof(b));
            sum += a * b;
        }
#else
        float sum[4] = {};
        for (std::uint32_t k = 0; k < kVoiceTaps; k += 4u) {
            sum[0] += s[k] * w[k];
            sum[1] += s[k + 1u] * w[k + 1u];
            sum[2] += s[k + 2u] * w[k + 2u];
            sum[3] += s[k + 3u] * w[k + 3u];
        }
#endif
        out[i] = sum[0] + sum[1] + sum[2] + sum[3];
    }
    state.last = out[n - 1u];
    state.phase = end_position - needed;
    std::copy_n(x + needed, kVoiceTaps, state.history.begin());
}

// PCM: `restart` the sample it starts (or loops) at, `address` the next byte, `left` the samples before the end.
void AudioMixer::read_pcm(VoiceState& state, NebulaVoice& voice, float* out, std::uint32_t count) {
    constexpr std::uint32_t bytes = sizeof(std::int16_t);
    const auto restart = [&] {
        voice.left = voice.end - voice.restart;
        voice.address = voice.restart * bytes;
        state.loop_pending = false;
    };
    const auto fetch = [&](std::uint32_t samples) {
        if (samples == 0u) {
            return;
        }
        const std::byte* data = wave_bytes(voice, voice.address, samples * bytes);
        if (data == nullptr) {
            ++stats_.unresolved_reads;
            galaxy::loss::note(galaxy::loss::Kind::SoundRead);
            std::fill_n(out, samples, 0.0f);
        } else {
            for (std::uint32_t k = 0; k < samples; ++k) {
                std::int16_t value;
                std::memcpy(&value, data + k * 2u, sizeof(value));
                out[k] = value / 32768.0f;
            }
        }
    };
    if (voice.finished != 0u) {
        std::fill_n(out, count, 0.0f);
        return;
    }
    if (voice.starting != 0u) {
        restart();
    }
    bool pending = state.loop_pending;
    for (std::uint32_t loops = 0;; ++loops) {
        if (pending) {
            state.loop_pending = false;
            if (voice.loop == 0u || loops >= 8u) {
                std::fill_n(out, count, 0.0f);
                voice.finished = 1u;
                return;
            }
            voice.restart = voice.loop_start;
            restart();
        }
        const std::uint32_t left = voice.left;
        if (left < count) {
            fetch(left);
            out += left;
            count -= left;
            pending = true;
            continue;
        }
        fetch(count);
        voice.left = left - count;
        if (left == count) {
            state.loop_pending = true;
        }
        voice.address += count * bytes;
        return;
    }
}

void AudioMixer::render_oscillator(VoiceState& state, const NebulaVoice& voice, float* out) {
    ++stats_.oscillator_renders;
    const std::uint32_t n = samples_;
    const double per_authored_sample = static_cast<double>(voice.rate) / kAuthoredRate;
    const double step = per_authored_sample / authored_ratio_;
    double cycle = state.cycle;
    const auto advance = [&](double by) {
        cycle += by;
        cycle -= std::floor(cycle);
    };
    switch (voice.oscillator) {
    case NEBULA_AUDIO_SQUARE:
        for (std::uint32_t i = 0; i < n; ++i) {
            out[i] = cycle < 0.5 ? 0.5f : -0.5f;
            advance(step);
        }
        break;
    case NEBULA_AUDIO_SAWTOOTH:
        for (std::uint32_t i = 0; i < n; ++i) {
            out[i] = static_cast<float>(cycle < 0.5 ? 2.0 * cycle : 2.0 * cycle - 2.0);
            advance(step);
        }
        break;
    case NEBULA_AUDIO_SQUARE_FM: {
        // The rate follows the surround-right bus (its 16 bits added to the pitch, wrapping), which it consumes.
        float* modulator = buses_[kSurroundRight].data();
        for (std::uint32_t i = 0; i < n; ++i) {
            out[i] = cycle < 0.5 ? 0.5f : -0.5f;
            const double m = std::clamp(modulator[i], -1.0f, 1.0f) * (32767.0 / 131072.0);
            modulator[i] = 0.0f;
            advance(std::max(per_authored_sample + m, 0.0) / authored_ratio_);
        }
        break;
    }
    case NEBULA_AUDIO_PULSE25:
        for (std::uint32_t i = 0; i < n; ++i) {
            out[i] = cycle < 0.25 ? 0.5f : -0.5f;
            advance(step);
        }
        break;
    case NEBULA_AUDIO_TRIANGLE:
    case NEBULA_AUDIO_SINE:
    case NEBULA_AUDIO_SINE_PM:
        for (std::uint32_t i = 0; i < n; ++i) {
            out[i] = table_wave(voice.oscillator == NEBULA_AUDIO_TRIANGLE, static_cast<std::uint32_t>(cycle * 64.0) & 63u);
            advance(step);
        }
        break;
    case NEBULA_AUDIO_CONSTANT:
        std::fill_n(out, n, voice.rate);
        break;
    case NEBULA_AUDIO_NOISE_A:
    case NEBULA_AUDIO_NOISE_B:
        // Noise held for each 64th of the cycle.
        ++stats_.noise_renders;
        for (std::uint32_t i = 0; i < n; ++i) {
            const auto step_index = static_cast<std::uint32_t>(cycle * 64.0) & 63u;
            if (step_index != state.noise_step) {
                state.noise_step = step_index;
                state.noise_seed ^= state.noise_seed << 13u;
                state.noise_seed ^= state.noise_seed >> 17u;
                state.noise_seed ^= state.noise_seed << 5u;
                state.noise = static_cast<float>(static_cast<std::int32_t>(state.noise_seed) / 2147483648.0);
            }
            out[i] = state.noise;
            advance(step);
        }
        break;
    default:
        std::fill_n(out, n, 0.0f);
        break;
    }
    state.cycle = cycle;
}

// FIR: taps (filter bits 0-4) over the previous inputs at 32 kHz, fir[k] at taps - k samples back; at the output
// rate each tap sits at the same time back.
void AudioMixer::apply_fir(VoiceState& state, const NebulaVoice& voice, float* samples) {
    const std::uint32_t taps = voice.fir_taps;
    if (taps == 0u) {
        return;
    }
    ++stats_.fir_renders;
    std::array<float, 8> key{};
    std::copy_n(voice.fir, 8u, key.begin());
    if (state.fir_key_mode != taps || state.fir_key != key || state.fir.empty()) {
        std::array<float, 8> coefficients{};
        std::array<double, 8> delays{};
        const std::uint32_t used = std::min<std::uint32_t>(taps, 8u);
        for (std::uint32_t k = 0; k < used; ++k) {
            coefficients[k] = voice.fir[k];
            delays[k] = static_cast<double>(taps - k);
        }
        state.fir = delays_to_weights(coefficients.data(), delays.data(), used, authored_ratio_);
        state.fir_key_mode = static_cast<std::uint16_t>(taps);
        state.fir_key = key;
    }
    run_fir(state.fir, state.fir_history, samples, samples_);
}

// IIR: y[n] = c0 x[n] + c1 x[n-1] + c2 y[n-1] + c3 y[n-2] at 32 kHz, carried to the output rate through the analog
// filter both rates' bilinear transforms share (exact at DC, the same shape with the top octave compressed); outputs
// saturate as the game's filter did. {1, 0, 0, 0} passes the signal through.
void AudioMixer::apply_iir(VoiceState& state, const NebulaVoice& voice, float* samples) {
    if (voice.iir_on == 0u) {
        return;
    }
    std::array<float, 4> key{voice.iir[0], voice.iir[1], voice.iir[2], voice.iir[3]};
    if (key[0] == kUnit && key[1] == 0.0f && key[2] == 0.0f && key[3] == 0.0f) {
        return;
    }
    ++stats_.iir_renders;
    if (!state.iir_valid || state.iir_key != key) {
        const double b0 = key[0];
        const double b1 = key[1];
        const double a1 = key[2];
        const double a2 = key[3];
        // z^-1 at 32 kHz = (A + B q) / (B + A q), q = z^-1 at the output rate.
        const double r = static_cast<double>(rate_) / kAuthoredRate;
        const double A = 1.0 - r;
        const double B = 1.0 + r;
        const double lead = b0 * B + b1 * A;
        const double trail = b0 * A + b1 * B;
        const double n0 = lead * B;
        const double n1 = lead * A + trail * B;
        const double n2 = trail * A;
        const double d0 = B * B - a1 * A * B - a2 * A * A;
        const double d1 = 2.0 * A * B - a1 * (A * A + B * B) - 2.0 * a2 * A * B;
        const double d2 = A * A - a1 * A * B - a2 * B * B;
        Biquad& f = state.iir;
        f.b0 = static_cast<float>(n0 / d0);
        f.b1 = static_cast<float>(n1 / d0);
        f.b2 = static_cast<float>(n2 / d0);
        f.a1 = static_cast<float>(-d1 / d0);
        f.a2 = static_cast<float>(-d2 / d0);
        state.iir_key = key;
        state.iir_valid = true;
    }
    Biquad& f = state.iir;
    for (std::uint32_t i = 0; i < samples_; ++i) {
        const float x0 = samples[i];
        const float y0 = std::clamp(f.b0 * x0 + f.b1 * f.x1 + f.b2 * f.x2 + f.a1 * f.y1 + f.a2 * f.y2, -1.0f,
                                    32767.0f / 32768.0f);
        f.x2 = f.x1;
        f.x1 = x0;
        f.y2 = f.y1;
        f.y1 = y0;
        samples[i] = y0;
    }
}

void AudioMixer::ramp_mix(float* destination, const float* source, float start, float delta) const {
    if (destination == nullptr || (start == 0.0f && delta == 0.0f)) {
        return;
    }
    for (std::uint32_t i = 0; i < samples_; ++i) {
        const float volume = start + delta * ramp_[i];
        destination[i] += source[i] * volume;
    }
}

void AudioMixer::make_ramp() {
    if (ramp_samples_ == samples_) {
        return;
    }
    ramp_samples_ = samples_;
    // The volume reaches its target 65 samples of 32 kHz into the subframe.
    const double authored_per_output = static_cast<double>(kAuthoredTickSamples) / samples_;
    for (std::uint32_t i = 0; i < samples_; ++i) {
        const double t = std::clamp((i * authored_per_output - 1.0) / 64.0, 0.0, 1.0);
        ramp_[i] = static_cast<float>(t);
    }
}

void AudioMixer::mix_sends(NebulaVoice& voice, const float* samples) {
    if (voice.forced_stop != 0u) {
        // Every target becomes half its current volume; the voice finishes once all are zero.
        bool silent = true;
        for (NebulaAudioSend& send : voice.sends) {
            send.target = halved(send.current);
            silent = silent && send.current == 0.0f;
        }
        if (silent) {
            voice.finished = 1u;
        }
    }
    for (NebulaAudioSend& send : voice.sends) {
        float* destination = bus(send.bus);
        if (destination == nullptr) {
            continue;
        }
        ramp_mix(destination, samples, send.current, send.target - send.current);
        send.current = send.target;
    }
}

// The surround panner: pan and front/rear positions pick quarter-sine gains; the volume ramps to its target; the
// effect send feeds effect lines 2 and 3 from the front and the panner's rear sends from the back.
void AudioMixer::mix_panner(NebulaVoice& voice, const float* samples) {
    ++stats_.panner_renders;
    if (voice.forced_stop != 0u) {
        voice.panner_target = halved(voice.panner_volume);
        if (voice.panner_volume == 0.0f) {
            voice.finished = 1u;
        }
    }
    const float left = pan_gain(1.0f - voice.pan);
    const float right = pan_gain(voice.pan);
    const float front = pan_gain(1.0f - voice.rear);
    const float back = pan_gain(voice.rear);
    const std::array<float, 4> gains{left * front, right * front, left * back, right * back};
    const std::array<std::uint32_t, 4> dry{kLeft, kRight, kSurroundLeft, kSurroundRight};
    const std::array<std::uint32_t, 4> wet{kEffect0 + 2u, kEffect0 + 3u, kPannerSendLeft, kPannerSendRight};
    const float current = voice.panner_volume;
    const float delta = voice.panner_target - current;
    const float send = voice.panner_send;
    for (std::size_t i = 0; i < 4u; ++i) {
        ramp_mix(bus(dry[i]), samples, gains[i] * current, gains[i] * delta);
        ramp_mix(bus(wet[i]), samples, gains[i] * current * send, gains[i] * delta * send);
    }
    voice.panner_volume = voice.panner_target;
}

// An effect line: the delayed signal (FIR'd before or after the sends) back into the delay with the line's input.
void AudioMixer::effects_begin() {
    const std::size_t n = samples_;
    // A line's row is written for [0, n) before its sends read it: no fill.
    std::array<std::array<float, kMaxSubframe>, 4> sends;
    for (std::size_t line = 0; line < effect_states_.size(); ++line) {
        EffectState& e = effect_states_[line];
        std::fill_n(e.input.begin(), n, 0.0f);
        if (effects_ == nullptr) {
            continue;
        }
        const NebulaEffectLine& config = effects_[line];
        if (config.generation != e.generation || config.subframes != e.subframes) {
            e.generation = config.generation;
            e.subframes = config.subframes;
            e.delay.assign(std::max<std::size_t>(
                               static_cast<std::size_t>(std::lround(config.subframes * static_cast<double>(rate_) /
                                                                    kSubframesPerSecond)),
                               1u),
                           0.0f);
            e.position = 0u;
            e.history_a.clear();
            e.history_b.clear();
        }
        if (config.filter == 0u || config.subframes == 0u) {
            continue;
        }
        ++stats_.effect_subframes;
        std::array<float, 8> key{};
        std::copy_n(config.fir, 8u, key.begin());
        if (e.fir.empty() || e.fir_key != key) {
            std::array<float, 8> coefficients{};
            std::array<double, 8> delays{};
            for (std::size_t k = 0; k < 8u; ++k) {
                coefficients[k] = config.fir[k];
                delays[k] = static_cast<double>(8u - k);
            }
            e.fir = delays_to_weights(coefficients.data(), delays.data(), 8u, authored_ratio_);
            e.fir_key = key;
        }
        float* delayed = sends[line].data();
        const std::size_t size = e.delay.size();
        for (std::size_t i = 0, at = e.position; i < n; ++i) {
            delayed[i] = e.delay[at];
            at = at + 1u == size ? 0u : at + 1u;
        }
        if ((config.filter & 1u) != 0u) {
            run_fir(e.fir, e.history_a, delayed, n);
        }
        std::copy_n(delayed, n, e.input.begin());
        if ((config.filter & 2u) != 0u) {
            run_fir(e.fir, e.history_b, e.input.data(), n);
        }
    }
    // The sends, after every line's feedback is in place (a line may send into another's input).
    for (std::size_t line = 0; line < effect_states_.size() && effects_ != nullptr; ++line) {
        const NebulaEffectLine& config = effects_[line];
        if (config.filter == 0u || config.subframes == 0u) {
            continue;
        }
        for (const auto& [target, volume] : {std::pair{config.bus_a, config.volume_a},
                                             std::pair{config.bus_b, config.volume_b}}) {
            float* destination = bus(target);
            if (destination == nullptr || volume == 0.0f) {
                continue;
            }
            for (std::size_t i = 0; i < n; ++i) {
                destination[i] += sends[line][i] * volume;
            }
        }
    }
}

void AudioMixer::effects_end() {
    const std::size_t n = samples_;
    // The panner's rear effect sends: the left one subframe late, the right half a subframe late, matrixed into
    // effect lines 2 and 3.
    // Written for [0, n) by the delays before any read: no fill.
    std::array<float, kMaxSubframe> late_left;
    std::array<float, kMaxSubframe> late_right;
    panner_send_left_.process(buses_[kPannerSendLeft].data(), late_left.data(), n);
    panner_send_right_.process(buses_[kPannerSendRight].data(), late_right.data(), n);
    float* line2 = effect_states_[2].input.data();
    float* line3 = effect_states_[3].input.data();
    for (std::size_t i = 0; i < n; ++i) {
        line2[i] += kUnit * late_left[i] - kMatrix * late_right[i];
        line3[i] += -kMatrix * late_left[i] + kUnit * late_right[i];
    }
    for (std::size_t line = 0; line < effect_states_.size() && effects_ != nullptr; ++line) {
        EffectState& e = effect_states_[line];
        const NebulaEffectLine& config = effects_[line];
        if (config.filter == 0u || config.subframes == 0u || e.delay.empty()) {
            continue;
        }
        const std::size_t size = e.delay.size();
        for (std::size_t i = 0, at = e.position; i < n; ++i) {
            e.delay[at] = e.input[i];
            at = at + 1u == size ? 0u : at + 1u;
        }
        e.position = (e.position + n) % e.delay.size();
    }
}

// `size` bytes `offset` past the voice's wave, when they lie in the sound memory.
const std::byte* AudioMixer::wave_bytes(const NebulaVoice& voice, std::uint32_t offset, std::uint32_t size) const {
    const auto* wave = reinterpret_cast<const std::byte*>(voice.wave);
    if (voice.wave_end == nullptr) {
        return nullptr;
    }
    const auto available = static_cast<std::size_t>(voice.wave_end - voice.wave);
    return offset <= available && size <= available - offset ? wave + offset : nullptr;
}

}  // namespace galaxy::audio
