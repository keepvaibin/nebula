#pragma once

#include "galaxy/native_audio_adpcm.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>

namespace galaxy::host {

// Adapted from Dusklight src/dusk/audio/DuskDsp.cpp::{ResetChannel,
// ReadChannelSamplesChunk,FillDecodeBuf,RenderChannel,DspRender,
// CalcStereoChannelVolumes,ApplyPanning,ApplyVolume} at CC0-1.0 revision
// ad979d3dae092d0f5cbdaf49eabca7b4f1db4838. The guest-memory adapter
// and mailbox integration remain separate from this normalized render kernel.
// https://github.com/TwilitRealm/dusklight/blob/ad979d3dae092d0f5cbdaf49eabca7b4f1db4838/src/dusk/audio/DuskDsp.cpp
// Source-zero oscillator path additionally adapted from the current CC0 native
// file at 40457c6adb381928e4b5fef6ed459ed291edd5e2:
// https://github.com/TwilitRealm/dusklight/blob/40457c6adb381928e4b5fef6ed459ed291edd5e2/src/dusk/audio/DuskDsp.cpp

inline constexpr std::size_t kNativeDuskDspSubframeSamples = 0x50;

// RMGE01 dspproc::DsyncFrame2ch packs 0x82, subframe count, and the global
// DSP mixer level into one command word. The lower half is a gain, not a
// fixed format tag. Native DSP clamps that level to 0x8000.
[[nodiscard]] constexpr bool native_audio_dusk_sync_frame_shape_supported(
    std::uint32_t command,
    std::uint32_t subframe_index,
    std::uint32_t aux_a_buffer,
    std::uint32_t aux_b_buffer) noexcept {
    return (command >> 24u) == 0x82u &&
        ((command >> 16u) & 0xffu) == 7u &&
        subframe_index < 7u &&
        (command & 0xffffu) <= 0x8000u &&
        aux_a_buffer == 0u && aux_b_buffer == 0u;
}

struct NativeDuskDspChannel {
    bool active{};
    bool finished{};
    bool reset{};
    bool paused{};
    bool forced_stop{};
    std::uint16_t pitch{};
    std::uint16_t samples_per_block{};
    std::uint16_t bytes_per_block{};
    std::uint16_t filter_mode{};
    std::array<std::int16_t, 8> iir_coefficients{};
    bool loop{};
    std::uint32_t sample_position{};
    std::uint32_t samples_left{};
    std::uint32_t loop_start_sample{};
    std::uint32_t end_sample{};
    std::uint32_t aram_stream_position{};
    std::int16_t loop_history_older{};
    std::int16_t loop_history_newer{};
    std::span<const std::uint8_t> wave;
};

struct NativeDuskDspChannelAux {
    std::int16_t history_older{};
    std::int16_t history_newer{};
    std::array<std::int16_t, 2048> decode_buffer{};
    std::size_t decoded_samples{};
    float resample_position{};
    std::int16_t resample_previous{};
    std::uint32_t reset_count{};
    std::uint16_t oscillator_phase{};
    std::array<float, 2> previous_volume{{
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::quiet_NaN()}};
};

struct NativeDuskDspBus {
    std::uint16_t connect{};
    std::uint16_t target_volume{};
    std::uint16_t current_volume{};
};

struct NativeDuskDspStereoMix {
    bool auto_mixer{};
    std::uint16_t pan_dolby{};
    std::uint16_t auto_init_volume{};
    std::uint16_t auto_volume{};
    std::uint16_t effect_mix{};
    std::array<NativeDuskDspBus, 6> buses{};
};

enum class NativeDuskDspRenderResult {
    Rendered,
    Inactive,
    UnsupportedFormat,
    MissingSource,
    InvalidChannel,
    UnsupportedRouting,
};

// The source-zero branch in Dusklight's DspRender uses a 16-bit phase rather
// than reading AFC/PCM bytes. Keep that voice state per channel, just as the
// wave decoder keeps its history per channel.
inline void native_audio_dusk_reset_channel(
    NativeDuskDspChannel& channel,
    NativeDuskDspChannelAux& aux) noexcept {
    ++aux.reset_count;
    channel.samples_left = channel.end_sample - channel.sample_position;
    aux.history_older = 0;
    aux.history_newer = 0;
    aux.decoded_samples = 0;
    aux.resample_position = 0.0f;
    aux.resample_previous = 0;
    aux.oscillator_phase = 0;
    aux.previous_volume = {{
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::quiet_NaN()}};
    channel.reset = false;
}

// Adapted from Dusklight DuskDsp.cpp::RenderOscChannel. The evolving
// harmonic oscillator (type 11) depends on a separate mutable 64-entry table;
// it remains unsupported until that table's frame update is integrated.
[[nodiscard]] inline NativeDuskDspRenderResult
native_audio_dusk_render_oscillator_32k(
    NativeDuskDspChannel& channel,
    NativeDuskDspChannelAux& aux,
    std::span<float, kNativeDuskDspSubframeSamples> output) noexcept {
    std::fill(output.begin(), output.end(), 0.0f);
    if (!channel.active || channel.paused) {
        return NativeDuskDspRenderResult::Inactive;
    }
    if (channel.forced_stop) {
        channel.finished = true;
        return NativeDuskDspRenderResult::Inactive;
    }
    switch (channel.bytes_per_block) {
    case 0u:  // square, 50% duty
    case 1u:  // saw / evolving ramp
    case 3u:  // square, 25% duty
    case 4u:  // triangle
    case 7u:  // sine
    case 10u: // sine with variable pitch
    case 12u: // evolving ramp
        break;
    default:
        return NativeDuskDspRenderResult::UnsupportedFormat;
    }
    if (channel.reset) {
        native_audio_dusk_reset_channel(channel, aux);
    }
    constexpr float kPhaseToRadians =
        6.2831853071795864769f / 65536.0f;
    const std::uint16_t phase_step =
        static_cast<std::uint16_t>(channel.pitch >> 1u);
    for (float& sample : output) {
        const std::uint16_t phase = aux.oscillator_phase;
        const float signed_phase = static_cast<float>(
            std::bit_cast<std::int16_t>(phase)) / 32768.0f;
        switch (channel.bytes_per_block) {
        case 0u:
            sample = phase < 0x8000u ? 0.5f : -0.5f;
            break;
        case 1u:
        case 12u:
            sample = signed_phase;
            break;
        case 3u:
            sample = phase < 0x4000u ? 0.5f : -0.5f;
            break;
        case 4u:
            sample = 0.5f - std::fabs(signed_phase);
            break;
        case 7u:
        case 10u:
            sample = std::sin(static_cast<float>(phase) *
                              kPhaseToRadians) * 0.5f;
            break;
        default:
            break;
        }
        aux.oscillator_phase = static_cast<std::uint16_t>(
            phase + phase_step);
    }
    return NativeDuskDspRenderResult::Rendered;
}

// Adapted from DuskDsp.cpp::{CalcStereoChannelVolumes,ApplyPanning,
// ApplyVolume}. The Wii guest's JASDriver::MAX_MIXERLEVEL is 0x2ee0.
[[nodiscard]] inline NativeDuskDspRenderResult native_audio_dusk_mix_stereo(
    const NativeDuskDspStereoMix& mix,
    NativeDuskDspChannelAux& aux,
    std::span<const float, kNativeDuskDspSubframeSamples> mono,
    std::span<float, kNativeDuskDspSubframeSamples> left,
    std::span<float, kNativeDuskDspSubframeSamples> right) noexcept {
    if (mix.effect_mix != 0u) {
        return NativeDuskDspRenderResult::UnsupportedRouting;
    }
    constexpr float kChannelLevel = 0x2ee0;
    std::array<float, 2> initial{};
    std::array<float, 2> target{};
    if (mix.auto_mixer) {
        const float pan = static_cast<float>(mix.pan_dolby >> 8u) / 127.0f;
        if (pan > 1.0f) {
            return NativeDuskDspRenderResult::UnsupportedRouting;
        }
        const float start = static_cast<float>(mix.auto_init_volume) /
            kChannelLevel;
        const float end = static_cast<float>(mix.auto_volume) /
            kChannelLevel;
        initial = {{(1.0f - pan) * start, pan * start}};
        target = {{(1.0f - pan) * end, pan * end}};
    } else {
        for (const auto& bus : mix.buses) {
            if (bus.connect == 0u) {
                // Dusklight's ApplyPanning ignores an unconnected output
                // record even if the DSP left old gain values in that slot.
                continue;
            }
            // RMGE01 keeps its auxiliary L/R routes connected even while
            // both gains are zero. The native DSP then contributes no
            // samples to those buses. Dusklight's ApplyPanning selects only
            // the stereo outputs; adapt that no-signal case without silently
            // dropping an audible auxiliary route or an unknown DSP address.
            if ((bus.connect == 0x0dc0u || bus.connect == 0x0e20u) &&
                bus.current_volume == 0u && bus.target_volume == 0u) {
                continue;
            }
            std::size_t output = 0u;
            if (bus.connect == 0x0d60u) {
                output = 1u;
            } else if (bus.connect != 0x0d00u) {
                return NativeDuskDspRenderResult::UnsupportedRouting;
            }
            initial[output] = static_cast<float>(bus.current_volume) /
                kChannelLevel;
            target[output] = static_cast<float>(bus.target_volume) /
                kChannelLevel;
        }
    }
    std::array<std::span<float, kNativeDuskDspSubframeSamples>, 2> outputs{
        left, right};
    for (std::size_t ch = 0u; ch < outputs.size(); ++ch) {
        const float start = std::isnan(aux.previous_volume[ch])
            ? initial[ch] : aux.previous_volume[ch];
        const float end = target[ch];
        const float step = (end - start) /
            static_cast<float>(kNativeDuskDspSubframeSamples);
        for (std::size_t i = 0u; i < mono.size(); ++i) {
            outputs[ch][i] += mono[i] * (start + static_cast<float>(i) * step);
        }
        aux.previous_volume[ch] = end;
    }
    return NativeDuskDspRenderResult::Rendered;
}

[[nodiscard]] inline NativeDuskDspRenderResult
native_audio_dusk_render_channel_32k(
    NativeDuskDspChannel& channel,
    NativeDuskDspChannelAux& aux,
    std::span<float, kNativeDuskDspSubframeSamples> output) noexcept {
    std::fill(output.begin(), output.end(), 0.0f);
    if (!channel.active || channel.paused) {
        return NativeDuskDspRenderResult::Inactive;
    }
    if (channel.forced_stop) {
        channel.finished = true;
        return NativeDuskDspRenderResult::Inactive;
    }
    const bool adpcm = channel.samples_per_block == 16u &&
        channel.bytes_per_block == 9u;
    const bool pcm16 = channel.samples_per_block == 1u &&
        channel.bytes_per_block == 16u;
    // RMGE01 native ucode 0x0366..0x0384 bypasses its IIR kernel when
    // coefficients [0..3] are {0x7fff,0,0,0}. Keep a narrower full-table
    // identity precondition here until nontrivial filtering is implemented.
    // Applying a Q15 gain of 0x7fff would incorrectly attenuate this bypass.
    constexpr std::array<std::int16_t, 8> kIdentityIir{
        0x7fff, 0, 0, 0, 0, 0, 0, 0};
    const bool native_filter_bypassed = channel.filter_mode == 0u ||
        (channel.filter_mode == 0x20u &&
         channel.iir_coefficients == kIdentityIir);
    if ((!adpcm && !pcm16) || !native_filter_bypassed) {
        return NativeDuskDspRenderResult::UnsupportedFormat;
    }
    if (channel.end_sample < channel.sample_position ||
        (channel.loop && channel.end_sample <= channel.loop_start_sample)) {
        return NativeDuskDspRenderResult::InvalidChannel;
    }

    if (channel.reset) {
        native_audio_dusk_reset_channel(channel, aux);
    }

    // Dusklight's 32-kHz form: one pitch unit is 1/4096 source sample per
    // output sample, with two lookahead samples for interpolation.
    const float step = static_cast<float>(channel.pitch) / 4096.0f;
    if (!std::isfinite(step) || step > 16.0f ||
        !std::isfinite(aux.resample_position) ||
        aux.resample_position < 0.0f || aux.resample_position >= 1.0f) {
        return NativeDuskDspRenderResult::InvalidChannel;
    }
    const std::size_t needed = static_cast<std::size_t>(
        aux.resample_position + kNativeDuskDspSubframeSamples * step) + 2u;
    if (needed > aux.decode_buffer.size()) {
        return NativeDuskDspRenderResult::InvalidChannel;
    }

    while (aux.decoded_samples < needed) {
        if (channel.samples_left == 0u) {
            if (!channel.loop) {
                break;
            }
            channel.samples_left = channel.end_sample - channel.loop_start_sample;
            channel.sample_position = channel.loop_start_sample;
            aux.history_older = channel.loop_history_older;
            aux.history_newer = channel.loop_history_newer;
        }

        const std::size_t block_samples = channel.samples_per_block;
        const std::size_t skip = channel.sample_position % block_samples;
        const std::uint32_t aligned_position =
            channel.sample_position - static_cast<std::uint32_t>(skip);
        const std::size_t desired = std::min(
            aux.decode_buffer.size() - aux.decoded_samples,
            needed - aux.decoded_samples + skip);
        const std::size_t rounded =
            ((desired + block_samples - 1u) / block_samples) * block_samples;
        const std::size_t available = std::min({
            static_cast<std::size_t>(channel.samples_left) + skip,
            rounded,
            aux.decode_buffer.size()});
        const std::size_t block_count =
            (available + block_samples - 1u) / block_samples;
        const std::size_t byte_offset =
            (static_cast<std::size_t>(aligned_position) / block_samples) *
            (adpcm ? 9u : 2u);
        const std::size_t byte_count = block_count * (adpcm ? 9u : 2u);
        if (available == 0u || byte_offset > channel.wave.size() ||
            byte_count > channel.wave.size() - byte_offset ||
            skip >= available) {
            return NativeDuskDspRenderResult::MissingSource;
        }

        std::array<std::int16_t, 2048> decoded{};
        if (adpcm) {
            if (!native_audio_decode_dusk_adpcm4(
                    channel.wave.subspan(byte_offset, byte_count),
                    std::span<std::int16_t>(decoded).first(available),
                    aux.history_older, aux.history_newer)) {
                return NativeDuskDspRenderResult::MissingSource;
            }
        } else {
            for (std::size_t i = 0; i < available; ++i) {
                const std::size_t offset = byte_offset + i * 2u;
                decoded[i] = static_cast<std::int16_t>(
                    (static_cast<std::uint16_t>(channel.wave[offset]) << 8u) |
                    channel.wave[offset + 1u]);
            }
        }
        const std::size_t produced = std::min(
            available - skip,
            aux.decode_buffer.size() - aux.decoded_samples);
        std::copy_n(decoded.begin() + skip, produced,
                    aux.decode_buffer.begin() + aux.decoded_samples);
        aux.decoded_samples += produced;
        channel.sample_position = aligned_position +
            static_cast<std::uint32_t>(available);
        channel.samples_left -= static_cast<std::uint32_t>(available - skip);
        if (produced == 0u) {
            break;
        }
    }

    channel.aram_stream_position = static_cast<std::uint32_t>(
        (static_cast<std::size_t>(channel.sample_position) /
         channel.samples_per_block) * (adpcm ? 9u : 2u));
    if (aux.decoded_samples < needed) {
        channel.finished = true;
    }

    float position = aux.resample_position;
    std::int16_t previous = aux.resample_previous;
    std::int16_t next = aux.decoded_samples != 0u
        ? aux.decode_buffer[0] : previous;
    std::size_t source_index = 0;
    for (std::size_t i = 0; i < output.size(); ++i) {
        output[i] = (previous + position * (next - previous)) / 32768.0f;
        position += step;
        while (position >= 1.0f) {
            position -= 1.0f;
            previous = next;
            ++source_index;
            next = source_index < aux.decoded_samples
                ? aux.decode_buffer[source_index] : previous;
        }
    }
    aux.resample_position = position;
    aux.resample_previous = previous;
    const std::size_t remaining = aux.decoded_samples > source_index
        ? aux.decoded_samples - source_index : 0u;
    if (remaining != 0u) {
        std::memmove(aux.decode_buffer.data(),
                     aux.decode_buffer.data() + source_index,
                     remaining * sizeof(std::int16_t));
    }
    aux.decoded_samples = remaining;
    return NativeDuskDspRenderResult::Rendered;
}

}  // namespace galaxy::host
