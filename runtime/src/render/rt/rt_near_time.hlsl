// SPDX-License-Identifier: GPL-3.0-only
// The near field's filter over time (FB b76): V_near averaged with what the same surface point had in the frames
// before, found by where it was (S.motion) and taken only where the pixel there was the same object at the same depth,
// at most 16 frames (a contact follows its caster as a shadow does), the history held inside what this frame's rays
// saw over five by five pixels of the same object (their mean give or take 1.25 of their spread: rt_sun_time.hlsl's
// rule, so a caster that moves leaves no trail). rt_indirect_up.hlsl averages it over three by three pixels.
#include "rt_bindings.hlsli"
#include "rt_frame.hlsli"

Texture2D<float> g_raw : register(t9);             // V_near raw (rt_near.hlsl)
Texture2D<float2> g_history : register(t10);       // the frame before's: x V_near, y frames / most
Texture2D<float4> g_guide : register(t11);
Texture2D<float4> g_guide_before : register(t12);
Texture2D<float4> g_motion : register(t13);        // xy the move in pixels, z the distance before (0: not known)
RT_FORMAT(rg16f) RWTexture2D<float2> g_out : register(u0);
// Pass constants: g_pass.w 1: the near field ran the frame before (its history images hold it; b76c).

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
    const float raw = g_raw.Load(int3(pixel, 0));
    if (guide.a == 0.0f || (g_more.z & 2048u) != 0u) {  // raw signals (22.20): copied through
        g_out[id.xy] = float2(raw, 1.0f / most_frames());
        return;
    }
    float first = 0.0f, square = 0.0f, taps = 0.0f;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            const int2 at = clamp(pixel + int2(x, y), int2(0, 0), int2(width, height) - 1);
            const float4 near = g_guide.Load(int3(at, 0));
            if (near.a != 0.0f && near.g == guide.g && near.b == guide.b) {
                const float value = g_raw.Load(int3(at, 0));
                first += value;
                square += value * value;
                taps += 1.0f;
            }
        }
    }
    const float mean = first / max(taps, 1.0f);
    const float spread = sqrt(max(square / max(taps, 1.0f) - mean * mean, 0.0f));
    const float low = min(raw, mean - 1.25f * spread - 0.02f), high = max(raw, mean + 1.25f * spread + 0.02f);
    float sum = 0.0f, frames = 0.0f, weight = 0.0f;
    if (rt_history() && g_pass.w != 0u) {
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
                const float2 history = g_history.Load(int3(from, 0));
                sum += history.x * w;
                frames += history.y * w;
                weight += w;
            }
        }
    }
    if (weight < 0.01f) {
        g_out[id.xy] = float2(raw, 1.0f / most_frames());
        return;
    }
    const float count = min(frames / weight * most_frames() + 1.0f, most_frames());
    const float share = max(1.0f / count, 1.0f / most_frames());
    g_out[id.xy] = float2(lerp(clamp(sum / weight, low, high), raw, share), count / most_frames());
}
