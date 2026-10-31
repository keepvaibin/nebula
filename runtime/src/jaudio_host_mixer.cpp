#include "galaxy/jaudio_host_mixer.h"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace galaxy::host {
namespace {

constexpr std::uint32_t kRecordBytes = 0x180u;
constexpr std::uint32_t kSamples = 80u;
constexpr std::uint32_t kOutOffset = 64u;
constexpr std::uint32_t kMaxDelay = 16u;

// Mix buses (DSP data memory word addresses).
constexpr std::uint32_t kBusLeft = 0x0D00u;
constexpr std::uint32_t kBusRight = 0x0D60u;
constexpr std::uint32_t kBusSurroundLeft = 0x0F40u;
constexpr std::uint32_t kBusSurroundRight = 0x0CA0u;
constexpr std::uint32_t kBusRightInverted = 0x0FA0u;
constexpr std::uint32_t kBusCenter = 0x09A0u;
constexpr std::uint32_t kBusDiscard = 0x0B00u;
constexpr std::uint32_t kSendSurroundLeft = 0x0BE0u;
constexpr std::uint32_t kSendSurroundRight = 0x0C40u;
constexpr std::array<std::uint32_t, 4> kFxBus{0x0DC0u, 0x0E20u, 0x0E80u, 0x0EE0u};
// Auto mixer destinations: front L/R, surround L/R, then the four effect
// sends (ucode table at IRAM 0x0E6D).
constexpr std::array<std::uint32_t, 8> kAutoMixerBus{
    0x0D00u, 0x0D60u, 0x0F40u, 0x0CA0u, 0x0E80u, 0x0EE0u, 0x0BE0u, 0x0C40u};

[[nodiscard]] std::uint16_t be16(const std::byte* p, std::uint32_t offset) noexcept {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint32_t>(p[offset]) << 8u) |
        static_cast<std::uint32_t>(p[offset + 1u]));
}

[[nodiscard]] std::int16_t sbe16(const std::byte* p, std::uint32_t offset) noexcept {
    return static_cast<std::int16_t>(be16(p, offset));
}

// The game's channel records are in host byte order; wave data keeps the disc's big-endian samples.
template <typename T>
[[nodiscard]] T record_load(const std::byte* p, std::uint32_t offset) noexcept {
    T value;
    std::memcpy(&value, p + offset, sizeof(T));
    return value;
}
auto r16 = record_load<std::uint16_t>;
auto rs16 = record_load<std::int16_t>;
auto r32 = record_load<std::uint32_t>;

void w16(std::byte* p, std::uint32_t offset, std::uint32_t value) noexcept {
    const auto v = static_cast<std::uint16_t>(value);
    std::memcpy(p + offset, &v, sizeof(v));
}

void w32(std::byte* p, std::uint32_t offset, std::uint32_t value) noexcept {
    std::memcpy(p + offset, &value, sizeof(value));
}

void put16(std::byte* p, std::uint32_t offset, std::uint32_t value) noexcept {
    p[offset] = static_cast<std::byte>((value >> 8u) & 0xFFu);
    p[offset + 1u] = static_cast<std::byte>(value & 0xFFu);
}

[[nodiscard]] std::int16_t sat16(std::int64_t value) noexcept {
    return static_cast<std::int16_t>(std::clamp<std::int64_t>(value, -32768, 32767));
}

// The DSP rounds a product to bit 16 half-to-even when it moves it into an
// accumulator; returns the rounded value's middle word.
[[nodiscard]] std::int64_t rounded_mid(std::int64_t product) noexcept {
    return ((product & 0x10000) != 0 ? product + 0x8000 : product + 0x7FFF) >> 16;
}

// Q15 multiply with the product doubled (DSP "M2" mode), truncated.
[[nodiscard]] std::int16_t q15(std::int32_t a, std::int32_t b) noexcept {
    return sat16((static_cast<std::int64_t>(a) * b * 2) >> 16);
}

// 40-bit accumulator helpers for the oscillator table generator, which
// depends on the accumulator's full width and on 16-bit-mode middle writes.
[[nodiscard]] std::int64_t wrap40(std::int64_t value) noexcept {
    std::uint64_t bits = static_cast<std::uint64_t>(value) & 0xFF'FFFF'FFFFull;
    if ((bits & 0x80'0000'0000ull) != 0u) {
        bits |= ~0xFF'FFFF'FFFFull;
    }
    return static_cast<std::int64_t>(bits);
}

[[nodiscard]] std::int16_t mid_of(std::int64_t accumulator) noexcept {
    return static_cast<std::int16_t>(
        static_cast<std::uint16_t>(static_cast<std::uint64_t>(accumulator) >> 16u));
}

[[nodiscard]] std::int64_t with_mid(std::int64_t accumulator, std::int16_t mid) noexcept {
    std::uint64_t bits = static_cast<std::uint64_t>(accumulator) & 0xFF'FFFF'FFFFull;
    bits = (bits & ~0xFFFF'0000ull) |
        (static_cast<std::uint64_t>(static_cast<std::uint16_t>(mid)) << 16u);
    return wrap40(static_cast<std::int64_t>(bits));
}

}  // namespace

JAudioHostMixer::JAudioHostMixer() = default;

void JAudioHostMixer::setup(std::uint32_t channel_count, std::byte* channel_records,
                            const std::int16_t* resample_filter, const std::int16_t* adpcm_filter,
                            const NebulaJAudioFxLine* fx_lines) {
    ++stats_.setups;
    channels_ = channel_records;
    channel_count_ = std::min<std::uint32_t>(channel_count, 64u);
    fx_lines_ = fx_lines;
    // The DSP copies DSPRES_FILTER (640 words) and DSPADPCM_FILTER (32 words)
    // into its data memory once, at setup.
    std::copy_n(resample_filter, 0x280u, dram_.data());
    std::copy_n(adpcm_filter, 0x20u, dram_.data() + 0x300u);
}

void JAudioHostMixer::set_aux(const std::byte* base, std::uint32_t size) noexcept {
    aux_ = base;
    aux_size_ = size;
}

void JAudioHostMixer::set_bus_gains(const galaxy::audio::NativeAudioBusGains& gains) noexcept {
    using galaxy::audio::native_audio_gain_to_q16;
    bus_gain_q16_ = {native_audio_gain_to_q16(gains.music), native_audio_gain_to_q16(gains.voice),
                     native_audio_gain_to_q16(gains.sfx), native_audio_gain_to_q16(gains.ambience),
                     native_audio_gain_to_q16(gains.cutscene)};
    bus_gains_set_ = true;
}

void JAudioHostMixer::classify_channel(std::uint32_t index, const std::byte* record, bool starting) {
    JAudioChannelCategory& category = categories_[index];
    // The game names the channel's sound (nebula_game_audio_channel_owner); without one, the sample format decides.
    std::uint32_t route = 0u;
    const std::uint32_t sound_id = owner_resolver_ != nullptr ? owner_resolver_(index, &route) : 0u;
    category.owner = {static_cast<galaxy::audio::JAudioOwnerRoute>(route <= 3u ? route : 0u), sound_id};
    category.decision = route != 0u ? galaxy::audio::native_audio_bus_from_sound_id(sound_id)
                                    : galaxy::audio::NativeAudioBusDecision{};
    if (category.decision.classification != galaxy::audio::NativeAudioBusClassification::OwnerSoundId) {
        category.decision = galaxy::audio::native_audio_bus_from_channel_format(r16(record, 0x064u),
                                                                                r16(record, 0x100u));
    }
    ++stats_.category_decisions;
    if (category.decision.classification == galaxy::audio::NativeAudioBusClassification::OwnerSoundId) {
        category_retries_[index] = 0u;
    } else if (starting) {
        ++stats_.category_owner_misses;
        category_retries_[index] = 8u;
    } else {
        --category_retries_[index];
    }
}

void JAudioHostMixer::sync_frame(std::uint32_t subframes, std::uint32_t mixer_level,
                                 std::int16_t* left, std::int16_t* right) {
    ++stats_.frames;
    mixer_level_ = mixer_level & 0xFFFFu;
    left_plane_ = left;
    right_plane_ = right;
    subframes_ = subframes & 0xFFu;
    next_subframe_ = 0u;
    frame_active_ = configured() && subframes_ != 0u;
    maps_ = {};
    // FX_BUF is read once per frame.
    for (std::size_t line = 0; line < fx_.size(); ++line) {
        FxLine fx{};
        if (fx_lines_ != nullptr) {
            const NebulaJAudioFxLine& raw = fx_lines_[line];
            fx.flags = raw.flags;
            fx.frames = raw.frames;
            fx.buffer = raw.buffer;
            fx.dest_a = raw.dest_a;
            fx.volume_a = raw.volume_a;
            fx.dest_b = raw.dest_b;
            fx.volume_b = raw.volume_b;
            std::copy_n(raw.coefficients, 8u, fx.coefficients.begin());
        }
        fx_[line] = fx;
    }
}

bool JAudioHostMixer::release_halt(std::uint32_t group, std::uint32_t map) {
    maps_[group & 3u] = static_cast<std::uint16_t>(map & 0xFFFFu);
    if (group != 3u) {
        return false;
    }
    if (!frame_active_ || next_subframe_ >= subframes_) {
        ++stats_.stray_releases;
        return false;
    }
    const auto begin = std::chrono::steady_clock::now();
    mix_subframe();
    stats_.mix_ns += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - begin).count());
    ++next_subframe_;
    if (next_subframe_ >= subframes_) {
        frame_active_ = false;
    }
    return true;
}

void JAudioHostMixer::mix_subframe() {
    ++stats_.subframes;
    std::int16_t* d = dram_.data();
    const auto clear = [d](std::uint32_t address, std::uint32_t count) {
        std::fill_n(d + address, count, std::int16_t{0});
    };

    // Subframe start (ucode 0x0116): fresh front buses, then the previous
    // subframe's surround buses folded into them with the matrix gains of
    // 0x0F35, then fresh surround/center buses.
    clear(kBusLeft, kSamples);
    clear(kBusRight, kSamples);
    {
        std::array<std::int16_t, kSamples> fold{};
        for (std::uint32_t i = 0; i < kSamples; ++i) {
            fold[i] = static_cast<std::int16_t>(
                rounded_mid(static_cast<std::int64_t>(d[kBusSurroundLeft + i]) * 0x6784 * 2));
        }
        add_scaled(kBusLeft, fold.data(), kSamples, 0x7FFF);
        add_scaled(kBusRight, fold.data(), kSamples, static_cast<std::int16_t>(0xB820));
        for (std::uint32_t i = 0; i < kSamples; ++i) {
            fold[i] = static_cast<std::int16_t>(
                rounded_mid(static_cast<std::int64_t>(d[kBusSurroundRight + i]) * 0x6784 * 2));
        }
        add_scaled(kBusLeft, fold.data(), kSamples, 0x47E0);
        add_scaled(kBusRight, fold.data(), kSamples, static_cast<std::int16_t>(0x8001));
    }
    clear(kBusSurroundRight, kSamples);
    clear(kBusSurroundLeft, kSamples);
    clear(kBusRightInverted, kSamples);
    clear(kBusCenter, kSamples);
    clear(kBusDiscard, kSamples);
    // FX inputs keep their last 8 samples as FIR history.
    for (const std::uint32_t base : kFxBus) {
        std::copy_n(d + base + kSamples, 8u, d + base);
        clear(base + 8u, kSamples);
    }

    // FX lines (0x01C2): delayed signal into each line's bus, optional
    // 8-tap FIR, sends to their destination buses.
    for (std::size_t line = 0; line < fx_.size(); ++line) {
        fx_line_read(line);
    }
    // Surround effect sends of the previous subframe (0x01F0).
    add_scaled_dram(0x0E80u, kSendSurroundLeft, kSamples, 0x7FFF);
    add_scaled_dram(0x0EE0u, kSendSurroundLeft, kSamples, static_cast<std::int16_t>(0xB820));
    add_scaled_dram(0x0E80u, kSendSurroundRight + 40u, 40u, static_cast<std::int16_t>(0xB820));
    add_scaled_dram(0x0EE0u, kSendSurroundRight + 40u, 40u, 0x7FFF);
    clear(kSendSurroundLeft, kSamples);
    clear(kSendSurroundRight, kSamples);

    evolve_oscillator_tables();

    for (std::uint32_t channel = 0; channel < channel_count_; ++channel) {
        const std::uint16_t map = maps_[(channel >> 4u) & 3u];
        if (((map << (channel & 15u)) & 0x8000u) == 0u) {
            continue;
        }
        render_channel(channels_ + channel * kRecordBytes, channel);
    }

    // Output (0x042D..0x045C): center at -3 dB into both sides, mixer level,
    // inverted-right bus.
    add_scaled_dram(kBusLeft, kBusCenter, kSamples, 0x5A82);
    add_scaled_dram(kBusRight, kBusCenter, kSamples, 0x5A82);
    write_output(kBusLeft, left_plane_);
    add_scaled_dram(kBusRight, kBusRightInverted, kSamples, static_cast<std::int16_t>(0x8000));
    write_output(kBusRight, right_plane_);

    // Second half of the delayed right surround send (0x023F), then the FX
    // delay-line writes.
    add_scaled_dram(0x0EA8u, kSendSurroundRight, 40u, static_cast<std::int16_t>(0xB820));
    add_scaled_dram(0x0F08u, kSendSurroundRight, 40u, 0x7FFF);
    for (std::size_t line = 0; line < fx_.size(); ++line) {
        fx_line_write(line);
    }
}

void JAudioHostMixer::render_channel(std::byte* record, std::uint32_t index) {
    if (r16(record, 0x000u) == 0u || r16(record, 0x002u) != 0u) {
        return;
    }
    ++stats_.channel_renders;
    std::int16_t* out = channel_out_.data() + kOutOffset;
    const bool paused = r16(record, 0x00Cu) != 0u;
    if (paused) {
        std::fill_n(out, kSamples, rs16(record, 0x066u));
    } else {
        render_source(record, out);
    }
    apply_fir(record, out);
    apply_iir(record, out);
    if (bus_gains_set_) {
        const bool starting = r16(record, 0x008u) != 0u;
        if (starting || category_retries_[index] != 0u) {
            classify_channel(index, record, starting);
        }
        const galaxy::audio::NativeAudioBus bus = categories_[index].decision.bus;
        galaxy::audio::NativeAudioBusRamp& ramp = bus_state_.ramps[index];
        galaxy::audio::native_audio_bus_set_ramp_target(
            ramp, bus_gain_q16_[static_cast<std::size_t>(bus)], bus);
        if (ramp.remaining != 0u || ramp.current_q16 != galaxy::audio::kNativeAudioUnityGainQ16) {
            bus_state_.active_channel = static_cast<std::uint16_t>(index);
            bus_state_.active_channel_valid = true;
            for (std::uint32_t k = 0; k < kSamples; ++k) {
                out[k] = galaxy::audio::native_dsp_audio_bus_apply_sample(bus_state_, out[k]);
            }
        }
    }
    if (r16(record, 0x058u) != 0u) {
        mix_auto(record, out);
    } else {
        mix_buses(record, out);
    }
    if (!paused) {
        w16(record, 0x008u, 0u);
    }
}

std::uint32_t JAudioHostMixer::render_source(std::byte* record, std::int16_t* out) {
    const std::uint16_t format = r16(record, 0x100u);
    const std::uint32_t needed =
        (static_cast<std::uint32_t>(r16(record, 0x004u)) * kSamples +
         r16(record, 0x060u)) >> 12u;
    std::int16_t* history = source_.data();
    const auto load_history = [&] {
        for (std::uint32_t k = 0; k < 4u; ++k) {
            history[k] = rs16(record, 0x078u + k * 2u);
        }
    };
    switch (format) {
    case 9u:
    case 5u:
        load_history();
        decode_adpcm(record, history + 4, needed);
        resample(record, history, out);
        break;
    case 8u:
    case 0x10u:
        ++stats_.pcm_renders;
        load_history();
        read_pcm(record, history + 4, needed, format == 8u ? 1u : 2u);
        resample(record, history, out);
        break;
    default:
        render_oscillator(record, out, format);
        break;
    }
    return needed;
}

// 0x0E75: positions advance by pitch/4096 source samples; each output is the
// game's DSPRES_FILTER row for the position's top six fraction bits applied
// to four consecutive samples, the first four being the previous subframe's
// tail kept in the record. Steps of 4.0 or more pick samples directly.
void JAudioHostMixer::resample(std::byte* record, std::int16_t* x, std::int16_t* out) {
    const std::uint32_t pitch = r16(record, 0x004u);
    const std::uint32_t step = pitch << 4u;
    std::uint32_t position = static_cast<std::uint32_t>(r16(record, 0x060u)) << 4u;
    if ((pitch >> 12u) >= 4u) {
        for (std::uint32_t i = 0; i < kSamples; ++i) {
            out[i] = x[position >> 16u];
            position += step;
        }
    } else {
        for (std::uint32_t i = 0; i < kSamples; ++i) {
            const std::int16_t* taps = dram_.data() + ((position >> 10u) & 0x3Fu) * 4u;
            const std::int16_t* s = x + (position >> 16u);
            const std::int64_t sum = static_cast<std::int64_t>(s[0]) * taps[0] +
                static_cast<std::int64_t>(s[1]) * taps[1] +
                static_cast<std::int64_t>(s[2]) * taps[2] +
                static_cast<std::int64_t>(s[3]) * taps[3];
            out[i] = sat16(rounded_mid(sum * 2));
            position += step;
        }
    }
    const std::uint32_t end = position >> 16u;
    w16(record, 0x066u, static_cast<std::uint16_t>(out[kSamples - 1u]));
    w16(record, 0x060u, (position >> 4u) & 0xFFFu);
    for (std::uint32_t k = 0; k < 4u; ++k) {
        w16(record, 0x078u + k * 2u, static_cast<std::uint16_t>(x[end + k]));
    }
}

// 0x086E: AFC decode with the DSP's block bookkeeping in the record.
//   0x064 decoded samples still unread in the last block (0x0B0..0x0CF,
//         whose last two words are the predictor history)
//   0x070 next block address, VARAM-relative
//   0x074 samples left before the end (or loop end)
void JAudioHostMixer::decode_adpcm(std::byte* record, std::int16_t* out,
                                   std::uint32_t count) {
    if (r16(record, 0x008u) != 0u) {
        w16(record, 0x0F6u, r16(record, 0x0F6u) + 1u);
        w16(record, 0x064u, 0u);
        w16(record, 0x0CCu, 0u);
        w16(record, 0x0CEu, 0u);
        w32(record, 0x074u, r32(record, 0x114u));
        w32(record, 0x070u, r32(record, 0x118u));
    }
    if (r16(record, 0x002u) != 0u) {
        std::fill_n(out, count, std::int16_t{0});
        return;
    }
    const auto copy_leftover = [&](std::uint32_t leftover) {
        for (std::uint32_t k = 0; k < leftover; ++k) {
            out[k] = rs16(record, 0x0B0u + (16u - leftover + k) * 2u);
        }
    };
    std::uint32_t leftover = r16(record, 0x064u);
    if (count <= leftover) {
        copy_leftover(leftover);
        w16(record, 0x064u, leftover - count);
        return;
    }
    copy_leftover(leftover);
    out += leftover;
    count -= leftover;
    for (std::uint32_t loops = 0;; ++loops) {
        const std::uint32_t left = r32(record, 0x074u);
        if (left < count) {
            if (left != 0u) {
                count -= left;
                decode_adpcm_blocks(record, out, (left + 15u) >> 4u);
                out += left;
            }
            if (r16(record, 0x102u) == 0u || loops >= 8u) {
                w16(record, 0x002u, 1u);
                std::fill_n(out, count, std::int16_t{0});
                return;
            }
            // Loop: restart at the loop start's block with the stored
            // predictor history, skipping into the block.
            const std::uint32_t loop_start = r32(record, 0x110u);
            w32(record, 0x070u, r32(record, 0x118u) + (loop_start >> 4u) * r16(record, 0x100u));
            w16(record, 0x0CEu, r16(record, 0x104u));
            w16(record, 0x0CCu, r16(record, 0x106u));
            std::array<std::int16_t, 16> block{};
            decode_adpcm_blocks(record, block.data(), 1u);
            for (std::uint32_t k = 0; k < 16u; ++k) {
                w16(record, 0x0B0u + k * 2u, static_cast<std::uint16_t>(block[k]));
            }
            leftover = 16u - (loop_start & 15u);
            w32(record, 0x074u, r32(record, 0x114u) - loop_start - leftover);
            if (count <= leftover) {
                copy_leftover(leftover);
                w16(record, 0x064u, leftover - count);
                return;
            }
            copy_leftover(leftover);
            out += leftover;
            count -= leftover;
            continue;
        }
        const std::uint32_t blocks = (count + 15u) >> 4u;
        const std::uint32_t samples = blocks << 4u;
        std::uint32_t remaining = 0;
        if (left >= samples) {
            w32(record, 0x074u, left - samples);
            remaining = samples - count;
        } else {
            remaining = left - count;
            w32(record, 0x074u, 0u);
        }
        w16(record, 0x064u, remaining);
        decode_adpcm_blocks(record, out, blocks);
        if (remaining != 0u) {
            const std::int16_t* last = out + samples - 16u;
            for (std::uint32_t k = 0; k < 16u; ++k) {
                w16(record, 0x0B0u + k * 2u, static_cast<std::uint16_t>(last[k]));
            }
            // A sample end inside the final block: move its valid tail to
            // the end of the leftover buffer.
            // The end's low 16 bits (the u32 at 0x114 is in host order).
            const auto end_low = static_cast<std::uint16_t>(r32(record, 0x114u) & 0xFFFFu);
            if (r32(record, 0x074u) == 0u && end_low != 0u) {
                const std::uint32_t last_valid = (end_low + 15u) & 15u;
                for (std::uint32_t j = 0; j < remaining && j <= last_valid; ++j) {
                    w16(record, 0x0B0u + (15u - j) * 2u,
                          r16(record, 0x0B0u + (last_valid - j) * 2u));
                }
            }
        }
        return;
    }
}

void JAudioHostMixer::decode_adpcm_blocks(std::byte* record, std::int16_t* out,
                                          std::uint32_t blocks) {
    const std::uint32_t block_bytes = r16(record, 0x100u);
    std::int32_t s1 = rs16(record, 0x0CEu);
    std::int32_t s2 = rs16(record, 0x0CCu);
    std::uint32_t address = r32(record, 0x070u);
    for (std::uint32_t b = 0; b < blocks; ++b, out += 16) {
        ++stats_.adpcm_blocks;
        const std::byte* block = resolve_wave(address, block_bytes);
        address += block_bytes;
        if (block == nullptr) {
            ++stats_.unresolved_reads;
            std::fill_n(out, 16u, std::int16_t{0});
            continue;
        }
        const auto header = static_cast<std::uint32_t>(block[0]);
        const std::uint32_t scale = header >> 4u;
        const std::uint32_t index = header & 0xFu;
        const std::int64_t c1 = dram_[0x300u + index * 2u];
        const std::int64_t c2 = dram_[0x301u + index * 2u];
        for (std::uint32_t k = 0; k < 16u; ++k) {
            std::int64_t delta = 0;
            if (block_bytes == 5u) {
                const auto byte = static_cast<std::uint8_t>(block[1u + k / 4u]);
                const auto bits = static_cast<std::int8_t>(
                    static_cast<std::uint8_t>(byte << ((k & 3u) * 2u)));
                delta = static_cast<std::int64_t>(bits >> 6) << (scale + 2u);
            } else {
                const auto byte = static_cast<std::uint8_t>(block[1u + k / 2u]);
                const auto bits = static_cast<std::int8_t>(
                    (k & 1u) != 0u ? static_cast<std::uint8_t>(byte << 4u) : byte);
                delta = static_cast<std::int64_t>(bits >> 4) << scale;
            }
            const std::int64_t value = (delta << 11) + c1 * s1 + c2 * s2;
            const std::int16_t sample = sat16(value >> 11);
            s2 = s1;
            s1 = sample;
            out[k] = sample;
        }
    }
    w32(record, 0x070u, address);
    w16(record, 0x0CEu, static_cast<std::uint16_t>(s1));
    w16(record, 0x0CCu, static_cast<std::uint16_t>(s2));
}

// 0x0AC1 (PCM8) / 0x0B3E (PCM16): _68 sample position, _70 next byte
// address, _74 samples left, 0x00A loop pending.
void JAudioHostMixer::read_pcm(std::byte* record, std::int16_t* out,
                               std::uint32_t count, std::uint32_t bytes_per_sample) {
    const auto restart = [&] {
        const std::uint32_t position = r32(record, 0x068u);
        w32(record, 0x074u, r32(record, 0x114u) - position);
        w32(record, 0x070u, r32(record, 0x118u) + position * bytes_per_sample);
        w16(record, 0x00Au, 0u);
    };
    const auto finish = [&](std::uint32_t remaining) {
        std::fill_n(out, remaining, std::int16_t{0});
        w16(record, 0x002u, 1u);
    };
    const auto fetch = [&](std::uint32_t samples) {
        if (samples == 0u) {
            return;
        }
        const std::byte* data =
            resolve_wave(r32(record, 0x070u), samples * bytes_per_sample);
        if (data == nullptr) {
            ++stats_.unresolved_reads;
            std::fill_n(out, samples, std::int16_t{0});
        } else if (bytes_per_sample == 2u) {
            for (std::uint32_t k = 0; k < samples; ++k) {
                out[k] = sbe16(data, k * 2u);
            }
        } else {
            for (std::uint32_t k = 0; k < samples; ++k) {
                out[k] = static_cast<std::int16_t>(
                    static_cast<std::uint16_t>(static_cast<std::uint32_t>(data[k]) << 8u));
            }
        }
    };
    if (r16(record, 0x002u) != 0u) {
        finish(count);
        return;
    }
    if (r16(record, 0x008u) != 0u) {
        restart();
    }
    bool pending = r16(record, 0x00Au) != 0u;
    for (std::uint32_t loops = 0;; ++loops) {
        if (pending) {
            w16(record, 0x00Au, 0u);
            if (r16(record, 0x102u) == 0u || loops >= 8u) {
                finish(count);
                return;
            }
            w32(record, 0x068u, r32(record, 0x110u));
            restart();
        }
        const std::uint32_t left = r32(record, 0x074u);
        if (left < count) {
            fetch(left);
            out += left;
            count -= left;
            pending = true;
            continue;
        }
        fetch(count);
        w32(record, 0x074u, left - count);
        if (left == count) {
            w16(record, 0x00Au, 1u);
        }
        w32(record, 0x070u, r32(record, 0x070u) + count * bytes_per_sample);
        return;
    }
}

void JAudioHostMixer::render_oscillator(std::byte* record, std::int16_t* out,
                                        std::uint16_t type) {
    ++stats_.oscillator_renders;
    const std::uint32_t pitch = r16(record, 0x004u);
    std::uint32_t phase = r16(record, 0x060u);
    const auto table = [&](std::uint32_t base) {
        std::uint32_t index = (phase >> 10u) & 0x3Fu;
        std::uint32_t fraction = (phase << 6u) & 0xFFFFu;
        const std::uint32_t step = pitch << 5u;
        for (std::uint32_t i = 0; i < kSamples; ++i) {
            fraction += step;
            out[i] = dram_[base + index];
            index = (index + (fraction >> 16u)) & 0x3Fu;
            fraction &= 0xFFFFu;
        }
        phase = ((index << 10u) | (fraction >> 6u)) & 0xFFFFu;
    };
    switch (type & 0x3Fu) {
    case 0u: {  // square, 50 %
        std::uint32_t p = phase << 1u;
        for (std::uint32_t i = 0; i < kSamples; ++i) {
            out[i] = (p & 0x10000u) != 0u ? std::int16_t{-0x4000} : std::int16_t{0x4000};
            p += pitch;
        }
        phase = (p >> 1u) & 0xFFFFu;
        break;
    }
    case 1u: {  // sawtooth
        const std::uint32_t step = pitch >> 1u;
        for (std::uint32_t i = 0; i < kSamples; ++i) {
            out[i] = static_cast<std::int16_t>(static_cast<std::uint16_t>(phase));
            phase += step;
        }
        phase &= 0xFFFFu;
        break;
    }
    case 2u: {  // square, frequency-modulated by the right surround bus
        std::uint32_t p = phase << 1u;
        for (std::uint32_t i = 0; i < kSamples; ++i) {
            out[i] = (p & 0x10000u) != 0u ? std::int16_t{-0x4000} : std::int16_t{0x4000};
            const std::uint32_t modulator =
                static_cast<std::uint16_t>(dram_[kBusSurroundRight + i]);
            p += (modulator + pitch) & 0xFFFFu;
            dram_[kBusSurroundRight + i] = 0;
        }
        phase = (p >> 1u) & 0xFFFFu;
        break;
    }
    case 3u: {  // square, 25 %
        std::uint32_t p = phase << 2u;
        const std::uint32_t step = pitch << 1u;
        for (std::uint32_t i = 0; i < kSamples; ++i) {
            out[i] = ((p >> 16u) & 3u) != 0u ? std::int16_t{-0x4000} : std::int16_t{0x4000};
            p += step;
        }
        phase = (p >> 2u) & 0xFFFFu;
        break;
    }
    case 4u:
        table(0x140u);  // triangle
        break;
    case 6u:
        std::fill_n(out, kSamples, static_cast<std::int16_t>(pitch));
        break;
    case 7u:
    case 10u:  // the DSP also phase-modulates type 10 from the surround bus
        table(0x100u);  // sine
        break;
    case 11u:
        table(0x180u);
        break;
    case 12u:
        table(0x1C0u);
        break;
    default:
        ++stats_.unsupported_oscillators;
        std::fill_n(out, kSamples, std::int16_t{0});
        return;
    }
    w16(record, 0x060u, phase);
}

// 0x0C62: T taps (mode & 0x1F) over the previous T inputs and this
// subframe's, coefficients from 0x120, history at 0x080.
void JAudioHostMixer::apply_fir(std::byte* record, std::int16_t* samples) {
    const std::uint32_t taps = r16(record, 0x108u) & 0x1Fu;
    if (taps == 0u) {
        return;
    }
    ++stats_.fir_renders;
    std::array<std::int16_t, 32 + kSamples> x{};
    std::array<std::int16_t, 32> c{};
    for (std::uint32_t k = 0; k < taps; ++k) {
        x[k] = rs16(record, 0x080u + k * 2u);
        c[k] = rs16(record, 0x120u + k * 2u);
    }
    std::copy_n(samples, kSamples, x.data() + taps);
    for (std::uint32_t k = 0; k < taps; ++k) {
        w16(record, 0x080u + k * 2u, static_cast<std::uint16_t>(x[kSamples + k]));
    }
    for (std::uint32_t n = 0; n < kSamples; ++n) {
        std::int64_t sum = 0;
        for (std::uint32_t k = 0; k < taps; ++k) {
            sum += static_cast<std::int64_t>(c[k]) * x[n + k];
        }
        samples[n] = sat16(rounded_mid(sum * 2));
    }
}

// 0x0C7D: y[n] = c0 x[n] + c1 x[n-1] + c2 y[n-1] + c3 y[n-2] (Q15), output
// one sample late; state (x[-1], x[-2], y[-2], y[-3]) at 0x0A8. Coefficients
// {0x7FFF, 0, 0, 0} bypass the filter.
void JAudioHostMixer::apply_iir(std::byte* record, std::int16_t* samples) {
    if ((r16(record, 0x108u) & 0x20u) == 0u) {
        return;
    }
    const std::int64_t c0 = rs16(record, 0x148u);
    const std::int64_t c1 = rs16(record, 0x14Au);
    const std::int64_t c2 = rs16(record, 0x14Cu);
    const std::int64_t c3 = rs16(record, 0x14Eu);
    if (c1 == 0 && c2 == 0 && c3 == 0 && c0 == 0x7FFF) {
        return;
    }
    ++stats_.iir_renders;
    std::int64_t x1 = rs16(record, 0x0A8u);
    std::int64_t x2 = rs16(record, 0x0AAu);
    std::int64_t y2 = rs16(record, 0x0ACu);
    std::int64_t y3 = rs16(record, 0x0AEu);
    std::int64_t y1 = sat16((c0 * x1 + c1 * x2 + c2 * y2 + c3 * y3) >> 15);
    for (std::uint32_t n = 0; n < kSamples; ++n) {
        const std::int64_t x0 = samples[n];
        samples[n] = static_cast<std::int16_t>(y1);
        const std::int64_t y0 = sat16((c0 * x0 + c1 * x1 + c2 * y1 + c3 * y2) >> 15);
        y3 = y2;
        y2 = y1;
        y1 = y0;
        x2 = x1;
        x1 = x0;
    }
    // After the last input the DSP holds (x[79], x[78], y[78], y[77]).
    w16(record, 0x0A8u, static_cast<std::uint16_t>(x1));
    w16(record, 0x0AAu, static_cast<std::uint16_t>(x2));
    w16(record, 0x0ACu, static_cast<std::uint16_t>(y2));
    w16(record, 0x0AEu, static_cast<std::uint16_t>(y3));
}

// 0x0392..0x0406: six bus records at 0x010 (connect, target, current,
// delay target<<8 | delay), ramped over the first 64 samples; the delay
// moves one sample per subframe toward its target and reads the record's
// history of the last 0x00E output samples (0x0D0..0x0EF).
void JAudioHostMixer::mix_buses(std::byte* record, std::int16_t* samples) {
    const std::uint32_t history = std::min<std::uint32_t>(r16(record, 0x00Eu), kMaxDelay);
    std::fill_n(samples - kMaxDelay, kMaxDelay, std::int16_t{0});
    for (std::uint32_t j = 1; j <= history; ++j) {
        samples[-static_cast<std::int32_t>(j)] = rs16(record, 0x0EEu - (j - 1u) * 2u);
    }
    if (r16(record, 0x10Au) != 0u) {
        // Forced stop: every target becomes half its current volume; the
        // channel finishes once all current volumes are zero.
        bool silent = true;
        for (std::uint32_t bus = 0; bus < 6u; ++bus) {
            const std::uint16_t current = r16(record, 0x014u + bus * 8u);
            w16(record, 0x012u + bus * 8u, current >> 1u);
            silent = silent && current == 0u;
        }
        if (silent) {
            w16(record, 0x002u, 1u);
        }
    }
    for (std::uint32_t bus = 0; bus < 6u; ++bus) {
        const std::uint32_t base = 0x010u + bus * 8u;
        const std::uint16_t connect = r16(record, base);
        const std::int16_t target = rs16(record, base + 2u);
        const std::int16_t current = rs16(record, base + 4u);
        const std::uint16_t delay_word = r16(record, base + 6u);
        const std::uint32_t delay = delay_word & 0xFFu;
        const std::uint32_t delay_target = delay_word >> 8u;
        if (delay_target != delay) {
            w16(record, base + 6u, delay_target > delay ? delay_word + 1u : delay_word - 1u);
        }
        if (connect == 0u || connect + kSamples > dram_.size()) {
            continue;
        }
        const std::int16_t final_volume = ramp_mix(
            connect, samples - static_cast<std::int32_t>(std::min(delay, kMaxDelay)),
            current, static_cast<std::int32_t>(target) - current);
        w16(record, base + 4u, static_cast<std::uint16_t>(final_volume));
    }
    for (std::uint32_t j = 0; j < history; ++j) {
        w16(record, 0x0EEu - j * 2u,
              static_cast<std::uint16_t>(samples[kSamples - 1u - j]));
    }
}

// 0x0DC3: pan (0x050 high byte) and front/surround balance (low byte) pick
// quarter-sine gains from the table at data memory 0x200; volume ramps from
// 0x054 to 0x056, effect sends scale by 0x052.
void JAudioHostMixer::mix_auto(std::byte* record, const std::int16_t* samples) {
    ++stats_.auto_mixer_renders;
    if (r16(record, 0x10Au) != 0u) {
        const std::uint16_t current = r16(record, 0x054u);
        w16(record, 0x056u, current >> 1u);
        if (current == 0u) {
            w16(record, 0x002u, 1u);
        }
    }
    const std::uint16_t pan_word = r16(record, 0x050u);
    const std::uint32_t pan = std::min<std::uint32_t>(pan_word >> 8u, 0x7Fu);
    const std::uint32_t balance = pan_word & 0x7Fu;
    const std::int32_t left = dram_[0x200u + (pan ^ 0x7Fu)];
    const std::int32_t right = dram_[0x200u + pan];
    const std::int32_t front = dram_[0x200u + (balance ^ 0x7Fu)];
    const std::int32_t rear = dram_[0x200u + balance];
    const std::array<std::int32_t, 4> gains{
        q15(left, front), q15(right, front), q15(left, rear), q15(right, rear)};
    const std::int32_t current = rs16(record, 0x054u);
    const std::int32_t target = rs16(record, 0x056u);
    const std::int32_t send = rs16(record, 0x052u);
    const std::int32_t send_volume = q15(send, current);
    const auto delta = static_cast<std::int16_t>(target - current);
    w16(record, 0x054u, static_cast<std::uint16_t>(target));
    for (std::uint32_t i = 0; i < 8u; ++i) {
        const std::int32_t gain = gains[i & 3u];
        std::int32_t start = q15(gain, current);
        std::int32_t ramp = q15(gain, delta);
        if (i >= 4u) {
            start = q15(gain, send_volume);
            ramp = q15(ramp, send);
        }
        ramp_mix(kAutoMixerBus[i], samples, static_cast<std::int16_t>(start), ramp);
    }
}

// 0x0D99: volume steps by delta/32 after every second sample, so it reaches
// start + delta at sample 65; gain is volume / 65536.
std::int16_t JAudioHostMixer::ramp_mix(std::uint32_t destination,
                                       const std::int16_t* source,
                                       std::int16_t start, std::int32_t delta) {
    std::int64_t volume = static_cast<std::int64_t>(start) * 65536;
    const std::int64_t step = (static_cast<std::int64_t>(delta) * 65536) >> 5;
    if (volume == 0 && step == 0) {
        return start;
    }
    std::int16_t* dst = dram_.data() + destination;
    const auto mix = [&](std::uint32_t k) {
        const std::int64_t gain = mid_of(volume);
        dst[k] = sat16(static_cast<std::int64_t>(dst[k]) + ((gain * source[k]) >> 16));
    };
    mix(0u);
    for (std::uint32_t i = 0; i < 32u; ++i) {
        mix(2u * i + 1u);
        mix(2u * i + 2u);
        volume += step;
    }
    for (std::uint32_t k = 65u; k < kSamples; ++k) {
        mix(k);
    }
    return mid_of(volume);
}

// 0x00FF: destination += round(source * gain * 2 / 65536).
void JAudioHostMixer::add_scaled(std::uint32_t destination, const std::int16_t* source,
                                 std::uint32_t count, std::int16_t gain) {
    std::int16_t* dst = dram_.data() + destination;
    for (std::uint32_t i = 0; i < count; ++i) {
        dst[i] = sat16(static_cast<std::int64_t>(dst[i]) +
                       rounded_mid(static_cast<std::int64_t>(source[i]) * gain * 2));
    }
}

void JAudioHostMixer::add_scaled_dram(std::uint32_t destination, std::uint32_t source,
                                      std::uint32_t count, std::int16_t gain) {
    std::array<std::int16_t, kSamples> copy{};
    std::copy_n(dram_.data() + source, count, copy.data());
    add_scaled(destination, copy.data(), count, gain);
}

// 0x0C43: y[n] = sum c[k] x[n + k] over the bus's 8 history words and 80
// samples, written in place from the bus start.
void JAudioHostMixer::fir8_in_place(std::uint32_t base,
                                    const std::array<std::int16_t, 8>& coefficients) {
    std::int16_t* x = dram_.data() + base;
    for (std::uint32_t n = 0; n < kSamples; ++n) {
        std::int64_t sum = 0;
        for (std::uint32_t k = 0; k < 8u; ++k) {
            sum += static_cast<std::int64_t>(coefficients[k]) * x[n + k];
        }
        x[n] = sat16(rounded_mid(sum * 2));
    }
}

// 0x0A91: the two self-modifying oscillator tables (types 11 and 12) are
// regenerated every subframe from their own last entries.
void JAudioHostMixer::evolve_oscillator_tables() {
    std::int16_t* t = dram_.data();
    std::int64_t ac0 = std::int64_t{0x50} * 65536;
    std::int64_t ac1 = 0;
    std::int16_t ax0_low = t[0x1BEu];
    std::int16_t ax0_high = t[0x1BFu];
    std::int64_t product = static_cast<std::int64_t>(ax0_low) * ax0_high;
    std::uint32_t read = 0x180u;
    std::uint32_t write = 0x180u;
    ac0 = with_mid(ac0, t[read++]);
    ac1 = with_mid(ac1, mid_of(ac0));
    for (std::uint32_t i = 0; i < 32u; ++i) {
        ac0 = wrap40(-ac0);
        {
            const std::int64_t previous = product;
            product = static_cast<std::int64_t>(mid_of(ac1)) * ax0_high;
            ac0 = wrap40(ac0 + previous);
            ax0_high = t[read++];
        }
        ac1 = wrap40(static_cast<std::int64_t>(ax0_high) * 65536);
        t[write++] = mid_of(ac0);
        {
            const std::int64_t previous = product;
            product = static_cast<std::int64_t>(mid_of(ac0)) * ax0_high;
            ac1 = wrap40(ac1 + previous);
            ax0_high = t[read++];
        }
        ac1 = wrap40(ac1 * 2);
        const std::int16_t stored = mid_of(ac1);
        ac0 = wrap40(static_cast<std::int64_t>(ax0_high) * 65536);
        t[write++] = stored;
    }

    ax0_high = t[0x1FEu];
    ax0_low = t[0x1FFu];
    write = 0x1C0u;
    ac1 = with_mid(ac1, ax0_low);
    product = static_cast<std::int64_t>(ax0_low) * ax0_high;
    const std::int64_t rounded =
        ((product & 0x10000) != 0 ? product + 0x8000 : product + 0x7FFF) & ~std::int64_t{0xFFFF};
    ac0 = wrap40(rounded + static_cast<std::int64_t>(ax0_high) * 65536);
    ac0 = with_mid(ac0, static_cast<std::int16_t>((mid_of(ac0) & 0x1FF) | 0x2000));
    if (ac1 > 0) {
        ac0 = wrap40(-ac0);
    }
    const std::int64_t step = static_cast<std::int64_t>(mid_of(ac0)) * 65536;
    ac1 = wrap40(ac1 + step);
    for (std::uint32_t k = 0; k < 64u; ++k) {
        t[write++] = mid_of(ac1);
        ac1 = wrap40(ac1 + step);
    }
}

void JAudioHostMixer::fx_line_read(std::size_t line) {
    FxLine& fx = fx_[line];
    if (fx.flags == 0u || fx.frames == 0u) {
        return;
    }
    ++stats_.fx_line_subframes;
    if (fx_index_[line] >= fx.frames) {
        fx_index_[line] = 0;
    }
    const std::uint32_t base = kFxBus[line];
    if (fx.buffer == nullptr) {
        ++stats_.unresolved_reads;
    } else {
        add_scaled(base + 8u, fx.buffer + fx_index_[line] * kSamples, kSamples, 0x7FFF);
    }
    if ((fx.flags & 1u) != 0u) {
        fir8_in_place(base, fx.coefficients);
    }
    if (fx.dest_a != 0u && fx.dest_a + kSamples <= dram_.size()) {
        add_scaled_dram(fx.dest_a, base, kSamples, fx.volume_a);
    }
    if (fx.dest_b != 0u && fx.dest_b + kSamples <= dram_.size()) {
        add_scaled_dram(fx.dest_b, base, kSamples, fx.volume_b);
    }
    if ((fx.flags & 2u) != 0u) {
        fir8_in_place(base, fx.coefficients);
    }
}

void JAudioHostMixer::fx_line_write(std::size_t line) {
    const FxLine& fx = fx_[line];
    if (fx.flags == 0u || fx.frames == 0u) {
        return;
    }
    const std::uint32_t index = fx_index_[line];
    fx_index_[line] = static_cast<std::uint16_t>(index + 1u >= fx.frames ? 0u : index + 1u);
    if (fx.buffer == nullptr) {
        ++stats_.unresolved_reads;
        return;
    }
    std::copy_n(dram_.data() + kFxBus[line], kSamples, fx.buffer + index * kSamples);
}

// 0x0DB1: sample * mixer level / 4096, saturated.
void JAudioHostMixer::write_output(std::uint32_t bus, std::int16_t* plane) {
    if (plane == nullptr) {
        return;
    }
    std::int16_t* dst = plane + next_subframe_ * kSamples;
    const std::int64_t level = mixer_level_;
    for (std::uint32_t i = 0; i < kSamples; ++i) {
        dst[i] = sat16((dram_[bus + i] * level) >> 12);
    }
}

const std::byte* JAudioHostMixer::resolve_wave(std::uint32_t offset, std::uint32_t size) const {
    if (aux_ == nullptr || offset > aux_size_ || size > aux_size_ - offset) {
        return nullptr;
    }
    return aux_ + offset;
}

}  // namespace galaxy::host
