// SPDX-License-Identifier: GPL-3.0-only
// How much of a bright object shows, for the lens flare (Game/Screen/LensFlare.cpp, LensFlareDirector::drawSight): drawn
// into a 1 by 1 target over the frame's depth (TEX(0)) after the scene. A sample is seen where nothing drawn stands in
// front of its depth, the game's depth being one minus the texel's as the buffer is reversed (frame_depth.hlsl). The share
// seen is the count over all 17 samples, an off-screen one never seen; the target holds the last frame's share (TEX(1),
// params.x 1) moved towards it by at most params.y, or 0 moved so when there is none. Loaded, not sampled: not every
// device filters a depth format.
struct MaterialConstants {
    float4 params;       // x 1 when TEX(1) holds the last frame's share, y the most it moves in a frame
    float4 samples[17];  // xy the place in the frame (0 to 1, y down), z the depth just in front of the object, w 1 on the screen
};

#define NATIVE_COORDS 1
#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.uv[0] = float4(v.uv[0], 0.0, 0.0);
}

float4 material_color(NativeVaryings i) {
    uint width, height;
    TEX(0).GetDimensions(width, height);
    const int2 last = int2(width, height) - 1;
    float seen = 0.0;
    [unroll] for (uint n = 0u; n < 17u; ++n) {
        const float4 s = material.samples[n];
        if (s.w != 0.0) {
            const int2 p = clamp(int2(floor(s.xy * float2(width, height))), int2(0, 0), last);
            seen += 1.0 - TEX(0).Load(int3(p, 0)).r >= s.z ? 1.0 : 0.0;
        }
    }
    const float share = seen / 17.0;
    const float before = material.params.x != 0.0 ? TEX(1).Load(int3(0, 0, 0)).r : 0.0;
    const float now = before + clamp(share - before, -material.params.y, material.params.y);
    return float4(now, now, now, 1.0);
}
