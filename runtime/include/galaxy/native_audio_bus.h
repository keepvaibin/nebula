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

// Galaxy's JAudio IDs store the archive section in the high byte and the
// SoundInfo category in the next byte. Unknown IDs deliberately join SFX:
// muting music or voices must never accidentally mute an unclassified cue.
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
    case 0x00u:  // SYSTEM
    case 0x02u:  // PLAYER_MOTION
    case 0x04u:  // BOSS_MOTION
    case 0x05u:  // OBJECT
    case 0x07u:  // DEMO
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

// If a live JAudio owner cannot be resolved, the channel encoding is the only
// stable information carried into the DSP record. Galaxy streams/sequence
// voices use PCM16, while ordinary effects use ADPCM or PCM8. Any unfamiliar
// format still falls back to SFX rather than acquiring another bus by chance.
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

struct NativeAudioBusGains {
    float master{1.0f};
    float music{1.0f};
    float voice{1.0f};
    float sfx{1.0f};
    float ambience{1.0f};
};

inline constexpr std::uint32_t kNativeAudioUnityGainQ16 = 1u << 16u;
inline constexpr std::uint32_t kNativeAudioMaximumGainQ16 = 4u << 16u;
inline constexpr std::uint32_t kNativeAudioBusRampSamples = 80u;

[[nodiscard]] inline std::uint32_t native_audio_gain_to_q16(
    float gain) noexcept {
    // Runtime settings are sanitized too, but this boundary remains defensive
    // because it is independently unit-testable and may receive future APIs.
    if (!(gain >= 0.0f)) {
        return 0u;
    }
    const float clamped = std::min(gain, 4.0f);
    return static_cast<std::uint32_t>(
        clamped * static_cast<float>(kNativeAudioUnityGainQ16) + 0.5f);
}

struct NativeAudioBusChannelSnapshot {
    NativeAudioBus bus{NativeAudioBus::Sfx};
    std::uint32_t gain_q16{kNativeAudioUnityGainQ16};
};

// One CPU-thread publisher and one DSP-thread reader. Each channel assignment
// and its combined gain share one atomic word, so the DSP cannot observe a bus
// from one publication with a gain from another. The DSP snapshots once per
// selected channel record; sample reads use only worker-owned ramp state and
// therefore perform no atomics, locks, or allocations.
class NativeAudioBusControls final {
public:
    static constexpr std::size_t kChannelCount = 64u;

    NativeAudioBusControls() noexcept {
        for (auto& channel : channels_) {
            channel.store(
                pack_channel(NativeAudioBus::Sfx, kNativeAudioUnityGainQ16),
                std::memory_order_relaxed);
        }
    }

    void publish(
        const NativeAudioBusGains& gains,
        const std::array<NativeAudioBus, kChannelCount>& buses) noexcept {
        const std::array<std::uint32_t, 4u> gains_q16{
            combined_gain_q16(gains.master, gains.music),
            combined_gain_q16(gains.master, gains.voice),
            combined_gain_q16(gains.master, gains.sfx),
            combined_gain_q16(gains.master, gains.ambience),
        };
        for (std::size_t channel = 0u; channel < buses.size(); ++channel) {
            const NativeAudioBus bus = buses[channel];
            channels_[channel].store(
                pack_channel(
                    bus, gains_q16[static_cast<std::size_t>(bus)]),
                std::memory_order_release);
        }
    }

    [[nodiscard]] NativeAudioBusChannelSnapshot snapshot_channel(
        std::size_t channel) const noexcept {
        if (channel >= kChannelCount) {
            return {};
        }
        const std::uint64_t packed =
            channels_[channel].load(std::memory_order_acquire);
        return {
            static_cast<NativeAudioBus>(packed >> 32u),
            static_cast<std::uint32_t>(packed),
        };
    }

private:
    [[nodiscard]] static constexpr std::uint64_t pack_channel(
        NativeAudioBus bus,
        std::uint32_t gain_q16) noexcept {
        return (static_cast<std::uint64_t>(bus) << 32u) | gain_q16;
    }

    [[nodiscard]] static std::uint32_t combined_gain_q16(
        float master,
        float bus) noexcept {
        const std::uint64_t product =
            static_cast<std::uint64_t>(native_audio_gain_to_q16(master)) *
            native_audio_gain_to_q16(bus);
        return static_cast<std::uint32_t>(std::min<std::uint64_t>(
            (product + kNativeAudioUnityGainQ16 / 2u) >> 16u,
            kNativeAudioMaximumGainQ16));
    }

    std::array<std::atomic<std::uint64_t>, kChannelCount> channels_{};
};

struct NativeAudioBusRamp {
    std::uint32_t current_q16{kNativeAudioUnityGainQ16};
    std::uint32_t target_q16{kNativeAudioUnityGainQ16};
    std::uint32_t remaining{};
    bool initialized{};
};

struct NativeDspAudioBusState {
    std::array<NativeAudioBusRamp, NativeAudioBusControls::kChannelCount>
        ramps{};
    std::uint16_t active_channel{};
    bool active_channel_valid{};
};

inline void native_dsp_audio_bus_clear_selection(
    NativeDspAudioBusState& state) noexcept {
    state.active_channel_valid = false;
}

inline void native_audio_bus_set_ramp_target(
    NativeAudioBusRamp& ramp,
    std::uint32_t gain_q16) noexcept {
    if (!ramp.initialized) {
        ramp.current_q16 = gain_q16;
        ramp.initialized = true;
        ramp.remaining = 0u;
    } else if (ramp.target_q16 != gain_q16) {
        ramp.remaining = kNativeAudioBusRampSamples;
    }
    ramp.target_q16 = gain_q16;
}

inline void native_dsp_audio_bus_begin_channel(
    NativeDspAudioBusState& state,
    std::uint16_t channel,
    const NativeAudioBusControls* controls) noexcept {
    if (controls == nullptr ||
        channel >= NativeAudioBusControls::kChannelCount) {
        native_dsp_audio_bus_clear_selection(state);
        return;
    }
    NativeAudioBusRamp& ramp = state.ramps[channel];
    const NativeAudioBusChannelSnapshot snapshot =
        controls->snapshot_channel(channel);
    native_audio_bus_set_ramp_target(ramp, snapshot.gain_q16);
    state.active_channel = channel;
    state.active_channel_valid = true;
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
