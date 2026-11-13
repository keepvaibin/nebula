// SPDX-License-Identifier: GPL-3.0-only
// The reference's means (rt_reference.hlsl) as the pictures the programs read, in the light pass's own encodings
// (rt_sun.hlsl, rt_light.hlsl): so a reference picture differs from a preset's only by its signals. Runs after the
// filters, over what they wrote.
//   Light: rgb V, a O: the sky and the bounce light's ratio, mean(I) held to [kSkyMin, kBounceMax] (the light pass's
//     contract since FA's l26: the programs put the shares together with the Facing picture's n.l).
//   Added: rgb D.   Mirror: not written here: under Medium the light pass's (the local lights' highlights, as in a
//     preset); at Medium and High rt_reflect_out.hlsl composites the reference's R (rt_bounce.cpp rt_reference_publish).
//   Indirect: rgb O a channel at a time, held as Light's a (what the programs' picture 7 reads; A1-16, FB b79).
// Pass constants: g_pass_value[0].x kSkyMin, .y kBounceMax.
#include "rt_bindings.hlsli"
#include "rt_frame.hlsli"

Texture2D<float4> g_v : register(t9);
Texture2D<float4> g_d : register(t10);
Texture2D<float4> g_i : register(t11);
Texture2D<float4> g_r : register(t12);
RT_FORMAT(rgba16f) RWTexture2D<float4> g_light : register(u0);
RT_FORMAT(rgba16f) RWTexture2D<float4> g_added : register(u1);
RT_FORMAT(rgba16f) RWTexture2D<float4> g_mirror : register(u2);
RT_FORMAT(rgba16f) RWTexture2D<float4> g_indirect : register(u3);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint width, height;
    g_light.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) {
        return;
    }
    const int3 here = int3(id.xy, 0);
    const float4 v = g_v.Load(here), d = g_d.Load(here), i = g_i.Load(here);
    if (i.a == 0.0f || v.a == 0.0f) {
        g_light[id.xy] = float4(1.0f, 1.0f, 1.0f, 1.0f);
        g_added[id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        g_mirror[id.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        g_indirect[id.xy] = float4(1.0f, 1.0f, 1.0f, 1.0f);
        return;
    }
    const float3 ratio = clamp(i.rgb, g_pass_value[0].x, g_pass_value[0].y);
    const float seen = (ratio.r + ratio.g + ratio.b) / 3.0f;
    g_light[id.xy] = float4(v.rgb, seen);  // l26: V rgb (in colour, as the sun pass), O
    g_added[id.xy] = float4(min(d.rgb, 32.0f), 0.0f);
    g_indirect[id.xy] = float4(ratio, 1.0f);  // rgb O (A1-16; FB b79)
}
