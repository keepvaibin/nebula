// The game's volume categories for the host's DSP mixer (jaudio_host_mixer.cpp): each DSP channel's category from
// its sound, and the per-channel gain ramps.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace galaxy::audio {

enum class NativeAudioBus : std::uint8_t {
    Music,
    Voice,
    Sfx,
    Ambience,
    Cutscene,
};

enum class NativeAudioBusClassification : std::uint8_t {
    OwnerSoundId,
    ChannelFormat,
    DeterministicSfxFallback,
};

struct NativeAudioBusDecision {
    NativeAudioBus bus{NativeAudioBus::Sfx};
    NativeAudioBusClassification classification{
        NativeAudioBusClassification::DeterministicSfxFallback};
};

// The game's JAudio sound IDs hold the archive section in the high byte and the SoundInfo category in the next.
// Unknown IDs join SFX, so muting music or voices never mutes an unclassified cue.
[[nodiscard]] constexpr NativeAudioBusDecision
native_audio_bus_from_sound_id(std::uint32_t sound_id) noexcept {
    const std::uint32_t section = (sound_id >> 24u) & 0xffu;
    const std::uint32_t category = (sound_id >> 16u) & 0xffu;
    if (section == 0x01u || section == 0x02u) {
        return {NativeAudioBus::Music,
                NativeAudioBusClassification::OwnerSoundId};
    }
    if (section != 0u) {
        return {};
    }

    switch (category) {
    case 0x01u:  // PLAYER_VOICE
    case 0x03u:  // BOSS_VOICE
    case 0x08u:  // ENEMY_VOICE
    case 0x0au:  // SUPPORTER_VOICE
        return {NativeAudioBus::Voice,
                NativeAudioBusClassification::OwnerSoundId};
    case 0x06u:  // ATMOSPHERE
        return {NativeAudioBus::Ambience,
                NativeAudioBusClassification::OwnerSoundId};
    case 0x07u:  // DEMO (in-engine cutscenes)
        return {NativeAudioBus::Cutscene,
                NativeAudioBusClassification::OwnerSoundId};
    case 0x00u:  // SYSTEM
    case 0x02u:  // PLAYER_MOTION
    case 0x04u:  // BOSS_MOTION
    case 0x05u:  // OBJECT
    case 0x09u:  // ENEMY_MOTION
    case 0x0bu:  // SUPPORTER_MOTION
    case 0x0cu:  // REMIX_SEQ
    case 0x0du:  // HOME_BUTTON_MENU
        return {NativeAudioBus::Sfx,
                NativeAudioBusClassification::OwnerSoundId};
    default:
        return {};
    }
}

// Without a resolved owner, the channel's sample format decides: the game's streams and sequence voices use PCM16,
// ordinary effects ADPCM or PCM8. Any other format falls back to SFX.
[[nodiscard]] constexpr NativeAudioBusDecision
native_audio_bus_from_channel_format(
    std::uint16_t block_samples,
    std::uint16_t block_bytes) noexcept {
    if (block_samples == 1u && block_bytes == 16u) {
        return {NativeAudioBus::Music,
                NativeAudioBusClassification::ChannelFormat};
    }
    if ((block_samples == 16u && block_bytes == 9u) ||
        (block_samples == 1u && block_bytes == 8u)) {
        return {NativeAudioBus::Sfx,
                NativeAudioBusClassification::ChannelFormat};
    }
    return {};
}

// The JAudio2 owner of one DSP channel, through the game's structures (field
// offsets from the Petari layouts, docs/PROVENANCE.md), every link checked:
//   JASDSPChannel::sDspChannels: 64 x 0x1C; +0x14 callback data. A
//     JASChannel's DSP channel has the JASChannel as callback data, and the
//     JASChannel points back at it (+0x08 mDspCh).
//   JASChannel +0x10 callback data: its track's TChannelMgr (+0x4C mTrack;
//     the track lists the manager at +0x170..+0x17C or embeds it at +0x180),
//     or its JASAramStream (+0x90 mChannels[6] lists the channel).
//   JASTrack +0x12C mParent up to the output track, embedded in a JAISe at
//     +0xC8 (a JAISe begins with its JSULink, whose +0 points at it) or in a
//     JAISeq at +0xA8. JAISound::mSoundID is JAISe +0x28, JAISeq +0x18,
//     JAIStream +0x18 (its JASAramStream at +0xA8).
enum class JAudioOwnerRoute : std::uint8_t { Format, Se, Seq, Stream };

struct JAudioChannelOwner {
    JAudioOwnerRoute route{JAudioOwnerRoute::Format};
    std::uint32_t sound_id{};
};

inline constexpr std::uint32_t kJasDspChannelsPointer = 0x806A2BD0u;
inline constexpr std::uint32_t kJasDspChannelSize = 0x1cu;

// `read_u32(address)` returns the big-endian word at a console address, or 0 if unmapped.
template <class ReadU32>
[[nodiscard]] JAudioChannelOwner resolve_jaudio_channel_owner(
    const ReadU32& read_u32,
    std::uint32_t index) noexcept {
    const std::uint32_t table = read_u32(kJasDspChannelsPointer);
    if (table == 0u) {
        return {};
    }
    const std::uint32_t dsp_channel = table + index * kJasDspChannelSize;
    const std::uint32_t channel = read_u32(dsp_channel + 0x14u);
    if (channel == 0u || read_u32(channel + 0x08u) != dsp_channel) {
        return {};
    }
    const std::uint32_t data = read_u32(channel + 0x10u);
    if (data == 0u) {
        return {};
    }
    const std::uint32_t track = read_u32(data + 0x4cu);
    bool track_owned = track != 0u && track + 0x180u == data;
    for (std::uint32_t k = 0u; !track_owned && track != 0u && k < 4u; ++k) {
        track_owned = read_u32(track + 0x170u + k * 4u) == data;
    }
    if (track_owned) {
        std::uint32_t root = track;
        for (std::uint32_t depth = 0u; depth < 16u; ++depth) {
            const std::uint32_t parent = read_u32(root + 0x12cu);
            if (parent == 0u || parent == root) {
                break;
            }
            root = parent;
        }
        const std::uint32_t se = root - 0xc8u;
        if (read_u32(se) == se) {
            return {JAudioOwnerRoute::Se, read_u32(se + 0x28u)};
        }
        return {JAudioOwnerRoute::Seq, read_u32(root - 0xa8u + 0x18u)};
    }
    for (std::uint32_t k = 0u; k < 6u; ++k) {
        if (read_u32(data + 0x90u + k * 4u) == channel) {
            return {JAudioOwnerRoute::Stream, read_u32(data - 0xa8u + 0x18u)};
        }
    }
    return {};
}

// The category of the sound on DSP channel `index`: its owner's sound ID,
// else the channel record's sample format (TChannel +0x064, +0x100), else SFX.
template <class ReadU32>
[[nodiscard]] NativeAudioBusDecision classify_jaudio_channel(
    const ReadU32& read_u32,
    std::uint32_t index,
    std::uint16_t block_samples,
    std::uint16_t block_bytes,
    JAudioChannelOwner* owner_out = nullptr) noexcept {
    const JAudioChannelOwner owner = resolve_jaudio_channel_owner(read_u32, index);
    if (owner_out != nullptr) {
        *owner_out = owner;
    }
    NativeAudioBusDecision decision{};
    if (owner.route != JAudioOwnerRoute::Format) {
        decision = native_audio_bus_from_sound_id(owner.sound_id);
    }
    if (decision.classification != NativeAudioBusClassification::OwnerSoundId) {
        decision = native_audio_bus_from_channel_format(block_samples, block_bytes);
    }
    return decision;
}

// Per-bus gains. `master` multiplies each bus here; the host passes 1 because it applies the master gain once, at
// the final output, so that it also covers audio mixed outside these channels (the movie player).
struct NativeAudioBusGains {
    float master{1.0f};
    float music{1.0f};
    float voice{1.0f};
    float sfx{1.0f};
    float ambience{1.0f};
    float cutscene{1.0f};
};

inline constexpr std::uint32_t kNativeAudioUnityGainQ16 = 1u << 16u;
// 10 ms (four DSP subframes): a mute or unmute of a loud low-frequency bus
// over one 2.5 ms subframe is an audible click.
inline constexpr std::uint32_t kNativeAudioBusRampSamples = 320u;

[[nodiscard]] inline std::uint32_t native_audio_gain_to_q16(
    float gain) noexcept {
    // NaN and negative gains are silence.
    if (!(gain >= 0.0f)) {
        return 0u;
    }
    const float clamped = std::min(gain, 4.0f);
    return static_cast<std::uint32_t>(
        clamped * static_cast<float>(kNativeAudioUnityGainQ16) + 0.5f);
}

inline constexpr std::size_t kNativeAudioDspChannelCount = 64u;

struct NativeAudioBusRamp {
    std::uint32_t current_q16{kNativeAudioUnityGainQ16};
    std::uint32_t target_q16{kNativeAudioUnityGainQ16};
    std::uint32_t remaining{};
    NativeAudioBus bus{NativeAudioBus::Sfx};
    bool initialized{};
};

struct NativeDspAudioBusState {
    std::array<NativeAudioBusRamp, kNativeAudioDspChannelCount>
        ramps{};
    std::uint16_t active_channel{};
    bool active_channel_valid{};
};

// A gain change on the channel's bus ramps; a channel's first sound, or a new
// owner on another bus, starts at its own gain so that nothing of a muted bus
// leaks through a ramp from the previous owner's gain.
inline void native_audio_bus_set_ramp_target(
    NativeAudioBusRamp& ramp,
    std::uint32_t gain_q16,
    NativeAudioBus bus) noexcept {
    if (!ramp.initialized || ramp.bus != bus) {
        ramp.current_q16 = gain_q16;
        ramp.initialized = true;
        ramp.remaining = 0u;
        ramp.bus = bus;
    } else if (ramp.target_q16 != gain_q16) {
        ramp.remaining = kNativeAudioBusRampSamples;
    }
    ramp.target_q16 = gain_q16;
}

[[nodiscard]] inline std::int16_t native_dsp_audio_bus_apply_sample(
    NativeDspAudioBusState& state,
    std::int16_t sample) noexcept {
    if (!state.active_channel_valid) {
        return sample;
    }
    NativeAudioBusRamp& ramp = state.ramps[state.active_channel];
    if (ramp.remaining != 0u) {
        const std::int64_t difference =
            static_cast<std::int64_t>(ramp.target_q16) - ramp.current_q16;
        ramp.current_q16 = static_cast<std::uint32_t>(
            static_cast<std::int64_t>(ramp.current_q16) +
            difference / static_cast<std::int64_t>(ramp.remaining));
        --ramp.remaining;
        if (ramp.remaining == 0u) {
            ramp.current_q16 = ramp.target_q16;
        }
    }
    if (ramp.current_q16 == kNativeAudioUnityGainQ16) {
        return sample;
    }
    const std::int64_t scaled =
        static_cast<std::int64_t>(sample) * ramp.current_q16 /
        kNativeAudioUnityGainQ16;
    return static_cast<std::int16_t>(std::clamp<std::int64_t>(
        scaled,
        std::numeric_limits<std::int16_t>::min(),
        std::numeric_limits<std::int16_t>::max()));
}

}  // namespace galaxy::audio
