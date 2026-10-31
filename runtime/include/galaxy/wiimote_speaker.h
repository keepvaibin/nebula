#pragma once

// The Wii Remote speaker's playback side: a single-producer, single-consumer ring of mono samples at the speaker
// rate (the game streams 4-bit Yamaha ADPCM or signed 8-bit PCM in output report 0x18; WiiBrew, Wiimote: Speaker),
// resampled into the game's audio output.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace galaxy::input::speaker {

class Speaker {
public:
    static constexpr std::uint32_t kRingSamples = 8192;
    static constexpr std::uint32_t kTaps = 16;

    // Producer: the game's thread. The speaker's samples (16-bit) at `rate` Hz as the game hands them over (its 6 kHz
    // blocks of 40). Kept only while a consumer listens (a speaker volume above 0); dropped when the ring is full.
    void push(std::span<const std::int16_t> samples, std::uint32_t rate) noexcept;

    // Consumer: one audio thread, called with each output buffer as the game produces it. Adds the speaker, scaled by
    // `gain`, to both channels of interleaved 16-bit little-endian stereo at `output_rate`.
    void mix(std::span<std::byte> stereo_le, std::uint32_t output_rate, float gain) noexcept;

private:
    // Shared with the producer.
    std::unique_ptr<float[]> ring_ = std::make_unique<float[]>(kRingSamples);
    std::atomic<std::uint32_t> head_{0};
    std::atomic<std::uint32_t> tail_{0};
    std::atomic<std::uint32_t> sample_rate_{0};
    std::atomic<bool> listening_{false};

    // Consumer state: the last kTaps samples shifted in. The output position is kTaps / 2 - 1 samples into the
    // window plus `fraction_` (2^-32 sample units).
    std::array<float, kTaps> window_{};
    std::uint32_t fraction_ = 0;
    std::uint32_t owed_shifts_ = 0;
    std::uint32_t silent_shifts_ = kTaps;
    std::uint32_t last_head_ = 0;
    bool playing_ = false;
};

// The speaker of player 1's controller: the game's speaker sound (speaker_samples) goes in, the audio output mixes it.
Speaker& game_speaker() noexcept;

}  // namespace galaxy::input::speaker
