// SPDX-License-Identifier: GPL-3.0-only
// The light pass's shares over time. One ray a light a pixel is a coin toss inside a penumbra: each pixel's shares are
// averaged with what the same surface point had in the frames before. The point is found again by where it was (the
// light pass follows the point itself: the camera's move, a thing's own, a skin's bending), and its
// history counts only when the pixel there was the same object at the same depth. What a moving caster leaves behind
// is removed by holding the history inside what this frame's rays saw around the pixel: their mean, give or take
// their spread, over five by five pixels of the same object (three by three of one ray each is all lit or all dark
// too often inside a penumbra, and the history then jumps: speckles).
// Channels (rt_light.hlsl): r V (filtered already by the sun's own filter), b how the surface faces the sun, a the
// sky's share: a alone is averaged here, r and b go through as this frame has them; g here is how many frames the
// average holds, over most_frames().
// Compiled a second time as rt_lights_time.hlsl (RT_LIGHTS) for the local lights' share: red, green, blue in r, b, a,
// light of any size. One ray to one light chosen of many is noisier than a coin toss and by no fixed amount, so that
// kind also keeps what its noise is (Schied et al. 2017, "Spatiotemporal Variance-Guided Filtering"): the first and
// second moment of the luminance over time, and from them the variance of the average, which the steps over space
// (rt_lights_space.hlsl) weigh their taps by. A pixel with fewer than four frames behind it takes the variance of
// this frame's rays around it instead. Two guards of ReSTIR's: the boiling filter (RTXDI's): a pixel whose kept light's
// weight is over 20 times the mean of its lit neighbours' is a firefly, brought down to that; and a flash stays a
// flash: where this frame is over four times the history, the history holds at most 8 frames.
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
                                                                  // b: the variance of the average; a 1 where the average
                                                                  // moved by more than a quarter (rt_reactive.hlsl)
Texture2D<float4> g_reservoirs_now : register(t15);           // this frame's reservoirs: b the kept light's weight
static const float3 kLuminance = float3(0.2126f, 0.7152f, 0.0722f);
#endif


// The most frames a history holds: 32 at 60 frames a second, as many seconds at any rate (plan 16.5).
float most_frames() {
    return 32.0f * rt_history_scale();
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
    float3 raw = g_raw.Load(int3(pixel, 0)).rba;
    if (guide.a == 0.0f) {
#ifdef RT_LIGHTS
        g_out[id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        g_moments[id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
#else
        g_out[id.xy] = float4(1.0f, 0.0f, 1.0f, 1.0f);
#endif
        return;
    }
#ifdef RT_LIGHTS
    // Low's checkerboard (rt_lights.hlsl, R-2): a pixel the lights pass left out (g -1) takes its left or right
    // neighbour's share, whichever lies nearer its depth (both were traced).
    const bool left_out = g_raw.Load(int3(pixel, 0)).g < 0.0f;
    if (left_out) {
        const int2 left = int2(max(pixel.x - 1, 0), pixel.y), right = int2(min(pixel.x + 1, int(width) - 1), pixel.y);
        const float to_left = left.x == pixel.x ? 1e30f : abs(g_guide.Load(int3(left, 0)).r - guide.r);
        const float to_right = right.x == pixel.x ? 1e30f : abs(g_guide.Load(int3(right, 0)).r - guide.r);
        raw = g_raw.Load(int3(to_left <= to_right ? left : right, 0)).rba;
    }
    const float own = g_reservoirs_now.Load(int3(pixel, 0)).b;
    if (own > 0.0f && !left_out) {
        float total = 0.0f, lit = 0.0f;
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                const int2 at = clamp(pixel + int2(x, y), int2(0, 0), int2(width, height) - 1);
                const float w = g_reservoirs_now.Load(int3(at, 0)).b;
                if ((x != 0 || y != 0) && w > 0.0f) {
                    total += w;
                    lit += 1.0f;
                }
            }
        }
        const float most = 20.0f * total / max(lit, 1.0f);
        if (lit >= 2.0f && own > most) {
            raw *= most / own;
        }
    }
#endif
    if ((g_more.z & 2048u) != 0u) {
        // Raw signals for an outside denoiser (FB, 22.20): this frame's value as traced (Low's fill and the boiling
        // filter done), one frame, no history.
        g_out[id.xy] = float4(raw.x, 1.0f / most_frames(), raw.y, raw.z);
#ifdef RT_LIGHTS
        g_moments[id.xy] = float4(dot(raw, kLuminance), dot(raw, kLuminance) * dot(raw, kLuminance), 0.0f, 0.0f);
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
                const float4 shares = g_raw.Load(int3(at, 0));
#ifdef RT_LIGHTS
                if (shares.g < 0.0f) {
                    continue;  // left out by Low's checkerboard
                }
#endif
                const float3 value = shares.rba;
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
    if (rt_history()) {
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
        g_out[id.xy] = float4(raw.x, 1.0f / most_frames(), raw.y, raw.z);
#ifdef RT_LIGHTS
        g_moments[id.xy] = float4(bright, bright * bright, around, 0.0f);
#endif
        return;
    }
    const float3 history = clamp(sum / weight, low, high);
    float count = min(frames / weight * most_frames() + 1.0f, most_frames());
#ifdef RT_LIGHTS
    if (bright > 4.0f * dot(history, kLuminance) + 0.02f) {
        count = min(count, 8.0f * rt_history_scale());  // a flash
    }
#endif
    const float3 now = lerp(history, raw, max(1.0f / count, 0.06f / rt_history_scale()));
#ifdef RT_LIGHTS
    g_out[id.xy] = float4(now.x, count / most_frames(), now.y, now.z);
#else
    g_out[id.xy] = float4(raw.x, count / most_frames(), raw.y, now.z);
#endif
#ifdef RT_LIGHTS
    const float2 both = lerp(moments / weight, float2(bright, bright * bright), max(1.0f / count, 0.06f / rt_history_scale()));
    const float over_time = max(both.y - both.x * both.x, 0.0f);
    // The upscaler's history lags a light that changed: its reactive mask takes this frame there (plan 16.4). The
    // averages are compared, not this frame's rays (their noise is no change), and the history before its clamp.
    const float changed = abs(dot(now, kLuminance) - dot(sum / weight, kLuminance)) > 0.25f ? 1.0f : 0.0f;
    g_moments[id.xy] = float4(both, (count < 4.0f ? max(over_time, around) : over_time) / min(count, 16.0f), changed);
#endif
}
