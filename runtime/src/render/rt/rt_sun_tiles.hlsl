// SPDX-License-Identifier: GPL-3.0-only
// The sun filter's first pass: each 16 x 16 tile of the picture, one group a tile, says whether its raw V
// is all lit (0), all shadowed (0.5) or both (1: a penumbra or an edge there). The later passes take a pixel as
// penumbra where its tile or one next to it is (a dilation by one tile); everywhere else they copy V through, which
// is most of the screen and what makes the filter cheap.
#include "rt_bindings.hlsli"
#include "rt_frame.hlsli"

Texture2D<float4> g_sun_raw : register(t9);
Texture2D<float4> g_guide : register(t11);
RT_FORMAT(r8) RWTexture2D<float> g_tiles : register(u0);

groupshared uint g_any_lit;
groupshared uint g_any_dark;

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID, uint3 group : SV_GroupID, uint index : SV_GroupIndex) {
    if (index == 0u) {
        g_any_lit = 0u;
        g_any_dark = 0u;
    }
    GroupMemoryBarrierWithGroupSync();
    uint width, height;
    g_sun_raw.GetDimensions(width, height);
    if (id.x < width && id.y < height && g_guide.Load(int3(id.xy, 0)).a != 0.0f) {
        const float v = g_sun_raw.Load(int3(id.xy, 0)).g;
        if (v > 0.5f) {
            InterlockedOr(g_any_lit, 1u);
        } else {
            InterlockedOr(g_any_dark, 1u);
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if (index == 0u) {
        g_tiles[group.xy] = g_any_lit != 0u && g_any_dark != 0u ? 1.0f : g_any_dark != 0u ? 0.5f : 0.0f;
    }
}
