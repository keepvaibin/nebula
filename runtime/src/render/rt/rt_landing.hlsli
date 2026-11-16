// SPDX-License-Identifier: GPL-3.0-only
// The landing cue, shared by the passes that write the Light picture (rt_light_space.hlsl's compose step, and the
// reference's rt_reference_out.hlsl), so a preset and the reference show the same disc.
#ifndef NEBULA_RT_LANDING_HLSLI
#define NEBULA_RT_LANDING_HLSLI

#include "rt_bindings.hlsli"
#include "rt_frame.hlsli"

// The drops (48 bytes each from g_at.z: base, radius; direction, length; caster, the cue bit, the body's half reach
// along, its radius across) by 16-pixel tiles (from g_at.w; entries from g_more.x).
ByteAddressBuffer g_drops : register(t3);
ByteAddressBuffer g_tiles : register(t4);

// What the cue takes at most of the light under the player: it reads in sun and in shade and is never a black blot.
static const float kLandingCue = 0.30f;

// A soft disc on what faces up under the player, as wide as the body across the drop, its edge widening with the
// height as under a light 2 degrees across (g_stage.w), fading out to the drop's end. 1: none. `guide`: the pixel's
// S.guide (eye distance, the normal packed).
float landing_cue(int2 pixel, float4 guide) {
    const uint2 tile = uint2(pixel) / max(g_grid.z, 1u);
    if (tile.x >= g_grid.x || tile.y >= g_grid.y) {
        return 1.0f;
    }
    const float3 place = rt_eye_ray(g_ray, float2(pixel) + 0.5f) * guide.r;
    const float3 normal = rt_unpack_normal(guide.a);
    const uint2 run = g_tiles.Load2(g_at.w + (tile.y * g_grid.x + tile.x) * 8u);
    float v = 1.0f;
    for (uint k = 0u; k < min(run.y, 8u); ++k) {
        const uint at = g_at.z + g_tiles.Load(g_more.x + (run.x + k) * 4u) * 48u;
        const uint4 rest = g_drops.Load4(at + 32u);
        if ((rest.y & 1u) == 0u) {
            continue;
        }
        const float4 base = asfloat(g_drops.Load4(at));
        const float4 way = asfloat(g_drops.Load4(at + 16u));
        const float3 off = place - base.xyz;
        const float along = dot(off, way.xyz);
        // From halfway down the body (its own upward faces below that: the ground under the feet) to the drop's end,
        // on what faces up against the drop.
        const float start = 0.5f * asfloat(rest.z);
        if (along <= start || along > way.w || dot(normal, way.xyz) >= 0.0f) {  // way.w: the drop's length
            continue;
        }
        const float height = along - start;
        const float aside = length(off - along * way.xyz);
        const float radius = asfloat(rest.w);
        const float wide = max(height * g_stage.w, 1.0f);
        const float under = 1.0f - smoothstep(radius - wide, radius + wide, aside);
        const float fade = 1.0f - saturate(height / max(way.w - start, 1.0f));
        v = min(v, 1.0f - under * fade);
    }
    return lerp(1.0f, v, kLandingCue);
}

#endif
