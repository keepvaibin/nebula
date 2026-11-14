// SPDX-License-Identifier: GPL-3.0-only
// The reference's means (rt_reference.hlsl) as the pictures the programs read, in the light pass's own encodings
// (rt_sun.hlsl, rt_light.hlsl): so a reference picture differs from a preset's only by its signals. Runs after the
// filters, over what they wrote.
//   Light: r the sun's share V; g 1; b 0; a the ground share: a lit surface keeps its ambient, a painted one falls to
//     the shadow's share as the sun leaves it, and either takes the sky and the bounce light by mean(I) where the sun
//     does not reach (until the programs take I themselves).
//   Added: rgb D.   Mirror: rgb, a as the first cut.
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

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint width, height;
    g_light.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) {
        return;
    }
    const int3 here = int3(id.xy, 0);
    const float4 v = g_v.Load(here), d = g_d.Load(here), i = g_i.Load(here), r = g_r.Load(here);
    if (i.a == 0.0f || v.a == 0.0f) {
        g_light[id.xy] = float4(1.0f, 1.0f, 1.0f, 1.0f);
        g_added[id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        g_mirror[id.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }
    const bool lit = i.a > 1.5f;
    const float sun = v.r;
    const float lambert = saturate(d.a);
    float share = 1.0f, in_sun = 1.0f;
    if ((g_more.z & 1u) != 0u) {
        const float sunlit = sun * saturate(lambert / g_look.z);
        share = lit ? 1.0f : lerp(g_look.x, 1.0f, sunlit);
        in_sun = lit ? sun * lambert : sunlit;
    }
    const float3 ratio = clamp(i.rgb, g_pass_value[0].x, g_pass_value[0].y);
    const float seen = (ratio.r + ratio.g + ratio.b) / 3.0f;
    share *= lerp(seen, 1.0f, 0.65f * in_sun);
    g_light[id.xy] = float4(sun.xxx, seen);  // l26: V rgb, O (the programs put the shares together)
    g_added[id.xy] = float4(min(d.rgb, 32.0f), 0.0f);
    g_mirror[id.xy] = r;
}
