// SPDX-License-Identifier: GPL-3.0-only
// The reflections over space: an a-trous step (5 by 5 taps `step` pixels apart; run with
// steps 1 and 2). A mirror (roughness under 0.05) stays as it is: it must stay sharp. Otherwise a neighbour counts
// by the same object, the same plane, the reflection lobe (its normal against the pixel's, sharper the smoother the
// surface) and a like ray length, so near and far reflections do not mix.
// Channels: rgb the radiance seen, a the ray's length (0: no reflection here). Pass constants: g_pass.y the step.
#include "rt_bindings.hlsli"
#include "rt_frame.hlsli"

Texture2D<float4> g_in : register(t9);
Texture2D<float4> g_guide : register(t11);
Texture2D<float4> g_material : register(t12);
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
    const float3 normal = rt_unpack_normal(guide.a);
    const float3 place = rt_eye_ray(g_ray, float2(pixel) + 0.5f) * guide.r;
    const float off_plane = 1.5f * abs(g_ray.x) * guide.r + 0.002f * guide.r + 0.25f;
    const float lobe = 0.1f / max(roughness * roughness, 0.001f);
    float3 sum = centre.rgb * (kTap[0] * kTap[0]);
    float weight = kTap[0] * kTap[0];
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            if ((x == 0 && y == 0) || (g_more.z & 2048u) != 0u) {  // raw signals (22.20): the centre alone
                continue;
            }
            const int2 at = pixel + int2(x, y) * step_;
            if (at.x < 0 || at.y < 0 || at.x >= int(width) || at.y >= int(height)) {
                continue;
            }
            const float4 near = g_guide.Load(int3(at, 0));
            const float4 value = g_in.Load(int3(at, 0));
            if (near.a == 0.0f || value.a == 0.0f || near.g != guide.g || near.b != guide.b) {
                continue;
            }
            const float3 there = rt_eye_ray(g_ray, float2(at) + 0.5f) * near.r;
            const float apart = abs(dot(normal, there - place));
            const float same = saturate(1.0f - apart / (off_plane * float(step_)));
            const float facing = pow(max(dot(normal, rt_unpack_normal(near.a)), 0.0f), lobe);
            const float length_like = exp(-abs(value.a - centre.a) / (0.1f * centre.a + 10.0f));
            const float w = kTap[abs(x)] * kTap[abs(y)] * same * facing * length_like;
            sum += value.rgb * w;
            weight += w;
        }
    }
    g_out[id.xy] = float4(sum / weight, centre.a);
}
