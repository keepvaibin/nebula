// SPDX-License-Identifier: GPL-3.0-only
// The sun pass: how much of the stage's one shadow sun reaches each pixel's surface. One ray a pixel toward a point
// of the sun's disc, from a place within the pixel (another each frame: the average over time holds the share of the
// pixel a hard edge covers). Writes V raw: r the share that reaches (0 or 1 this frame), g how much of the sun's light
// the surface takes (by its facing), a what a painted surface keeps of its authored brightness (its shading is painted
// in, so the sun's shadow takes it to the stage's own shadow ratio). The light pass (rt_light.hlsl) adds the sky's
// part and writes the shares the programs read.
#include "rt_common.hlsli"

Texture2D<float4> g_guide : register(t9);
Texture2D<float4> g_normals : register(t10);
Texture2D<float4> g_motion : register(t11);
Texture2D<float4> g_material : register(t12);
RT_FORMAT(rgba16f) RWTexture2D<float4> g_sun_raw : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint width, height;
    g_sun_raw.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) {
        return;
    }
    const int2 pixel = int2(id.xy);
    const bool second = rt_second_layer();
    if (second && g_item_map.Load(int3(pixel, 0)).x == 0u) {
        return;
    }
    const float4 guide = g_guide.Load(int3(pixel, 0));
    float4 share = float4(1.0f, 1.0f, 0.0f, 1.0f);
    if (guide.a == 0.0f || (g_more.z & 1u) == 0u) {
        g_sun_raw[id.xy] = share;
        return;
    }
    const float2 centre = float2(id.xy) + 0.5f;
    const RtSurface s = rt_surface(pixel, guide, g_normals.Load(int3(pixel, 0)), g_motion.Load(int3(pixel, 0)).w);
    const uint flags = uint(g_material.Load(int3(pixel, 0)).b * 255.0f + 0.5f);
    const bool lit = (flags & kSurfaceLit) != 0u, thin = (flags & kSurfaceTwoSided) != 0u;
    InterlockedAdd(g_counters[16], 1u);
    // A sheet drawn from both sides (a leaf) is lit from either; its ray leaves from the side the sun is on. A side
    // turned from the sun is in its own shadow, by the mesh's smooth normal (a lit program's sum says so by itself; for
    // any other surface the pass says it): no ray.
    const float turned = dot(s.smooth_, g_sun.xyz);
    const float facing = thin ? abs(turned) : turned;
    const float lambert = saturate(facing);
    bool shadowed = false;
    if (facing > 0.0f) {
        const float3 toward = within(g_sun.xyz, g_sun.w, light_noise(id.xy, g_more.y, 0u));
        // From a place within the pixel, another each frame: where the eye's ray through that place meets the
        // surface's own plane, no farther than two pixels' width at this distance.
        float3 within_pixel = float3(0.0f, 0.0f, 0.0f);
        if (!second) {
            const float3 through = rt_eye_ray(g_ray, centre + light_noise(id.xy, g_more.y, 11u) - 0.5f);
            const float slant = dot(s.normal, through);
            if (abs(slant) > 1e-4f) {
                const float3 moved = through * (dot(s.normal, s.at) / slant) - s.at;
                const float most = 2.0f * abs(g_ray.x) * s.away;
                const float size = length(moved);
                within_pixel = size > most ? moved * (most / size) : moved;
            }
        }
        const float3 from = (turned >= 0.0f ? s.origin : s.at - s.normal * s.skin) + within_pixel;
        // A lit surface is shadowed by everything solid; so is a painted one, unless the painted style asks for lit
        // things only (what the painting could not know).
        uint met;
        shadowed = blocked(from, toward, 200000.0f, lit || g_look.y == 0.0f ? kMaskSolid | kMaskCutout : kMaskLit, true, met);
        if (shadowed) {
            InterlockedAdd(g_counters[18], 1u);
        }
    } else if (!lit) {
        shadowed = true;
    }
    // How much of the sun the surface takes: all of it facing it, none turned from it or in shadow. A lit program sums
    // its key by that share (r): its shadow is the key it loses, and its ambient stays whole. A painted surface keeps
    // its authored brightness where the sun reaches it at g_look.z or more of full facing, and falls to g_look.x of it
    // as the sun leaves it: never brighter than painted, and one number says how dark a shadow is in every stage.
    const float reached = shadowed ? 0.0f : 1.0f;
    const float sunlit = reached * saturate(lambert / g_look.z);
    share.r = reached;
    share.a = lit ? 1.0f : lerp(g_look.x, 1.0f, sunlit);
    share.g = lit ? reached * lambert : sunlit;
    g_sun_raw[id.xy] = share;
}
