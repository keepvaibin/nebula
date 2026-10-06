#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace galaxy::host {

// Adapted from Dusklight src/dusk/audio/Adpcm.cpp::Adpcm4ToPcm16 (CC0-1.0),
// commit ad979d3dae092d0f5cbdaf49eabca7b4f1db4838. This standalone
// decoder is currently for comparison only; it is not wired into game audio.
// https://github.com/TwilitRealm/dusklight/blob/ad979d3dae092d0f5cbdaf49eabca7b4f1db4838/src/dusk/audio/Adpcm.cpp
// A frame contains one predictor/scale byte and eight bytes of AFC nibbles.
inline bool native_audio_decode_dusk_adpcm4(
    std::span<const std::uint8_t> adpcm,
    std::span<std::int16_t> pcm,
    std::int16_t& history_older,
    std::int16_t& history_newer) noexcept {
    constexpr std::size_t kFrameBytes = 9;
    constexpr std::size_t kSamplesPerFrame = 16;
    constexpr std::array<std::int16_t, 16> kCoefficient0{{
        0, 0x0800, 0, 0x0400, 0x1000, 0x0e00, 0x0c00, 0x1200,
        0x1068, 0x12c0, 0x1400, 0x0800, 0x0400, -0x0400, -0x0400, -0x0800}};
    constexpr std::array<std::int16_t, 16> kCoefficient1{{
        0, 0, 0x0800, 0x0400, -0x0800, -0x0600, -0x0400, -0x0a00,
        -0x08c8, -0x08fc, -0x0c00, -0x0800, -0x0400, 0x0400, 0, 0}};

    if (adpcm.size() % kFrameBytes != 0 ||
        pcm.size() > (adpcm.size() / kFrameBytes) * kSamplesPerFrame) {
        return false;
    }
    std::size_t written = 0;
    for (std::size_t frame = 0; frame < adpcm.size() && written < pcm.size();
         frame += kFrameBytes) {
        const std::uint8_t header = adpcm[frame];
        const std::int32_t scale = 1 << (header >> 4);
        const std::uint8_t predictor = header & 0x0f;
        const std::int32_t coef0 = kCoefficient0[predictor];
        const std::int32_t coef1 = kCoefficient1[predictor];

        for (std::size_t sample = 0;
             sample < kSamplesPerFrame && written < pcm.size(); ++sample) {
            const std::uint8_t packed = adpcm[frame + 1 + sample / 2];
            const std::uint8_t nibble = (sample & 1) == 0
                ? packed >> 4 : packed & 0x0f;
            const std::int32_t delta = nibble < 8 ? nibble : nibble - 16;
            const std::int64_t numerator =
                static_cast<std::int64_t>(delta) * scale * 2048 +
                static_cast<std::int64_t>(coef0) * history_newer +
                static_cast<std::int64_t>(coef1) * history_older;
            const auto decoded = static_cast<std::int16_t>(std::clamp(
                numerator >> 11, std::int64_t{-0x8000},
                std::int64_t{0x7fff}));
            history_older = history_newer;
            history_newer = decoded;
            pcm[written++] = decoded;
        }
    }
    return true;
}

}  // namespace galaxy::host
