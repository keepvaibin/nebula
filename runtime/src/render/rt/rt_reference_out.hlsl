// SPDX-License-Identifier: GPL-3.0-only
// The reference's means (rt_reference.hlsl) as the pictures the programs read, in the light pass's own encodings
// (rt_sun.hlsl, rt_light.hlsl): so a reference picture differs from a preset's only by its signals. Runs after the
// filters, over what they wrote.
//   Light: rgb V, a O: the sky and the bounce light's ratio, mean(I) held as rt_indirect_up holds a preset's (the light
//     pass's contract: the programs put the shares together with the Facing picture's n.l). With no
//     traced sun V is the point's O over the room's open level, at most 1, as rt_light_space.hlsl gives a preset's;
//     the emitters alone (g_more.z bit 7) none, as there.
//   Added: rgb D.   Mirror: not written here: under Medium the light pass's (the local lights' highlights, as in a
//     preset); at Medium and High rt_reflect_out.hlsl composites the reference's R (rt_bounce.cpp rt_reference_publish).
//   Indirect: rgb O a channel at a time, held as Light's a (what the programs' picture 7 reads).
//   The landing cue on Light as rt_light_space.hlsl's compose step puts it (g_more.z bit 13), where the host
//     binds what it reads: t13 the opaque layer's guide, t3 and t4 the drops and their tiles (g_pass_value[0].z 1).
// Pass constants: g_pass_value[0].x kSkyMin, .y kBounceMax, .z 1: the landing cue's guide and drops bound.
#include "rt_bindings.hlsli"
#include "rt_frame.hlsli"
#include "rt_landing.hlsli"  // landing_cue

Texture2D<float4> g_v : register(t9);
Texture2D<float4> g_d : register(t10);
Texture2D<float4> g_i : register(t11);
Texture2D<float4> g_r : register(t12);
Texture2D<float4> g_guide : register(t13);  // the opaque layer's guide: where the landing cue falls
RT_FORMAT(rgba16f) RWTexture2D<float4> g_light : register(u0);
RT_FORMAT(rgba16f) RWTexture2D<float4> g_added : register(u1);
RT_FORMAT(rgba16f) RWTexture2D<float4> g_mirror : register(u2);
RT_FORMAT(rgba16f) RWTexture2D<float4> g_indirect : register(u3);

// O held as rt_indirect_up.hlsl holds a preset's, step for step (its twin: change both): its luminance at least kSkyMin,
// the colour kept; a channel within 0.5 to 2 of the luminance, the luminance kept; a channel at most
// kBounceMax.
float3 o_held(float3 ratio, float sky_min, float bounce_max) {
    const float3 kLuma = float3(0.2126f, 0.7152f, 0.0722f);
    ratio = max(ratio, 0.0f);
    const float raw = dot(ratio, kLuma);
    ratio = raw > 1e-4f ? ratio * max(sky_min / raw, 1.0f) : float3(sky_min, sky_min, sky_min);
    const float lum = dot(ratio, kLuma);
    if (lum > 1e-4f) {
        const float3 held = clamp(ratio, 0.5f * lum, 2.0f * lum);
        ratio = held * (lum / max(dot(held, kLuma), 1e-6f));
    }
    return min(ratio, bounce_max);
}

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
    const float3 ratio = o_held(i.rgb, g_pass_value[0].x, g_pass_value[0].y);
    const float seen = (ratio.r + ratio.g + ratio.b) / 3.0f;
    float4 shares = float4(v.rgb, seen);  // V rgb (in colour, as the sun pass), O
    if ((g_more.z & 1u) == 0u) {
        const float open_ = g_sky.w > 0.0f ? clamp(g_sky.w, 0.25f, 1.0f) : 1.0f;
        shares.rgb = min(seen / open_, 1.0f).xxx;
    }
    if ((g_more.z & 8192u) != 0u && g_pass_value[0].z > 0.5f) {
        shares *= landing_cue(int2(id.xy), g_guide.Load(here));  // the landing cue under the player
    }
    if ((g_more.z & 128u) != 0u) {
        shares = float4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    g_light[id.xy] = shares;
    g_added[id.xy] = float4(min(d.rgb, 32.0f), 0.0f);
    g_indirect[id.xy] = float4(ratio, 1.0f);  // rgb O
}
