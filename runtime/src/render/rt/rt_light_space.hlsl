// SPDX-License-Identifier: GPL-3.0-only
// The light pass's shares over space: what the average over time (rt_light_time.hlsl) leaves of the noise is spread
// over the pixel's neighbours (an a-trous step: 5 by 5 taps `step` pixels apart; run with steps 1, 2 and 4). A
// neighbour counts by how surely it is the same surface and the same light:
//   the same object; in the pixel's own plane (its place against the pixel's normal); facing the same way;
//   and, a channel at a time, a share no farther from the pixel's than its noise explains. One ray a frame is a
//   coin toss, so a pixel with n frames behind it is uncertain by about 0.5 / sqrt(n): with few frames (a surface
//   just come into view, a moving thing) everything near is taken; with many, only the like, and a sharp edge
//   between light and shadow stays where it is.
// Channels (rt_light.hlsl): r V, b how the surface faces the sun, a the sky's share (spread here; r and b go
// through), g the frames averaged (over 32). The last step writes what the programs read: rgb V (the sun's filtered
// V, t14), a the sky's share (with --diagnostics rt-motion: the point's motion instead, from S.motion at t12).
// Compiled a second time as rt_lights_space.hlsl (RT_LIGHTS) for the local lights' share (red, green, blue in r, b,
// a): there a neighbour's light counts by how far its luminance is from the pixel's against the noise the pixel is
// known to have (the variance rt_lights_time.hlsl keeps, smoothed over three by three pixels, halved in reach by
// each step as the noise itself is: the weight of Schied et al. 2017), and the last step writes red, green, blue as
// the programs read them.
#include "rt_bindings.hlsli"
#include "rt_frame.hlsli"

Texture2D<float4> g_in : register(t9);
Texture2D<float4> g_guide : register(t11);  // eye distance, the object's slot in two parts, the normal packed (0: no surface)
RT_FORMAT(rgba16f) RWTexture2D<float4> g_out : register(u0);
#ifndef RT_LIGHTS
Texture2D<float4> g_motion_map : register(t12);  // the last step's, for --diagnostics rt-motion
Texture2D<float4> g_sun_out : register(t14);     // the last step's: the sun's V, filtered (rgb)
Texture2D<float4> g_indirect : register(t13);    // the last step's: I (rt_indirect_up.hlsl), when bit 8 of g_more.z says
#endif
#ifdef RT_LIGHTS
Texture2D<float4> g_moments : register(t10);  // b: the variance of the average's luminance
Texture2D<float4> g_body : register(t12);     // the second layer's last step: glass's and water's body, in a (FC's c16)
static const float3 kLuminance = float3(0.2126f, 0.7152f, 0.0722f);
#endif

// The step (b1, rt_frame.hlsli): g_pass.y pixels between taps; g_pass.z 1 the last step.

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
    const float frames = g_pass.z != 0u ? 1.0f : centre.g;
    if (guide.a == 0.0f) {
#ifdef RT_LIGHTS
        if (rt_second_layer()) {
            return;  // the added light there is the opaque layer's
        }
        g_out[id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
#else
        g_out[id.xy] = float4(1.0f, frames, 1.0f, 1.0f);
#endif
        return;
    }
    static const float kTap[3] = {6.0f / 16.0f, 4.0f / 16.0f, 1.0f / 16.0f};
    const int step_ = int(g_pass.y);
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
    const float unsure = sqrt(max(variance / max(counted, 1e-4f), 0.0f)) / float(g_pass.y);
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
            if ((x == 0 && y == 0) || (g_more.z & 2048u) != 0u) {  // raw signals (FB, 22.20): the centre tap alone
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
    const float body = g_pass.z != 0u && rt_second_layer() ? g_body.Load(int3(pixel, 0)).r : 1.0f;
    g_out[id.xy] = g_pass.z != 0u ? float4(now.x, now.y, now.z, body) : float4(now.x, frames, now.y, now.z);
#else
    if (g_pass.z == 0u) {
        g_out[id.xy] = float4(centre.r, frames, centre.b, now.z);
        return;
    }
    // What the programs read: V as a colour and the sky's share.
    // O: the sky and bounce light's mean ratio where that pass runs (bit 8), else the sky rays' share.
    float sky = now.z;
    if ((g_more.z & 256u) != 0u) {
        const float3 ratio = g_indirect.Load(int3(pixel, 0)).rgb;
        sky = (ratio.r + ratio.g + ratio.b) * (1.0f / 3.0f);
    }
    float4 shares = float4(g_sun_out.Load(int3(pixel, 0)).rgb, sky);
    if ((g_more.z & 128u) != 0u) {
        shares = float4(0.0f, 0.0f, 0.0f, 0.0f);  // the emitters alone (--diagnostics rt-added)
    }
    if ((g_more.z & 512u) != 0u) {
        // --diagnostics rt-motion (shown as rt-light shows r, a, b): the move across in red, down in green, eight
        // pixels a frame full; blue where where the point was is not known.
        const float4 m = g_motion_map.Load(int3(pixel, 0));
        shares = float4(saturate(abs(m.x) / 8.0f), 1.0f, m.z > 0.0f ? 0.0f : 1.0f, saturate(abs(m.y) / 8.0f));
    }
    g_out[id.xy] = shares;
#endif
}
