#pragma once

// The Wii Remote speaker. The game configures it through register block 0xA20000 and streams 4-bit Yamaha ADPCM or
// signed 8-bit PCM to it in output report 0x18 (WiiBrew, Wiimote: Speaker). The emulated remote decodes each report
// on the thread that receives it into one single-producer, single-consumer ring of mono samples at the speaker rate.
// The audio output resamples that ring into the game's output; a controller with a speaker can read the same stream.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace galaxy::input::speaker {

enum class Format : std::uint8_t { Adpcm4, Pcm8 };

// The configuration written to 0xA20001: 00 FF RR RR VV 00 00. FF 0x40 selects signed 8-bit PCM, anything else
// 4-bit ADPCM. RR RR (little endian) divides 12 MHz into the sample rate of either format; the game streams 40 ADPCM
// samples every 6.667 ms with 2000 (6000 Hz). VV is the volume, 0x40 for unity with ADPCM and 0xFF with PCM; the game
// writes the console's speaker volume setting (0..0x7F, 0x58 by default).
struct Config {
    Format format = Format::Adpcm4;
    std::uint32_t sample_rate = 0;
    std::uint8_t volume = 0;

    friend bool operator==(const Config&, const Config&) = default;
};
[[nodiscard]] Config parse_config(std::span<const std::uint8_t, 7> bytes) noexcept;

// Yamaha ADPCM exactly as the game's encoder (WENCGetEncodeData) tracks it: a nibble is a sign bit and magnitude
// bits L3 L2 L1; the sample moves by step * L3 + step / 2 * L2 + step / 4 * L1 + step / 8, each term truncated, and
// the step is scaled by 230, 230, 230, 230, 307, 409, 512 or 614 / 256 and kept within 127..24576. The first sample
// of each byte is in the high nibble.
struct AdpcmState {
    std::int32_t predictor = 0;
    std::int32_t step = 127;
};
[[nodiscard]] std::int16_t decode_adpcm_nibble(AdpcmState& state, std::uint8_t nibble) noexcept;

class Speaker {
public:
    static constexpr std::uint32_t kRingSamples = 8192;
    static constexpr std::uint32_t kTaps = 16;
    static constexpr std::uint32_t kMostSampleRate = 48000;

    // Producer: the thread that handles the remote's output reports. The decoder follows every report while the
    // speaker is on, as the remote's own does; samples reach the ring only while the output listens.
    void write_registers(std::uint32_t offset, std::span<const std::uint8_t> data) noexcept;
    void set_enabled(bool enabled) noexcept;
    void set_muted(bool muted) noexcept;
    // The data bytes of one 0x18 report.
    void receive(std::span<const std::uint8_t> data) noexcept;
    [[nodiscard]] Config config() const noexcept { return config_; }

    // Consumer: one audio thread, called with each output buffer as the game produces it. Adds the speaker, scaled by
    // `gain`, to both channels of interleaved 16-bit little-endian stereo at `output_rate`.
    void mix(std::span<std::byte> stereo_le, std::uint32_t output_rate, float gain) noexcept;
    // Decoded samples not yet mixed.
    [[nodiscard]] std::uint32_t buffered() const noexcept {
        return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
    }

private:
    void push(std::span<const float> samples) noexcept;

    // Producer state.
    std::array<std::uint8_t, 10> registers_{};
    Config config_{};
    AdpcmState adpcm_{};
    float volume_ = 0.0f;
    bool enabled_ = false;
    bool muted_ = true;

    // Shared. On the heap: the owner (the emulated address space) often lives on the stack in tests.
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

}  // namespace galaxy::input::speaker
