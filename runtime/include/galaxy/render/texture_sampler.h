#pragma once

// A texture's sampling as the game's material record or lookup op states it (E-02: no filter codes or fixed-point
// LOD words): wrap per axis, the minified and magnified filters, how levels are used, the LOD range and bias in levels
// and anisotropy.

#include <cstdint>

namespace galaxy::render {

enum class TexWrap : std::uint8_t { Clamp = 0, Repeat, Mirror };

struct TextureSampler {
    TexWrap wrap_s = TexWrap::Clamp;
    TexWrap wrap_t = TexWrap::Clamp;
    bool mag_linear = false;
    bool min_linear = false;
    std::uint8_t mip = 0u;         // 0 the first level only, 1 the nearest level, 2 between levels
    std::uint8_t anisotropy = 0u;  // 0 off, 1 two, 2 four samples
    float lod_bias = 0.0f;
    float min_lod = 0.0f;
    float max_lod = 0.0f;

    [[nodiscard]] bool operator==(const TextureSampler&) const = default;
};

// The GPU sampler key's fixed point (pack_sampler_key): LODs in sixteenths of a level, the bias in thirty-seconds, and
// the minified filter as a three-bit code (linear 4, plus 1 the nearest level or 2 between levels).
[[nodiscard]] inline std::uint8_t lod_x16(float lod) noexcept {
    return static_cast<std::uint8_t>(static_cast<std::int32_t>(16.0f * lod));
}
[[nodiscard]] inline std::int8_t lod_bias_x32(float bias) noexcept {
    return static_cast<std::int8_t>(static_cast<std::int32_t>(32.0f * bias));
}
[[nodiscard]] inline std::uint32_t min_filter_code(const TextureSampler& s) noexcept {
    return (s.min_linear ? 4u : 0u) | (s.mip > 2u ? 2u : s.mip);
}

}  // namespace galaxy::render
