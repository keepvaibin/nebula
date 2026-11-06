#pragma once

// A texture's sampling as the game's material record or lookup op states it (E-02: no filter codes or fixed-point
// LOD words): wrap per axis, the minified and magnified filters, how levels are used, the LOD range and bias in levels
// and anisotropy.

#include <cstdint>
#include <bit>
#include <cstddef>

namespace galaxy::render {

enum class TexWrap : std::uint8_t { Clamp = 0, Repeat, Mirror };

struct TextureSampler {
    TexWrap wrap_s = TexWrap::Clamp;
    TexWrap wrap_t = TexWrap::Clamp;
    bool mag_linear = false;
    bool min_linear = false;
    std::uint8_t mip = 0u;         // 0 the first level only, 1 the nearest level, 2 between levels
    std::uint8_t anisotropy = 0u;  // sample count; the host setting selects the final device anisotropy
    float lod_bias = 0.0f;
    float min_lod = 0.0f;
    float max_lod = 0.0f;

    [[nodiscard]] bool operator==(const TextureSampler&) const = default;
};

// Cache identity preserves the authored floating-point LOD values.
struct SamplerKey {
    std::uint32_t filter = 0;
    float min_lod = 0.0f, max_lod = 0.0f, lod_bias = 0.0f;
    bool operator==(const SamplerKey&) const = default;
};
struct SamplerKeyHasher {
    std::size_t operator()(const SamplerKey& key) const noexcept {
        const auto bits = [](float f) { return f == 0.0f ? 0u : std::bit_cast<std::uint32_t>(f); };
        std::uint64_t h = key.filter;
        h = (h ^ bits(key.min_lod)) * 0x100000001B3ull;
        h = (h ^ bits(key.max_lod)) * 0x100000001B3ull;
        return static_cast<std::size_t>((h ^ bits(key.lod_bias)) * 0x100000001B3ull);
    }
};
struct SamplerCacheKey {
    SamplerKey sampler;
    std::uint32_t config;
    bool operator==(const SamplerCacheKey&) const = default;
};
struct SamplerCacheKeyHasher {
    std::size_t operator()(const SamplerCacheKey& key) const noexcept {
        return SamplerKeyHasher{}(key.sampler) ^ (std::size_t{key.config} * 0x9E3779B9u);
    }
};

[[nodiscard]] inline std::uint32_t min_filter_code(const TextureSampler& s) noexcept {
    return (s.min_linear ? 4u : 0u) | (s.mip > 2u ? 2u : s.mip);
}

}  // namespace galaxy::render
