// SPDX-License-Identifier: GPL-3.0-only
// The reflections over time (the master plan's 12.3). A mirror image does not move with the surface it is seen in:
// it moves as if it stood behind the surface, at the ray's length. So the history is looked for in two places of the
// frame before: (a) where the surface point was (its motion), (b) where the mirror image's own point was (the
// "virtual" point: the surface's place carried on along the view by the ray's length, seen by the frame before's
// camera), as ReBLUR and the stochastic reflection papers do; each counts only on the same object, and the one nearer
// what this frame's rays saw around the pixel is kept (rough surfaces take (a), mirrors (b)). The history is held
// inside that neighbourhood's mean, give or take 1.5 of its spread, and holds 8 frames under roughness 0.05, rising
// to 32 at 0.3.
// Channels: rgb the radiance seen, a the ray's length (0: no reflection here; in R raw -1: a pixel Medium did not
// trace this frame, which keeps its history, held inside what its traced neighbours saw, or takes their mean).
// g_moved (the master plan's 16.4, R): 1 where the reflection's average moved by more than a quarter from its history
// before the clamp, as rt_lights_time.hlsl marks D; rt_reactive.hlsl puts it into the upscaler's reactive mask.
#include "rt_bindings.hlsli"
#include "rt_frame.hlsli"

Texture2D<float4> g_raw : register(t9);
Texture2D<float4> g_history : register(t10);       // the frame before's average
Texture2D<float4> g_guide : register(t11);
Texture2D<float4> g_guide_before : register(t12);
Texture2D<float4> g_motion : register(t13);
Texture2D<float4> g_material : register(t14);
RT_FORMAT(rgba16f) RWTexture2D<float4> g_out : register(u0);
RT_FORMAT(r8) RWTexture2D<unorm float> g_moved : register(u1);

static const float3 kLum = float3(0.2126f, 0.7152f, 0.0722f);

// How many frames a history holds, as a share of what it holds at 60 a second (FA's rt_history_scale, rt_frame.hlsli,
// 16.5: every history lasts the same time at any frame rate); 1 before that is in the frame's constants.
float fb_history_scale() {
#ifdef NEBULA_RT_HISTORY_SCALE
    return rt_history_scale();
#else
    return 1.0f;
#endif
}

// The frame before's average about `centre` (a pixel's middle there), from the taps that showed the same object:
// their weight (0: none).
float history_at(float2 centre, float4 guide, uint width, uint height, out float3 value) {
    value = float3(0.0f, 0.0f, 0.0f);
    const float2 at = centre - 0.5f;
    const float2 corner = floor(at);
    const float2 part = at - corner;
    float weight = 0.0f;
    for (int tap = 0; tap < 4; ++tap) {
        const int2 offset = int2(tap & 1, tap >> 1);
        const int2 from = int2(corner) + offset;
        if (from.x < 0 || from.y < 0 || from.x >= int(width) || from.y >= int(height)) {
            continue;
        }
        const float4 before = g_guide_before.Load(int3(from, 0));
        const float4 history = g_history.Load(int3(from, 0));
        if (before.a == 0.0f || before.g != guide.g || before.b != guide.b || history.a == 0.0f) {
            continue;
        }
        const float w = (offset.x != 0 ? part.x : 1.0f - part.x) * (offset.y != 0 ? part.y : 1.0f - part.y);
        value += history.rgb * w;
        weight += w;
    }
    if (weight > 0.0f) {
        value /= weight;
    }
    return weight;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint width, height;
    g_out.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) {
        return;
    }
    const int2 pixel = int2(id.xy);
    const float4 raw = g_raw.Load(int3(pixel, 0));
    const float4 guide = g_guide.Load(int3(pixel, 0));
    if (raw.a == 0.0f || guide.a == 0.0f) {
        g_out[id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        g_moved[id.xy] = 0.0f;
        return;
    }
    if ((g_more.z & 2048u) != 0u) {  // raw signals for an outside denoiser (22.20): this frame's as traced
        g_out[id.xy] = raw;
        g_moved[id.xy] = 0.0f;
        return;
    }
    // What this frame's rays saw around the pixel on the same object.
    float3 first = float3(0.0f, 0.0f, 0.0f), square = float3(0.0f, 0.0f, 0.0f);
    float taps = 0.0f, length_sum = 0.0f;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            const int2 at = clamp(pixel + int2(x, y), int2(0, 0), int2(width, height) - 1);
            const float4 near = g_guide.Load(int3(at, 0));
            const float4 value = g_raw.Load(int3(at, 0));
            if (near.a != 0.0f && near.g == guide.g && near.b == guide.b && value.a > 0.0f) {
                first += value.rgb;
                square += value.rgb * value.rgb;
                length_sum += value.a;
                taps += 1.0f;
            }
        }
    }
    const float3 mean = first / max(taps, 1.0f);
    const float3 spread = sqrt(max(square / max(taps, 1.0f) - mean * mean, 0.0f));
    const float roughness = g_material.Load(int3(pixel, 0)).r;
    const float frames = lerp(8.0f, 32.0f, saturate((roughness - 0.05f) / 0.25f)) * fb_history_scale();
    const bool traced = raw.a > 0.0f;
    const float ray_length = traced ? raw.a : taps > 0.0f ? length_sum / taps : 1000.0f;
    float3 now = traced ? raw.rgb : mean;
    bool found = false;
    float moved = 0.0f;
    if (g_kept.z != 0u) {
        float3 by_surface = float3(0.0f, 0.0f, 0.0f), by_image = float3(0.0f, 0.0f, 0.0f);
        float surface_weight = 0.0f, image_weight = 0.0f;
        const float4 motion = g_motion.Load(int3(pixel, 0));
        if (motion.z > 0.0f) {
            surface_weight = history_at(float2(pixel) + 0.5f + motion.xy, guide, width, height, by_surface);
        }
        // The mirror image's point: the surface's, carried on along the view by the ray's length.
        const float3 at = rt_eye_ray(g_ray, float2(pixel) + 0.5f) * guide.r;
        const float3 image = at + normalize(at) * min(ray_length, 1000000.0f);
        const float3 was = mul(float3x4(g_before[0], g_before[1], g_before[2]), float4(image, 1.0f));
        if (-was.z > 1e-3f) {
            const float2 centre = float2((was.x / -was.z - g_ray_before.y) / g_ray_before.x, (was.y / -was.z - g_ray_before.w) / g_ray_before.z);
            if (all(centre > -1.0f) && centre.x < float(width) + 1.0f && centre.y < float(height) + 1.0f) {
                image_weight = history_at(centre, guide, width, height, by_image);
            }
        }
        float3 history = float3(0.0f, 0.0f, 0.0f);
        if (surface_weight > 0.01f && image_weight > 0.01f) {
            const float a = abs(dot(by_surface - mean, kLum)), b = abs(dot(by_image - mean, kLum));
            history = a < b ? by_surface : by_image;
            found = true;
        } else if (surface_weight > 0.01f || image_weight > 0.01f) {
            history = surface_weight > 0.01f ? by_surface : by_image;
            found = true;
        }
        const float history_before = dot(history, kLum);  // the change is the averages', the history before its clamp
        if (found && traced) {
            history = clamp(history, mean - 1.5f * spread - 0.01f, mean + 1.5f * spread + 0.01f);
            now = lerp(history, raw.rgb, 1.0f / frames);
        } else if (found) {
            now = taps > 0.0f ? clamp(history, mean - 2.5f * spread - 0.01f, mean + 2.5f * spread + 0.01f) : history;
        }
        moved = found && abs(dot(now, kLum) - history_before) > 0.25f ? 1.0f : 0.0f;
    }
    if (!traced && !found && !(taps > 0.0f)) {
        g_out[id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);  // nothing seen near yet
        g_moved[id.xy] = 0.0f;
        return;
    }
    g_out[id.xy] = float4(now, ray_length);
    g_moved[id.xy] = moved;
}
