// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The sun a galaxy's sky shows. The picture capture adds every fourth sky sample's colour into
// bins by world direction (rt_lighting.hlsl CaptureCS: octahedral, +y the pole); the map keeps
// each bin's colour as the camera turns. The sun is the map's brightest place where it stands
// out from the rest of the sky and no place away from it comes close (white clouds spread over
// the sky do not make one).
//
// No Windows/D3D12 includes (unit-tested offline).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace galaxy::gx {

inline constexpr unsigned kSkyMapSide = 32u;
inline constexpr unsigned kSkyBins = kSkyMapSide * kSkyMapSide;
// Per bin and capture: samples, red, green, blue (sums of 0-255).
inline constexpr unsigned kSkyBinWords = 4u;

[[nodiscard]] inline unsigned sky_bin(const std::array<float, 3>& w) noexcept {
    const float l1 = std::fabs(w[0]) + std::fabs(w[1]) + std::fabs(w[2]);
    float x = l1 > 0.0f ? w[0] / l1 : 0.0f;
    float z = l1 > 0.0f ? w[2] / l1 : 0.0f;
    if (w[1] < 0.0f) {
        const float fx = (1.0f - std::fabs(z)) * (x >= 0.0f ? 1.0f : -1.0f);
        const float fz = (1.0f - std::fabs(x)) * (z >= 0.0f ? 1.0f : -1.0f);
        x = fx;
        z = fz;
    }
    const auto cell = [](float v) {
        return std::min(static_cast<unsigned>(std::max((v * 0.5f + 0.5f) * kSkyMapSide, 0.0f)), kSkyMapSide - 1u);
    };
    return cell(z) * kSkyMapSide + cell(x);
}

[[nodiscard]] inline std::array<float, 3> sky_bin_direction(unsigned bin) noexcept {
    float x = ((static_cast<float>(bin % kSkyMapSide) + 0.5f) / kSkyMapSide) * 2.0f - 1.0f;
    float z = ((static_cast<float>(bin / kSkyMapSide) + 0.5f) / kSkyMapSide) * 2.0f - 1.0f;
    const float y = 1.0f - std::fabs(x) - std::fabs(z);
    if (y < 0.0f) {
        const float fx = (1.0f - std::fabs(z)) * (x >= 0.0f ? 1.0f : -1.0f);
        const float fz = (1.0f - std::fabs(x)) * (z >= 0.0f ? 1.0f : -1.0f);
        x = fx;
        z = fz;
    }
    const float length = std::sqrt(x * x + y * y + z * z);
    return {x / length, y / length, z / length};
}

// Every bin's centre direction.
[[nodiscard]] inline const std::array<std::array<float, 3>, kSkyBins>& sky_bin_directions() noexcept {
    static const std::array<std::array<float, 3>, kSkyBins> table = [] {
        std::array<std::array<float, 3>, kSkyBins> t{};
        for (unsigned b = 0; b < kSkyBins; ++b) {
            t[b] = sky_bin_direction(b);
        }
        return t;
    }();
    return table;
}

struct SkySun {
    bool valid = false;
    std::array<float, 3> direction{};  // world, towards it
    std::array<float, 3> color{};      // peak 1
    float peak = 0.0f;                 // its bins' luminance (0-1)
    float distant = 0.0f;              // the brightest bin 30 degrees or more away
    float rest = 0.0f;                 // the median of those
    unsigned bins = 0;                 // bins seen
};

class SkyMap {
public:
    // Bins seen before a sun is looked for; bins 30 degrees or more from it; how far the sun
    // stands above the rest of the sky (and at least x the rest) and above the brightest bin
    // away from it, and its least luminance (0-1). Measured (smoke\sky2, sky3): a sky's sun (Good
    // Egg, within 5 degrees of its BrightSun) 0.86-0.99 with the brightest bin away 0.50-0.51;
    // skies without one, their brightest bins clouds or horizon haze (Gusty Garden, Honeyhive, the
    // Observatory's horizon) 0.75-0.97 with bins away within 0.08-0.22 of them. A sun stands no
    // lower than kMinHeight (world y of its direction; measured suns -0.04 to 0.02, a white place
    // seen while a stage loaded -0.42).
    static constexpr unsigned kMinBins = 24u;
    static constexpr unsigned kMinDistantBins = 8u;
    static constexpr float kDistantCos = 0.866f;
    static constexpr float kOverRest = 0.12f;
    static constexpr float kOverRestScale = 1.2f;
    static constexpr float kOverDistant = 0.3f;
    static constexpr float kMinPeak = 0.85f;
    static constexpr float kMinHeight = -0.15f;

    void reset() noexcept {
        bins_ = {};
        seen_ = 0u;
    }

    // One capture: kSkyBins x kSkyBinWords. A bin moves a tenth of the way to what it shows now.
    void add(const std::uint32_t* words) noexcept {
        for (unsigned b = 0; b < kSkyBins; ++b) {
            const std::uint32_t* w = words + b * kSkyBinWords;
            if (w[0] == 0u) {
                continue;
            }
            const float scale = 1.0f / (255.0f * static_cast<float>(w[0]));
            const std::array<float, 3> now{static_cast<float>(w[1]) * scale, static_cast<float>(w[2]) * scale,
                                           static_cast<float>(w[3]) * scale};
            Bin& bin = bins_[b];
            if (!bin.seen) {
                bin.color = now;
                bin.seen = true;
                ++seen_;
            } else {
                for (unsigned i = 0; i < 3u; ++i) {
                    bin.color[i] += (now[i] - bin.color[i]) * 0.1f;
                }
            }
        }
    }

    [[nodiscard]] bool seen(unsigned bin) const noexcept { return bins_[bin].seen; }
    [[nodiscard]] const std::array<float, 3>& color(unsigned bin) const noexcept { return bins_[bin].color; }
    [[nodiscard]] float luminance(unsigned bin) const noexcept {
        const auto& c = bins_[bin].color;
        return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
    }

    [[nodiscard]] SkySun find() const {
        SkySun sun{};
        sun.bins = seen_;
        if (seen_ < kMinBins) {
            return sun;
        }
        const auto& directions = sky_bin_directions();
        unsigned peak = 0u;
        float best = -1.0f;
        for (unsigned b = 0; b < kSkyBins; ++b) {
            if (bins_[b].seen && directions[b][1] >= kMinHeight && luminance(b) > best) {
                best = luminance(b);
                peak = b;
            }
        }
        if (best < 0.0f) {
            return sun;
        }
        const std::array<float, 3> p = directions[peak];
        std::vector<float> distant;
        distant.reserve(seen_);
        for (unsigned b = 0; b < kSkyBins; ++b) {
            if (bins_[b].seen && dot(directions[b], p) < kDistantCos) {
                distant.push_back(luminance(b));
            }
        }
        sun.peak = best;
        if (distant.size() < kMinDistantBins) {
            return sun;
        }
        std::nth_element(distant.begin(), distant.begin() + distant.size() / 2u, distant.end());
        sun.rest = distant[distant.size() / 2u];
        sun.distant = *std::max_element(distant.begin(), distant.end());
        if (best < kMinPeak || best < sun.rest + kOverRest || best < sun.rest * kOverRestScale ||
            best < sun.distant + kOverDistant) {
            return sun;
        }
        // Its centre and colour: the bins near the peak weighted by how far they stand above
        // halfway between the rest of the sky and the peak.
        const float floor = 0.5f * (best + sun.rest);
        std::array<float, 3> d{};
        std::array<float, 3> c{};
        float total = 0.0f;
        for (unsigned b = 0; b < kSkyBins; ++b) {
            if (!bins_[b].seen) {
                continue;
            }
            const std::array<float, 3>& w = directions[b];
            const float weight = luminance(b) - floor;
            if (weight <= 0.0f || dot(w, p) < 0.97f) {
                continue;
            }
            for (unsigned i = 0; i < 3u; ++i) {
                d[i] += w[i] * weight;
                c[i] += bins_[b].color[i] * weight;
            }
            total += weight;
        }
        const float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        const float top = std::max({c[0], c[1], c[2]});
        if (!(length > 1e-6f) || !(top > 1e-6f)) {
            return sun;
        }
        sun.valid = true;
        sun.direction = {d[0] / length, d[1] / length, d[2] / length};
        sun.color = {c[0] / top, c[1] / top, c[2] / top};
        return sun;
    }

    // The sun to keep: a new one near the held one replaces it; one elsewhere only where it is
    // brighter than the held one's place now. Without a new one the held one stays.
    void update(SkySun& held) const {
        const SkySun now = find();
        if (!now.valid) {
            return;
        }
        if (!held.valid || dot(now.direction, held.direction) > 0.94f ||
            now.peak > luminance(sky_bin(held.direction)) + 0.03f) {
            held = now;
        }
    }

private:
    struct Bin {
        std::array<float, 3> color{};
        bool seen = false;
    };
    static float dot(const std::array<float, 3>& a, const std::array<float, 3>& b) noexcept {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    }
    std::array<Bin, kSkyBins> bins_{};
    unsigned seen_ = 0u;
};

}  // namespace galaxy::gx
