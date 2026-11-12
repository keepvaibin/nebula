// SPDX-License-Identifier: GPL-3.0-only
// Where every random number of the tracer comes from (plan section 6.3): a 128 x 128 blue-noise texture of four
// independent channels (void and cluster, Ulichney 1993: tools/dev/make_blue_noise.py), so a pass's error is spread
// as fine grain the filters take out, shifted each frame by the R2 sequence (Roberts 2018), so the frames' errors
// differ and an average over time converges. A use takes its own place in the texture (a fixed offset) so two uses
// at one pixel do not move together. rt_hash stays for integer picks that need many numbers (a reservoir's choice).
#ifndef NEBULA_RT_SAMPLE_HLSLI
#define NEBULA_RT_SAMPLE_HLSLI

#include "rt_frame.hlsli"

Texture2D<float4> g_blue_noise : register(t20);  // bound by every pass that samples (rt_walk.cpp rt_pass_binds)

// The uses. Add one here (and an offset below) for a new kind of number; a use from 16 up takes the texel's second
// pair (zw) at the same place as use - 16.
static const uint kSampleSun = 0u;          // a point of the sun's disc
static const uint kSampleSunPixel = 1u;     // the place within the pixel a sun ray leaves from
// 2: retired (the light pass's sky rays, 22.10)
static const uint kSampleLightTry = 3u;     // the local lights' candidates (x) and the neighbours' turn (y)
static const uint kSampleLightNear = 4u;    // the frame before's neighbour reservoirs (xy one, zw the other)
static const uint kSampleBlur = 5u;         // the turn of the sun filter's disc
static const uint kSampleIndirect = 6u;     // the sky and bounce light's direction
static const uint kSampleReflect = 7u;      // a reflection's lobe
static const uint kSampleVolume = 8u;       // the light volume's place along a ray
static const uint kSampleLightDisc = 9u;    // the point of a large light's disc its shadow ray aims at
static const uint kSampleNear = 10u;        // the near field's directions (rt_near.hlsl; FB b76)
static const uint kSampleSecond = 16u;      // added to a use: the second pair
static const uint2 kSampleOffsets[16] = {
    uint2(0u, 0u), uint2(61u, 37u), uint2(23u, 101u), uint2(89u, 13u), uint2(47u, 71u), uint2(113u, 59u),
    uint2(7u, 83u), uint2(97u, 109u), uint2(31u, 19u), uint2(67u, 3u), uint2(11u, 47u), uint2(79u, 91u),
    uint2(53u, 29u), uint2(103u, 67u), uint2(17u, 117u), uint2(41u, 53u)};

// Two numbers in [0, 1) for pixel `pixel`, frame `frame`, use `use` (both layers: each has its filter after it).
float2 rt_sample(uint2 pixel, uint frame, uint use) {
    const float4 texel = g_blue_noise.Load(int3((pixel + kSampleOffsets[use & 15u]) & 127u, 0));
    const float2 base = ((use & kSampleSecond) != 0u ? texel.zw : texel.xy) * (255.0f / 256.0f) + 0.5f / 256.0f;
    // R2: frame * (0.7548776662, 0.5698402910) in 32-bit fixed point, exact for any frame number.
    const uint2 shift = uint2(frame * 3242174889u, frame * 2447445414u) >> 8u;
    return frac(base + float2(shift) * (1.0f / 16777216.0f));
}

#endif
