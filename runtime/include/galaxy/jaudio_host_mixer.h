#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "galaxy/native_audio_bus.h"

#include <nebula_game_api.h>

namespace galaxy::host {

// The game's JAudio DSP program, run on the host. The game's audio thread, JASDriver and the JASChannel updates run
// as usual; JASDsp drives the mixer through typed calls (nebula_game_api.h jaudio_*):
//
//   setup          the channel records, the filter tables and the FX lines, once
//   set_aux        the auxiliary memory the channels' wave offsets count from
//   sync_frame     subframe count, mixer level, L and R output planes
//   release_halt   a channel group ready (its active map); group 3 ends the
//                  channel update for one subframe, which is mixed
//                  immediately and acknowledged with AUDIOMSG_DSP.
//
// What a subframe computes is defined by the game's own DSP program (the
// JAudio ucode, 0x1F00 bytes at 0x805D8460), restated as host integer code:
// per-channel AFC/PCM decode with the program's block bookkeeping in the
// TChannel record (_64 leftover, _70 next block, _74 samples left, which
// JASAramStream::updateChannel reads back), the 64-phase 4-tap DSPRES_FILTER
// resampler, the FIR and IIR filters, six ramped bus sends with per-bus
// delay, the auto mixer's quarter-sine pan law, four FX feedback delay lines,
// the surround fold-down to stereo and the output gain (mixer level / 4096).
// Records, tables, delay lines and output planes are in host byte order; wave
// data keeps the disc's big-endian samples.
struct JAudioHostMixerStats {
    std::uint64_t setups{};
    std::uint64_t frames{};
    std::uint64_t subframes{};
    std::uint64_t channel_renders{};
    std::uint64_t adpcm_blocks{};
    std::uint64_t pcm_renders{};
    std::uint64_t oscillator_renders{};
    std::uint64_t fir_renders{};
    std::uint64_t iir_renders{};
    std::uint64_t auto_mixer_renders{};
    std::uint64_t fx_line_subframes{};
    std::uint64_t unresolved_reads{};
    std::uint64_t unsupported_oscillators{};
    std::uint64_t stray_releases{};
    std::uint64_t category_decisions{};
    std::uint64_t category_owner_misses{};
    std::uint64_t mix_ns{};
};

// A channel's volume category and the owner it was decided from.
struct JAudioChannelCategory {
    galaxy::audio::NativeAudioBusDecision decision{};
    galaxy::audio::JAudioChannelOwner owner{};
};

class JAudioHostMixer {
public:
    JAudioHostMixer();

    // The game's channel records, filter tables (640 and 32 words) and FX lines, once (JASDsp's setup).
    void setup(std::uint32_t channel_count, std::byte* channel_records, const std::int16_t* resample_filter,
               const std::int16_t* adpcm_filter, const NebulaJAudioFxLine* fx_lines);
    // The sound data in auxiliary memory (JKRHeap::getAltAramStartAdr()): wave offsets below `size`.
    void set_aux(const std::byte* base, std::uint32_t size) noexcept;
    // An audio frame: its subframes mix into the stereo output planes.
    void sync_frame(std::uint32_t subframes, std::uint32_t mixer_level, std::int16_t* left, std::int16_t* right);
    // A channel group ready (`map`: its channels' active bits). Returns true when
    // this release completed a subframe; the caller then posts the game's AUDIOMSG_DSP.
    bool release_halt(std::uint32_t group, std::uint32_t map);
    // Volume categories. The host passes the category gains once per audio frame (`master` is applied at the
    // output, not here). A channel's category is decided once per sound, at the sound's first render, when the game
    // has linked the channel to its owner (TChannel +0x008 is set by playStart and cleared after that render). The
    // gain then applies from the sound's first sample. A changed gain ramps over four subframes (320 samples).
    // Until the first call, every gain is 1.
    void set_bus_gains(const galaxy::audio::NativeAudioBusGains& gains) noexcept;
    // The game names the sound on a channel itself (its sound ID; route 1 effect, 2 sequence, 3 stream, 0 none);
    // without one the sample format decides.
    using OwnerResolver = std::uint32_t (*)(std::uint32_t channel, std::uint32_t* route);
    void set_owner_resolver(OwnerResolver resolver) noexcept { owner_resolver_ = resolver; }

    [[nodiscard]] const JAudioChannelCategory& channel_category(std::uint32_t channel) const noexcept {
        return categories_[channel < categories_.size() ? channel : 0u];
    }
    [[nodiscard]] bool configured() const noexcept { return channels_ != nullptr; }
    [[nodiscard]] const JAudioHostMixerStats& stats() const noexcept { return stats_; }

private:
    OwnerResolver owner_resolver_{};
    struct FxLine {
        std::uint16_t flags{};
        std::uint16_t frames{};
        std::int16_t* buffer{};
        std::uint16_t dest_a{};
        std::int16_t volume_a{};
        std::uint16_t dest_b{};
        std::int16_t volume_b{};
        std::array<std::int16_t, 8> coefficients{};
    };

    void mix_subframe();
    void classify_channel(std::uint32_t index, const std::byte* record, bool starting);
    void render_channel(std::byte* record, std::uint32_t index);
    std::uint32_t render_source(std::byte* record, std::int16_t* out);
    void resample(std::byte* record, std::int16_t* history_and_source,
                  std::int16_t* out);
    void decode_adpcm(std::byte* record, std::int16_t* out, std::uint32_t count);
    void decode_adpcm_blocks(std::byte* record, std::int16_t* out,
                             std::uint32_t blocks);
    void read_pcm(std::byte* record, std::int16_t* out, std::uint32_t count,
                  std::uint32_t bytes_per_sample);
    void render_oscillator(std::byte* record, std::int16_t* out,
                           std::uint16_t type);
    void apply_fir(std::byte* record, std::int16_t* samples);
    void apply_iir(std::byte* record, std::int16_t* samples);
    void mix_buses(std::byte* record, std::int16_t* samples);
    void mix_auto(std::byte* record, const std::int16_t* samples);
    std::int16_t ramp_mix(std::uint32_t destination, const std::int16_t* source,
                          std::int16_t start, std::int32_t delta);
    void add_scaled(std::uint32_t destination, const std::int16_t* source,
                    std::uint32_t count, std::int16_t gain);
    void add_scaled_dram(std::uint32_t destination, std::uint32_t source,
                         std::uint32_t count, std::int16_t gain);
    void fir8_in_place(std::uint32_t base,
                       const std::array<std::int16_t, 8>& coefficients);
    void evolve_oscillator_tables();
    void fx_line_read(std::size_t line);
    void fx_line_write(std::size_t line);
    void write_output(std::uint32_t bus, std::int16_t* plane);
    [[nodiscard]] const std::byte* resolve_wave(std::uint32_t offset, std::uint32_t size) const;

    // DSP data memory image: [0x000, 0x280) DSPRES_FILTER (resampler taps,
    // oscillator wave tables, pan table), [0x300, 0x320) ADPCM coefficients,
    // [0x900, 0x1000) the mix buses at the addresses JAudio's connect tables
    // name (0x0D00 L, 0x0D60 R, 0x0DC0.. FX inputs with 8 history words, ...).
    std::array<std::int16_t, 0x1000> dram_{};
    std::array<FxLine, 4> fx_{};
    std::array<std::uint16_t, 4> fx_index_{};
    std::array<std::uint16_t, 4> maps_{};
    std::array<std::int16_t, 4 + 2048> source_{};
    std::array<std::int16_t, 64 + 80 + 32> channel_out_{};

    std::byte* channels_{};
    std::uint32_t channel_count_{};
    const NebulaJAudioFxLine* fx_lines_{};
    const std::byte* aux_{};
    std::uint32_t aux_size_{};
    std::uint32_t mixer_level_{};
    std::int16_t* left_plane_{};
    std::int16_t* right_plane_{};
    std::uint32_t subframes_{};
    std::uint32_t next_subframe_{};
    bool frame_active_{};
    bool bus_gains_set_{};
    std::array<std::uint32_t, 5> bus_gain_q16_{};
    std::array<JAudioChannelCategory, 64> categories_{};
    // Renders left in which an owner that was unresolved at the sound's start
    // is looked up again.
    std::array<std::uint8_t, 64> category_retries_{};
    galaxy::audio::NativeDspAudioBusState bus_state_{};
    JAudioHostMixerStats stats_{};
};

}  // namespace galaxy::host
