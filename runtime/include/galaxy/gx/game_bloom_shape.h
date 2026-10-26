// SPDX-License-Identifier: GPL-3.0-only
// The game's bloom chain at the internal resolution (game_bloom.h): its resolutions and the widths of the Gaussians
// standing for the game's rings, shared by the Direct3D 12 and Vulkan renderers.
#pragma once

#include <algorithm>
#include <cmath>

namespace galaxy::gx::bloom {

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

// The chain's resolutions: level 1 one texel per f x f EFB pixels (a quarter of the internal resolution up to 8x, the
// frame's 456 lines at most twice over above it), level 2 one per n x n of those (half, at most the game's half: 228
// lines). The selection's box: the game's 4 x 4 pixels.
struct Shape {
    unsigned f = 4;
    unsigned w1 = 0, h1 = 0;
    unsigned n = 2;
    unsigned w2 = 0, h2 = 0;
    float box = 1.0f;
};

inline Shape shape_of(unsigned frame_width, unsigned frame_height, unsigned logical_height, unsigned scale) {
    Shape s;
    scale = std::max(scale, 1u);
    s.f = scale <= 8u ? 4u : (scale + 1u) / 2u;
    s.w1 = (frame_width + s.f - 1u) / s.f;
    s.h1 = (frame_height + s.f - 1u) / s.f;
    const unsigned cap = std::max(logical_height / 2u, 1u);
    s.n = std::max(2u, (s.h1 + cap - 1u) / cap);
    s.w2 = (s.w1 + s.n - 1u) / s.n;
    s.h2 = (s.h1 + s.n - 1u) / s.n;
    s.box = 4.0f * static_cast<float>(scale) / static_cast<float>(s.f);
    return s;
}

// The Gaussians' widths (texels of their level, across and down): each ring's spread plus what the game's resampling
// adds at its resolution (its taps' bilinear footprints, its eighth's 2 x 2 box, its upsamples), less what this chain's
// own resampling adds. A ring of radius r spreads r^2 / 2 along each axis. Normal: level 1's two rings and level 2's
// three; simple: level 1's one.
struct Widths {
    float x1[3]{}, y1[3]{}, x2[3]{}, y2[3]{};
};

inline Widths widths_of(bool normal, const Shape& s, unsigned frame_width, unsigned frame_height,
                        unsigned logical_width, unsigned logical_height, float ring_x, float ring_y) {
    const float lw = static_cast<float>(std::max(logical_width, 1u));
    const float lh = static_cast<float>(std::max(logical_height, 1u));
    const float qx = 4.0f / lw, qy = 4.0f / lh;  // the game's quarter texel, in frame widths and heights
    const float ex = 8.0f / lw, ey = 8.0f / lh;  // its eighth
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

}  // namespace galaxy::gx::bloom
