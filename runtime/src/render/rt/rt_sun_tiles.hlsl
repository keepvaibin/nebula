// SPDX-License-Identifier: GPL-3.0-only
// The sun filter's first pass (plan 7.3): each 16 x 16 tile of the picture, one group a tile, says whether its rays
// all met the same kind of thing on the way to the sun: nothing (0: all lit), something solid (0.5: all shadowed),
// blended things only (0.25: a tinted shadow, one value a caster), or more than one kind (1: a penumbra or an edge
// there, where one ray a pixel is a coin toss between them). By what was met, not by V against a threshold: the edge of
// a red crystal's shadow on lit ground (V 0.3 beside 1) or on a solid one (0.3 beside 0) is a penumbra too, and a
// smooth tint (water's caustics, a cloud's alpha) is not. The later passes take a pixel as penumbra where its tile or
// one next to it is (a dilation by one tile); everywhere else they copy V through, which is most of the screen and
// what makes the filter cheap.
#include "rt_bindings.hlsli"
#include "rt_frame.hlsli"

Texture2D<float4> g_sun_raw : register(t9);
Texture2D<float4> g_guide : register(t11);
RT_FORMAT(r8) RWTexture2D<float> g_tiles : register(u0);

groupshared uint g_kinds;  // bit 0 nothing met, 1 something solid, 2 blended things that tint

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID, uint3 group : SV_GroupID, uint index : SV_GroupIndex) {
    if (index == 0u) {
        g_kinds = 0u;
    }
    GroupMemoryBarrierWithGroupSync();
    uint width, height;
    g_sun_raw.GetDimensions(width, height);
    if (id.x < width && id.y < height && g_guide.Load(int3(id.xy, 0)).a != 0.0f) {
        // rt_sun.hlsl: rgb 0 where a solid stood, 1 where nothing took any of the sun (nothing met, a surface turned
        // from the sun, or only blended things that let all of it through), else what blended things let through.
        const float3 v = g_sun_raw.Load(int3(id.xy, 0)).rgb;
        const uint kind = all(v == 0.0f) ? 2u : all(v == 1.0f) ? 1u : 4u;
        InterlockedOr(g_kinds, kind);
    }
    GroupMemoryBarrierWithGroupSync();
    if (index == 0u) {
        const uint kinds = g_kinds;
        g_tiles[group.xy] = countbits(kinds) > 1u ? 1.0f : kinds == 2u ? 0.5f : kinds == 4u ? 0.25f : 0.0f;
    }
}
