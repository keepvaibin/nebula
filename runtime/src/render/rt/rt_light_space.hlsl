// SPDX-License-Identifier: GPL-3.0-only
// The light pass's shares over space: what the average over time (rt_light_time.hlsl) leaves of the noise is spread
// over the pixel's neighbours (an a-trous step: 5 by 5 taps `step` pixels apart; run with steps 1, 2 and 4). A
// neighbour counts by how surely it is the same surface and the same light:
//   the same object; in the pixel's own plane (its place against the pixel's normal); facing the same way;
//   and, a channel at a time, a share no farther from the pixel's than its noise explains. One ray a frame is a
//   coin toss, so a pixel with n frames behind it is uncertain by about 0.5 / sqrt(n): with few frames (a surface
//   just come into view, a moving thing) everything near is taken; with many, only the like, and a sharp edge
//   between light and shadow stays where it is.
// Channels: r the sun's share, b the lamp's, a the ground's, g the frames averaged (over 32); the last step writes g
// as 1, the fill light's share the programs read there.
// Compiled a second time as rt_lights_space.hlsl (RT_LIGHTS) for the local lights' share (red, green, blue in r, b,
// a): there a neighbour's light counts by how far its luminance is from the pixel's against the noise the pixel is
// known to have (the variance rt_lights_time.hlsl keeps, smoothed over three by three pixels, halved in reach by
// each step as the noise itself is: the weight of Schied et al. 2017), and the last step writes red, green, blue as
// the programs read them.
#include "rt_bindings.hlsli"

Texture2D<float4> g_in : register(t9);
Texture2D<float4> g_guide : register(t11);  // eye distance, the object's slot in two parts, the normal packed (0: no surface)
RT_FORMAT(rgba16f) RWTexture2D<float4> g_out : register(u0);
#ifdef RT_LIGHTS
Texture2D<float4> g_moments : register(t10);  // b: the variance of the average's luminance
static const float3 kLuminance = float3(0.2126f, 0.7152f, 0.0722f);
#endif

cbuffer SpaceConstants : register(b0) {
    uint4 g_step;  // x: pixels between taps; y 1: the last step
    float4 g_ray;  // rt_eye_ray
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint width, height;
    g_out.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) {
        return;
    }
    const int2 pixel = int2(id.xy);
    const float4 centre = g_in.Load(int3(pixel, 0));
    const float4 guide = g_guide.Load(int3(pixel, 0));
    const float frames = g_step.y != 0u ? 1.0f : centre.g;
    if (guide.a == 0.0f) {
#ifdef RT_LIGHTS
        g_out[id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
#else
        g_out[id.xy] = float4(1.0f, frames, 1.0f, 1.0f);
#endif
        return;
    }
    static const float kTap[3] = {6.0f / 16.0f, 4.0f / 16.0f, 1.0f / 16.0f};
    const int step_ = int(g_step.x);
    const float3 normal = rt_unpack_normal(guide.a);
    const float3 place = rt_eye_ray(g_ray, float2(pixel) + 0.5f) * guide.r;
    // How far a neighbour may stand off the pixel's plane: a pixel's width there, and the depth's own steps.
    const float off_plane = 1.5f * abs(g_ray.x) * guide.r + 0.002f * guide.r + 0.25f;
#ifdef RT_LIGHTS
    // The pixel's noise: its variance and its neighbours' of the same object, three by three.
    static const float kNear[2] = {0.5f, 0.25f};
    float variance = 0.0f, counted = 0.0f;
    for (int vy = -1; vy <= 1; ++vy) {
        for (int vx = -1; vx <= 1; ++vx) {
            const int2 at = clamp(pixel + int2(vx, vy), int2(0, 0), int2(width, height) - 1);
            const float4 near = g_guide.Load(int3(at, 0));
            if (near.a != 0.0f && near.g == guide.g && near.b == guide.b) {
                const float w = kNear[abs(vx)] * kNear[abs(vy)];
                variance += g_moments.Load(int3(at, 0)).b * w;
                counted += w;
            }
        }
    }
    const float unsure = sqrt(max(variance / max(counted, 1e-4f), 0.0f)) / float(g_step.x);
    const float like = 1.0f / (4.0f * unsure + 0.004f);
    const float bright = dot(centre.rba, kLuminance);
#else
    // The pixel's uncertainty, from the frames behind it (centre.g is frames over 32).
    const float unsure = 0.5f * rsqrt(max(centre.g * 32.0f, 1.0f));
    const float3 like = 1.0f / (2.0f * unsure + 0.02f);
#endif
    float3 sum = centre.rba * (kTap[0] * kTap[0]);
    float3 weight = float3(1.0f, 1.0f, 1.0f) * (kTap[0] * kTap[0]);
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            if (x == 0 && y == 0) {
                continue;
            }
            const int2 at = pixel + int2(x, y) * step_;
            if (at.x < 0 || at.y < 0 || at.x >= int(width) || at.y >= int(height)) {
                continue;
            }
            const float4 near = g_guide.Load(int3(at, 0));
            if (near.a == 0.0f || near.g != guide.g || near.b != guide.b) {
                continue;
            }
            const float3 there = rt_eye_ray(g_ray, float2(at) + 0.5f) * near.r;
            const float apart = abs(dot(normal, there - place));
            const float facing = saturate(dot(normal, rt_unpack_normal(near.a)));
            const float same = saturate(1.0f - apart / (off_plane * float(step_))) * facing * facing * facing * facing;
            if (same <= 0.0f) {
                continue;
            }
            const float3 value = g_in.Load(int3(at, 0)).rba;
#ifdef RT_LIGHTS
            const float3 w = float3(1.0f, 1.0f, 1.0f) * (kTap[abs(x)] * kTap[abs(y)] * same * exp(-abs(dot(value, kLuminance) - bright) * like));
#else
            const float3 w = kTap[abs(x)] * kTap[abs(y)] * same * exp(-abs(value - centre.rba) * like);
#endif
            sum += value * w;
            weight += w;
        }
    }
    const float3 now = sum / weight;
#ifdef RT_LIGHTS
    g_out[id.xy] = g_step.y != 0u ? float4(now.x, now.y, now.z, 1.0f) : float4(now.x, frames, now.y, now.z);
#else
    g_out[id.xy] = float4(now.x, frames, now.y, now.z);
#endif
}
