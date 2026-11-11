// SPDX-License-Identifier: GPL-3.0-only
// The light pass's shares over time. One ray a light a pixel is a coin toss inside a penumbra: each pixel's shares are
// averaged with what the same surface point had in the frames before. The point is found again by where it was (the
// light pass follows the point itself: the camera's move, a thing's own, a skin's bending), and its
// history counts only when the pixel there was the same object at the same depth. What a moving caster leaves behind
// is removed by holding the history inside what this frame's rays saw around the pixel: their mean, give or take
// their spread, over five by five pixels of the same object (three by three of one ray each is all lit or all dark
// too often inside a penumbra, and the history then jumps: speckles).
// Channels (rt_light.hlsl): r the sun's share, b the lamp's, a the ground's; g here is how many frames the average
// holds, over kMostFrames.
// Compiled a second time as rt_lights_time.hlsl (RT_LIGHTS) for the local lights' share: red, green, blue in r, b, a,
// light of any size. One ray to one light chosen of many is noisier than a coin toss and by no fixed amount, so that
// kind also keeps what its noise is (Schied et al. 2017, "Spatiotemporal Variance-Guided Filtering"): the first and
// second moment of the luminance over time, and from them the variance of the average, which the steps over space
// (rt_lights_space.hlsl) weigh their taps by. A pixel with fewer than four frames behind it takes the variance of
// this frame's rays around it instead.
#include "rt_bindings.hlsli"
#include "rt_frame.hlsli"

Texture2D<float4> g_raw : register(t9);            // this frame's shares, one ray each
Texture2D<float4> g_history : register(t10);       // the frame before's averages
Texture2D<float4> g_guide : register(t11);         // this frame: eye distance, the object's slot in two parts, 1 on a surface
Texture2D<float4> g_guide_before : register(t12);  // the frame before's
Texture2D<float4> g_motion : register(t13);        // where each pixel's point was: its move in pixels, its distance there (0: not known)
RT_FORMAT(rgba16f) RWTexture2D<float4> g_out : register(u0);
#ifdef RT_LIGHTS
Texture2D<float4> g_moments_before : register(t14);            // the frame before's moments
RT_FORMAT(rgba16f) RWTexture2D<float4> g_moments : register(u1);  // r, g: the luminance's first and second moment over time;
                                                                  // b: the variance of the average
static const float3 kLuminance = float3(0.2126f, 0.7152f, 0.0722f);
#endif

static const float kMostFrames = 32.0f;

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
    const float3 raw = g_raw.Load(int3(pixel, 0)).rba;
    if (guide.a == 0.0f) {
#ifdef RT_LIGHTS
        g_out[id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        g_moments[id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
#else
        g_out[id.xy] = float4(1.0f, 0.0f, 1.0f, 1.0f);
#endif
        return;
    }
    // What this frame's rays saw around the pixel on the same object: the history may not leave their mean by more
    // than their spread.
    float3 first = float3(0.0f, 0.0f, 0.0f), square = float3(0.0f, 0.0f, 0.0f);
    float taps = 0.0f;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            const int2 at = clamp(pixel + int2(x, y), int2(0, 0), int2(width, height) - 1);
            const float4 near = g_guide.Load(int3(at, 0));
            if (near.a != 0.0f && near.g == guide.g && near.b == guide.b) {
                const float3 value = g_raw.Load(int3(at, 0)).rba;
                first += value;
                square += value * value;
                taps += 1.0f;
            }
        }
    }
    const float3 mean = first / max(taps, 1.0f);
    const float3 spread = sqrt(max(square / max(taps, 1.0f) - mean * mean, 0.0f));
    const float3 low = min(raw, mean - 1.25f * spread - 0.02f), high = max(raw, mean + 1.25f * spread + 0.02f);
    float3 sum = float3(0.0f, 0.0f, 0.0f);
    float frames = 0.0f, weight = 0.0f;
#ifdef RT_LIGHTS
    const float bright = dot(raw, kLuminance);
    const float around = dot(spread, kLuminance) * dot(spread, kLuminance);  // the variance of this frame's rays about the pixel
    float2 moments = float2(0.0f, 0.0f);
#endif
    if (g_kept.z != 0u) {
        const float4 motion = g_motion.Load(int3(pixel, 0));
        if (motion.z > 0.0f) {
            const float away = motion.z;
            const float2 at = float2(id.xy) + motion.xy;  // the pixel's middle, moved, less the half a tap's middle stands at
            const float2 corner = floor(at);
            const float2 part = at - corner;
            for (int tap = 0; tap < 4; ++tap) {
                const int2 offset = int2(tap & 1, tap >> 1);
                const int2 from = int2(corner) + offset;
                if (from.x < 0 || from.y < 0 || from.x >= int(width) || from.y >= int(height)) {
                    continue;
                }
                const float4 before = g_guide_before.Load(int3(from, 0));
                if (!same_surface(guide, before, away)) {
                    continue;
                }
                const float w = (offset.x != 0 ? part.x : 1.0f - part.x) * (offset.y != 0 ? part.y : 1.0f - part.y);
                const float4 history = g_history.Load(int3(from, 0));
                sum += history.rba * w;
                frames += history.g * w;
                weight += w;
#ifdef RT_LIGHTS
                moments += g_moments_before.Load(int3(from, 0)).rg * w;
#endif
            }
        }
    }
    if (weight < 0.01f) {
        g_out[id.xy] = float4(raw.x, 1.0f / kMostFrames, raw.y, raw.z);
#ifdef RT_LIGHTS
        g_moments[id.xy] = float4(bright, bright * bright, around, 0.0f);
#endif
        return;
    }
    const float3 history = clamp(sum / weight, low, high);
    const float count = min(frames / weight * kMostFrames + 1.0f, kMostFrames);
    const float3 now = lerp(history, raw, max(1.0f / count, 0.06f));
    g_out[id.xy] = float4(now.x, count / kMostFrames, now.y, now.z);
#ifdef RT_LIGHTS
    const float2 both = lerp(moments / weight, float2(bright, bright * bright), max(1.0f / count, 0.06f));
    const float over_time = max(both.y - both.x * both.x, 0.0f);
    g_moments[id.xy] = float4(both, (count < 4.0f ? max(over_time, around) : over_time) / min(count, 16.0f), 0.0f);
#endif
}
