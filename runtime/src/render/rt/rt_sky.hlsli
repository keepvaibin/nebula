// SPDX-License-Identifier: GPL-3.0-only
// The tracer's sky picture: the sky as the screen draws it, six faces about the eye side by side (rt_walk.cpp
// rt_sky_capture), read where g_sky_faces.x is 1. Its own header: rt_light_space.hlsl reads it for --diagnostics rt-sky
// without the rest of rt_common.hlsli.
#ifndef NEBULA_RT_SKY_HLSLI
#define NEBULA_RT_SKY_HLSLI

#include "rt_frame.hlsli"

Texture2D<float4> g_sky_picture : register(t7);

// The sky as the screen draws it along `dir` (the eye's space), as the screen's values: the face of the sky picture the
// direction leaves the cube through (render_core.h kRtSkyFaces: forward and up, right = forward x up; face n at column
// n % 3, row n / 3), its point there by the face's 90-degree frustum, held half a texel inside the face so that no
// neighbour bleeds into it.
float3 rt_sky_picture_by(SamplerState linear_, float3 dir) {
    const float3 a = abs(dir);
    uint face;
    float3 forward, up;
    if (a.x >= a.y && a.x >= a.z) {
        face = dir.x >= 0.0f ? 0u : 1u;
        forward = float3(dir.x >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f);
        up = float3(0.0f, 1.0f, 0.0f);
    } else if (a.y >= a.z) {
        face = dir.y >= 0.0f ? 2u : 3u;
        forward = float3(0.0f, dir.y >= 0.0f ? 1.0f : -1.0f, 0.0f);
        up = float3(0.0f, 0.0f, dir.y >= 0.0f ? 1.0f : -1.0f);
    } else {
        face = dir.z >= 0.0f ? 4u : 5u;
        forward = float3(0.0f, 0.0f, dir.z >= 0.0f ? 1.0f : -1.0f);
        up = float3(0.0f, 1.0f, 0.0f);
    }
    const float3 right = cross(forward, up);
    const float2 clip = float2(dot(dir, right), dot(dir, up)) / max(dot(dir, forward), 1e-6f);
    uint width, height;
    g_sky_picture.GetDimensions(width, height);
    const float size = float(height) * 0.5f;
    const float2 inside = clamp((float2(0.5f, 0.5f) + float2(0.5f, -0.5f) * clip) * size, 0.5f, size - 0.5f);
    const float2 texel = float2(float(face % 3u), float(face / 3u)) * size + inside;
    return g_sky_picture.SampleLevel(linear_, texel / float2(float(width), float(height)), 0.0f).rgb;
}

#endif
