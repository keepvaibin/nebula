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

// How the game knows a channel's sound (nebula_game_audio_channel_owner): by its sample format only, or as a
// sound effect, a sequence or a stream with that sound's ID.
enum class JAudioOwnerRoute : std::uint8_t { Format, Se, Seq, Stream };

struct JAudioChannelOwner {
    JAudioOwnerRoute route{JAudioOwnerRoute::Format};
    std::uint32_t sound_id{};
};

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
