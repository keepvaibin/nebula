// SPDX-License-Identifier: GPL-3.0-only
// The sun filter's second pass (plan 7.3): V averaged with what the same surface point had in the frames before,
// found by where it was (S.motion) and taken only where the pixel there was the same object at the same depth. At
// most 16 frames: a shadow must follow its moving caster (the lights keep 32). The history is held inside what this
// frame's rays saw around the pixel (their mean, give or take 1.25 of their spread, over five by five pixels of the
// same object), so a caster that moves leaves no trail. The occluder's distance is the nearest of the taps that have
// one (each ray reports the first caster its walk met, not the nearest: B3), averaged over time: the blur after takes
// the penumbra's width from it. Outside the penumbra tiles
// (rt_sun_tiles) V is copied through.
#include "rt_bindings.hlsli"
#include "rt_frame.hlsli"

Texture2D<float4> g_raw : register(t9);             // rgb V, a the occluder's distance (kNoOccluder: none)
Texture2D<float4> g_history : register(t10);        // the frame before's averages: rgb V, a frames / 16
Texture2D<float4> g_guide : register(t11);
Texture2D<float4> g_guide_before : register(t12);
Texture2D<float4> g_motion : register(t13);         // xy the move in pixels, z the distance before (0: not known)
Texture2D<float> g_tiles : register(t14);
Texture2D<float> g_distance_before : register(t15); // the frame before's averaged distance (0: none)
RT_FORMAT(rgba16f) RWTexture2D<float4> g_out : register(u0);
RT_FORMAT(r16f) RWTexture2D<float> g_distance : register(u1);


bool in_penumbra(uint2 pixel) {
    uint across, down;
    g_tiles.GetDimensions(across, down);
    const int2 tile = int2(pixel / 16u);
    float most = 0.0f;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            const int2 at = clamp(tile + int2(x, y), int2(0, 0), int2(across, down) - 1);
            most = max(most, g_tiles.Load(int3(at, 0)));
        }
    }
    return most > 0.75f;
}

// The most frames a history holds: 16 at 60 frames a second, as many seconds at any rate (plan 16.5).
float most_frames() {
    return 16.0f * rt_history_scale();
}

bool same_surface(float4 guide, float4 before, float away) {
    return before.a != 0.0f && before.g == guide.g && before.b == guide.b && abs(before.r - away) < 0.02f * away + 1.0f;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint width, height;
    g_out.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) {
        return;
    }
    const int2 pixel = int2(id.xy);
    const float4 guide = g_guide.Load(int3(pixel, 0));
    const float4 raw = g_raw.Load(int3(pixel, 0));
    const float own_distance = raw.a < kNoOccluder ? raw.a : 0.0f;
    if (guide.a == 0.0f || !in_penumbra(id.xy) || (g_more.z & 2048u) != 0u) {  // raw signals (FB, 22.20): copied through
        g_out[id.xy] = float4(raw.rgb, 1.0f);
        g_distance[id.xy] = own_distance;
        return;
    }
    // This frame's rays around the pixel, on the same object.
    float3 first = float3(0.0f, 0.0f, 0.0f), square = float3(0.0f, 0.0f, 0.0f);
    float taps = 0.0f, nearest = kNoOccluder, hits = 0.0f;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            const int2 at = clamp(pixel + int2(x, y), int2(0, 0), int2(width, height) - 1);
            const float4 near = g_guide.Load(int3(at, 0));
            if (near.a != 0.0f && near.g == guide.g && near.b == guide.b) {
                const float4 value = g_raw.Load(int3(at, 0));
                first += value.rgb;
                square += value.rgb * value.rgb;
                taps += 1.0f;
                if (value.a < kNoOccluder) {
                    nearest = min(nearest, value.a);
                    hits += 1.0f;
                }
            }
        }
    }
    const float3 mean = first / max(taps, 1.0f);
    const float3 spread = sqrt(max(square / max(taps, 1.0f) - mean * mean, 0.0f));
    const float3 low = min(raw.rgb, mean - 1.25f * spread - 0.02f), high = max(raw.rgb, mean + 1.25f * spread + 0.02f);
    const float distance_now = hits > 0.0f ? nearest : 0.0f;
    float3 sum = float3(0.0f, 0.0f, 0.0f);
    float frames = 0.0f, weight = 0.0f, distance_was = 0.0f, distance_weight = 0.0f;
    if (rt_history()) {
        const float4 motion = g_motion.Load(int3(pixel, 0));
        if (motion.z > 0.0f) {
            const float2 at = float2(id.xy) + motion.xy;
            const float2 corner = floor(at);
            const float2 part = at - corner;
            for (int tap = 0; tap < 4; ++tap) {
                const int2 offset = int2(tap & 1, tap >> 1);
                const int2 from = int2(corner) + offset;
                if (from.x < 0 || from.y < 0 || from.x >= int(width) || from.y >= int(height)) {
                    continue;
                }
                if (!same_surface(guide, g_guide_before.Load(int3(from, 0)), motion.z)) {
                    continue;
                }
                const float w = (offset.x != 0 ? part.x : 1.0f - part.x) * (offset.y != 0 ? part.y : 1.0f - part.y);
                const float4 history = g_history.Load(int3(from, 0));
                sum += history.rgb * w;
                frames += history.a * w;
                weight += w;
                const float was = g_distance_before.Load(int3(from, 0));
                if (was > 0.0f) {
                    distance_was += was * w;
                    distance_weight += w;
                }
            }
        }
    }
    if (weight < 0.01f) {
        g_out[id.xy] = float4(raw.rgb, 1.0f / most_frames());
        g_distance[id.xy] = distance_now;
        return;
    }
    const float count = min(frames / weight * most_frames() + 1.0f, most_frames());
    const float share = max(1.0f / count, 1.0f / most_frames());
    const float3 now = lerp(clamp(sum / weight, low, high), raw.rgb, share);
    g_out[id.xy] = float4(now, count / most_frames());
    const float before = distance_weight > 0.01f ? distance_was / distance_weight : 0.0f;
    g_distance[id.xy] = before > 0.0f && distance_now > 0.0f ? lerp(before, distance_now, max(share, 0.2f)) : max(before, distance_now);
}
