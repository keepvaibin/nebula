// The game's volume categories for the host's mixer (audio_mixer.cpp): each voice's category from its sound.
#pragma once

#include <cstdint>

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

// How the game names a voice's sound (NebulaAudioVoice.sound_route): by its sample format only, or as a sound effect,
// a sequence or a stream with that sound's ID.
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

}  // namespace galaxy::audio
