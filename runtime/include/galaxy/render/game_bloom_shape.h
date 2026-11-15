// SPDX-License-Identifier: GPL-3.0-only
// The game's bloom chain at the internal resolution (game_bloom.h): its resolutions and the widths of the Gaussians
// standing for the game's rings, shared by the Direct3D 12 and Vulkan renderers.
#pragma once

#include "galaxy/render/resolution_governor.h"

#include <algorithm>
#include <cmath>

namespace galaxy::render::bloom {

// BloomEffect's rings (sL1RadAndOfs, sL2RadAndOfs: radii in frame heights, taps per ring) and BloomEffectSimple's one
// (ImageEffectLocalUtil::blurTexture: 8 taps, radius 0.009).
inline constexpr float kLevel1Radii[] = {0.01f, 0.02f};
inline constexpr float kLevel2Radii[] = {0.04f, 0.07f, 0.09f};
inline constexpr float kLevel1Taps = 6.0f;
inline constexpr float kLevel2Taps = 12.0f;
inline constexpr float kSimpleRadius = 0.009f;
inline constexpr float kSimpleTaps = 8.0f;

// Spreads (variance along an axis, in that axis's frame units) of the game's resampling: a bilinear tap or upsample at
// texel size d, and a box of m texels of size d.
inline float tent(float d) {
    return d * d / 6.0f;
}

inline float box(float m, float d) {
    return (m * m - 1.0f) / 12.0f * d * d;
}

// A Gaussian's width in texels of size d for a spread; at least a quarter texel.
inline float sigma(float variance, float d) {
    return std::max(std::sqrt(std::max(variance, 0.0f)) / d, 0.25f);
}

// The chain's resolutions: level 1 one texel per f x f scene target pixels (a quarter of the internal resolution up to 8x, the
// frame's height at most twice over above it), level 2 one per n x n of those (half, at most the game's half: 228
// lines). The selection's box: the game's 4 x 4 pixels.
struct Shape {
    unsigned f = 4;
    unsigned w1 = 0, h1 = 0;
    unsigned n = 2;
    unsigned w2 = 0, h2 = 0;
    float box = 1.0f;
};

inline Shape shape_of(unsigned frame_width, unsigned frame_height, float scale) {
    Shape s;
    scale = std::max(scale, 1.0f);
    s.f = scale <= 8.0f ? 4u : (static_cast<unsigned>(std::ceil(scale)) + 1u) / 2u;
    s.w1 = (frame_width + s.f - 1u) / s.f;
    s.h1 = (frame_height + s.f - 1u) / s.f;
    constexpr unsigned cap = 228u;
    s.n = std::max(2u, (s.h1 + cap - 1u) / cap);
    s.w2 = (s.w1 + s.n - 1u) / s.n;
    s.h2 = (s.h1 + s.n - 1u) / s.n;
    s.box = 4.0f * scale / static_cast<float>(s.f);
    return s;
}

// The largest of each of a shape's sizes over the dynamic resolution steps of an `extent_width` x `extent_height` render
// size down to `lowest_sixteenths` (16: the render size alone), each step's scale its height over `lines` (the
// renderers' bloom scale): the chain's images are made at these once and each step works in its part of them.
struct Capacity {
    unsigned w1 = 0, h1 = 0, w2 = 0, h2 = 0;
};

inline Capacity largest_shape(unsigned extent_width, unsigned extent_height, double lines, unsigned lowest_sixteenths) {
    Capacity c;
    for (const unsigned sixteenths : kResolutionSixteenths) {
        if (sixteenths < lowest_sixteenths && sixteenths != 16u) {
            break;
        }
        const auto [width, height] = resolution_rectangle(extent_width, extent_height, sixteenths);
        const Shape s = shape_of(width, height, static_cast<float>(height / lines));
        c.w1 = std::max(c.w1, s.w1);
        c.h1 = std::max(c.h1, s.h1);
        c.w2 = std::max(c.w2, s.w2);
        c.h2 = std::max(c.h2, s.h2);
    }
    return c;
}

// The composite's constants (game_bloom.hlsl CompositePS): frame pixel to each level's UV for images made at
// `capacity` (uv: level 1 x, y, level 2 x, y), and the UV of each level's last drawn texel centre (limit, same order),
// where every sample is held.
struct CompositeUv {
    float uv[4]{};
    float limit[4]{};
};

inline CompositeUv composite_uv(const Shape& s, const Capacity& capacity) {
    const float cw1 = static_cast<float>(std::max(capacity.w1, 1u)), ch1 = static_cast<float>(std::max(capacity.h1, 1u));
    const float cw2 = static_cast<float>(std::max(capacity.w2, 1u)), ch2 = static_cast<float>(std::max(capacity.h2, 1u));
    CompositeUv out;
    out.uv[0] = 1.0f / (static_cast<float>(s.f) * cw1);
    out.uv[1] = 1.0f / (static_cast<float>(s.f) * ch1);
    out.uv[2] = 1.0f / (static_cast<float>(s.f * s.n) * cw2);
    out.uv[3] = 1.0f / (static_cast<float>(s.f * s.n) * ch2);
    out.limit[0] = (static_cast<float>(s.w1) - 0.5f) / cw1;
    out.limit[1] = (static_cast<float>(s.h1) - 0.5f) / ch1;
    out.limit[2] = (static_cast<float>(std::max(s.w2, 1u)) - 0.5f) / cw2;
    out.limit[3] = (static_cast<float>(std::max(s.h2, 1u)) - 0.5f) / ch2;
    return out;
}

// The Gaussians' widths (texels of their level, across and down): each ring's spread plus what the game's resampling
// adds at its resolution (its taps' bilinear footprints, its eighth's 2 x 2 box, its upsamples), less what this chain's
// own resampling adds. A ring of radius r spreads r^2 / 2 along each axis. Normal: level 1's two rings and level 2's
// three; simple: level 1's one.
struct Widths {
    float x1[3]{}, y1[3]{}, x2[3]{}, y2[3]{};
};

inline Widths widths_of(bool normal, const Shape& s, unsigned frame_width, unsigned frame_height, float ring_x,
                        float ring_y) {
    constexpr float qx = 1.0f / 160.0f, qy = 1.0f / 114.0f;
    constexpr float ex = 1.0f / 80.0f, ey = 1.0f / 57.0f;
    const float d1x = static_cast<float>(s.f) / static_cast<float>(std::max(frame_width, 1u));
    const float d1y = static_cast<float>(s.f) / static_cast<float>(std::max(frame_height, 1u));
    const float d2x = d1x * static_cast<float>(s.n), d2y = d1y * static_cast<float>(s.n);
    const float m = static_cast<float>(s.n);
    const auto spread = [](float r, float axis) { return 0.5f * (r * axis) * (r * axis); };
    Widths w;
    if (normal) {
        for (unsigned i = 0; i < 2u; ++i) {
            const float r = kLevel1Radii[i];
            w.x1[i] = sigma(spread(r, ring_x) + 2.0f * tent(qx) - tent(d1x), d1x);
            w.y1[i] = sigma(spread(r, ring_y) + 2.0f * tent(qy) - tent(d1y), d1y);
        }
        for (unsigned i = 0; i < 3u; ++i) {
            const float r = kLevel2Radii[i];
            w.x2[i] = sigma(spread(r, ring_x) + box(2.0f, qx) + 2.0f * tent(ex) + tent(d1x) - box(m, d1x) - tent(d2x),
                            d2x);
            w.y2[i] = sigma(spread(r, ring_y) + box(2.0f, qy) + 2.0f * tent(ey) + tent(d1y) - box(m, d1y) - tent(d2y),
                            d2y);
        }
    } else {
        w.x1[0] = sigma(spread(kSimpleRadius, ring_x) + box(2.0f, qx) + 2.0f * tent(ex) - tent(d1x), d1x);
        w.y1[0] = sigma(spread(kSimpleRadius, ring_y) + box(2.0f, qy) + 2.0f * tent(ey) - tent(d1y), d1y);
    }
    return w;
}

// The selection's largest 8-bit value (game_bloom.hlsl BoxCS): DownCS clamps every texel to 1 before its box, the wide
// scene's too, and the boxes after it average, so the selection reads colours of at most 1; the intensity of white,
// 220 * 255 / 256 + 16 = 235.14, rounds to 235; a channel's to 255.
inline constexpr unsigned kLargestIntensity = 235u;
inline constexpr unsigned kLargestChannel = 255u;
static_assert(static_cast<unsigned>((66.0 + 129.0 + 25.0) * (255.0 / 256.0) + 16.0 + 0.5) == kLargestIntensity);

// The chain adds exactly zero to every pixel: no intensity or strength, no weight on level 1 (the normal bloom's level 2
// is made from it), or a threshold no selection passes (it keeps a value above the threshold). The composite then writes
// the scene back unchanged: its colour is the scene plus a bloom term of +0 (no clamp or scale of the sum), its alpha the
// scene's. `channel` as the selection reads it: 0 (the normal bloom's) the intensity, else a channel.
inline bool adds_nothing(unsigned threshold, unsigned channel, unsigned tap1, unsigned intensity, float strength) {
    return intensity == 0u || !(strength > 0.0f) || tap1 == 0u ||
           threshold >= (channel == 0u ? kLargestIntensity : kLargestChannel);
}

}  // namespace galaxy::render::bloom
