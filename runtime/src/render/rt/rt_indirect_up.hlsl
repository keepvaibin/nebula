// SPDX-License-Identifier: GPL-3.0-only
// The sky and bounce light from half to full resolution (the master plan's 11.3): each pixel takes the four nearest
// half-res values (the filter's last step: rgb in r, g, b), weighed by where they lie and by how surely they are its
// own surface: the same object, the same distance, facing the same way (joint bilateral upsampling, Kopf et al.
// 2007), so I stays inside the edges the picture has. Where none of the four is its surface, the nearest of any.
// Written clamped to [kSkyMin, kBounceMax]: what the light pass and, from 22.13, the programs read.
// Pass constants: g_pass_value[0].x kSkyMin, .y kBounceMax.
#include "rt_bindings.hlsli"
#include "rt_frame.hlsli"

Texture2D<float4> g_half : register(t9);         // the filtered I (half res)
Texture2D<float4> g_half_guide : register(t10);  // its receivers' guide (half res)
Texture2D<float4> g_guide : register(t11);       // the pixel's own
RT_FORMAT(rgba16f) RWTexture2D<float4> g_out : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint width, height, half_width, half_height;
    g_out.GetDimensions(width, height);
    g_half.GetDimensions(half_width, half_height);
    if (id.x >= width || id.y >= height) {
        return;
    }
    const float4 guide = g_guide.Load(int3(id.xy, 0));
    if (guide.a == 0.0f) {
        g_out[id.xy] = float4(1.0f, 1.0f, 1.0f, 1.0f);
        return;
    }
    const float3 normal = rt_unpack_normal(guide.a);
    // The pixel's middle in half-res pixels, less the half a tap's middle stands at.
    const float2 at = (float2(id.xy) + 0.5f) * 0.5f - 0.5f;
    const float2 corner = floor(at);
    const float2 part = at - corner;
    float3 sum = float3(0.0f, 0.0f, 0.0f), any_sum = float3(0.0f, 0.0f, 0.0f);
    float weight = 0.0f, any_weight = 0.0f;
    for (int tap = 0; tap < 4; ++tap) {
        const int2 offset = int2(tap & 1, tap >> 1);
        const int2 from = clamp(int2(corner) + offset, int2(0, 0), int2(half_width, half_height) - 1);
        const float4 near = g_half_guide.Load(int3(from, 0));
        if (near.a == 0.0f) {
            continue;
        }
        const float place = (offset.x != 0 ? part.x : 1.0f - part.x) * (offset.y != 0 ? part.y : 1.0f - part.y) + 1e-3f;
        const float3 value = g_half.Load(int3(from, 0)).rgb;
        any_sum += value * place;
        any_weight += place;
        const float same_object = near.g == guide.g && near.b == guide.b ? 1.0f : 0.05f;
        const float depth = exp(-abs(near.r - guide.r) / (0.01f * guide.r + 0.5f));
        const float facing = pow(saturate(dot(normal, rt_unpack_normal(near.a))), 8.0f);
        const float w = place * same_object * depth * facing;
        sum += value * w;
        weight += w;
    }
    float3 ratio = weight > 1e-4f ? sum / weight : any_weight > 0.0f ? any_sum / any_weight : float3(1.0f, 1.0f, 1.0f);
    ratio = clamp(ratio, g_pass_value[0].x, g_pass_value[0].y);
    g_out[id.xy] = float4(ratio, 1.0f);
}
