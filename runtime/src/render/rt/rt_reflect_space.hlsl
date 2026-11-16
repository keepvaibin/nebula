// SPDX-License-Identifier: GPL-3.0-only
// The reflections over space: an a-trous step (5 by 5 taps `step` pixels apart; run with
// steps 1 and 2). A mirror (roughness under 0.05) stays as it is: it must stay sharp. Otherwise a neighbour counts
// by the same object, the same plane, the reflection lobe (its normal against the pixel's, sharper the smoother the
// surface), a like roughness and a like ray length, so near and far reflections do not mix; and only within the
// lobe's own footprint on the screen (ReBLUR: a constant radius over-blurs): the lobe's half angle (90 m / (1 + m)
// degrees, m the roughness squared) at the ray's length t, as the eye at distance d sees the mirror image (at d + t).
// While the average holds fewer frames than it will (n of `frames`, rt_reflect_time.hlsl), the footprint is wider by
// sqrt(frames / n): the estimate's variance goes as 1 / (n taps) and taps as the radius squared, so the blur stands in
// for the frames not yet held (ReBLUR: the accumulated frame count drives the radius, widest just after a disocclusion).
// Channels: rgb the radiance seen, a the ray's length (0: no reflection here). Pass constants: g_pass.y the step;
// g_pass.w bit 1: S.normal bound at t13 (the lobe by the shading normal, the plane by the geometric one, each whole;
// without it the guide's packed normal for both); bit 2: n bound at t14.
#include "rt_bindings.hlsli"
#include "rt_frame.hlsli"

Texture2D<float4> g_in : register(t9);
Texture2D<float4> g_guide : register(t11);
Texture2D<float4> g_material : register(t12);
Texture2D<float4> g_normals : register(t13);  // S.normal: rg the shading normal, ba the geometric one (rt_oct)
Texture2D<float> g_frames : register(t14);  // n, this frame's (rt_reflect_time.hlsl)
RT_FORMAT(rgba16f) RWTexture2D<float4> g_out : register(u0);

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
    const float roughness = g_material.Load(int3(pixel, 0)).r;
    if (centre.a == 0.0f || guide.a == 0.0f || roughness < 0.05f) {
        g_out[id.xy] = centre;
        return;
    }
    static const float kTap[3] = {6.0f / 16.0f, 4.0f / 16.0f, 1.0f / 16.0f};
    const int step_ = int(g_pass.y);
    const bool whole = (g_pass.w & 2u) != 0u;
    const float4 normals = whole ? g_normals.Load(int3(pixel, 0)) : float4(0.0f, 0.0f, 0.0f, 0.0f);
    const float3 normal = whole ? rt_unoct(normals.rg) : rt_unpack_normal(guide.a);
    const float3 plane = whole ? rt_unoct(normals.ba) : normal;
    const float3 place = rt_eye_ray(g_ray, float2(pixel) + 0.5f) * guide.r;
    const float off_plane = 1.5f * abs(g_ray.x) * guide.r + 0.002f * guide.r + 0.25f;
    const float lobe = 0.1f / max(roughness * roughness, 0.001f);
    const float m = roughness * roughness;
    const float half_angle = 1.5707963f * m / (1.0f + m);
    float widen = 1.0f;
    if ((g_pass.w & 4u) != 0u) {
        // rt_reflect_time.hlsl's frames at this roughness; an n past them (frames fell with the rate) is held to them.
        const float frames = lerp(8.0f, 32.0f, saturate((roughness - 0.05f) / 0.25f)) * rt_history_scale();
        widen = sqrt(frames / clamp(g_frames.Load(int3(pixel, 0)), 1.0f, frames));
    }
    const float footprint = centre.a * tan(half_angle) / ((guide.r + centre.a) * abs(g_ray.x)) * widen;  // pixels
    const float rough_norm = roughness * roughness * 0.99f + 0.01f;
    float3 sum = centre.rgb * (kTap[0] * kTap[0]);
    float weight = kTap[0] * kTap[0];
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            if ((x == 0 && y == 0) || (g_more.z & 2048u) != 0u) {  // raw signals: the centre alone
                continue;
            }
            const int2 at = pixel + int2(x, y) * step_;
            const float within = saturate(1.0f - length(float2(x, y)) * float(step_) / footprint);
            if (at.x < 0 || at.y < 0 || at.x >= int(width) || at.y >= int(height) || !(within > 0.0f)) {
                continue;
            }
            const float4 near = g_guide.Load(int3(at, 0));
            const float4 value = g_in.Load(int3(at, 0));
            if (near.a == 0.0f || value.a == 0.0f || near.g != guide.g || near.b != guide.b) {
                continue;
            }
            const float3 there = rt_eye_ray(g_ray, float2(at) + 0.5f) * near.r;
            const float apart = abs(dot(plane, there - place));
            const float same = saturate(1.0f - apart / (off_plane * float(step_)));
            const float3 near_normal = whole ? rt_unoct(g_normals.Load(int3(at, 0)).rg) : rt_unpack_normal(near.a);
            const float facing = pow(max(dot(normal, near_normal), 0.0f), lobe);
            const float length_like = exp(-abs(value.a - centre.a) / (0.1f * centre.a + 10.0f));
            const float rough_like = saturate(1.0f - abs(g_material.Load(int3(at, 0)).r - roughness) / rough_norm);
            const float w = kTap[abs(x)] * kTap[abs(y)] * same * facing * length_like * rough_like * within;
            sum += value.rgb * w;
            weight += w;
        }
    }
    g_out[id.xy] = float4(sum / weight, centre.a);
}
