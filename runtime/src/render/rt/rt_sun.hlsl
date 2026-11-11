// SPDX-License-Identifier: GPL-3.0-only
// The sun pass: how much of the stage's one shadow sun reaches each pixel's surface (plan 7.2). One ray a pixel
// toward a point of the sun's disc, from a place within the pixel (both by rt_sample: another each frame, so the
// average over time holds the share of the disc and of the pixel a hard edge covers). Writes V raw:
//   rgb  1 where the ray reaches the sun, 0 where something solid stands on it, else what the blended things on it
//        let through (let_through: a red crystal's shadow is red);
//   a    how far along the ray that thing stood, in units (kNoOccluder: nothing): the sun filter's blur follows the
//        penumbra's true width by it (rt_sun_time, rt_sun_blur).
// A surface turned from the sun takes none of its key whatever V is: no ray, V = 1 and no occluder, so the filter
// draws no edge there; the light pass puts the facing in (rt_light.hlsl).
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
    float4 v = float4(1.0f, 1.0f, 1.0f, kNoOccluder);
    if (guide.a == 0.0f || (g_more.z & 1u) == 0u) {
        g_sun_raw[id.xy] = v;
        return;
    }
    const float2 centre = float2(id.xy) + 0.5f;
    const RtSurface s = rt_surface(pixel, guide, g_normals.Load(int3(pixel, 0)), g_motion.Load(int3(pixel, 0)).w);
    const uint flags = uint(g_material.Load(int3(pixel, 0)).b * 255.0f + 0.5f);
    const bool lit = (flags & kSurfaceLit) != 0u, thin = (flags & kSurfaceTwoSided) != 0u;
    InterlockedAdd(g_counters[16], 1u);
    // A sheet drawn from both sides (a leaf) is lit from either; its ray leaves from the side the sun is on.
    const float turned = dot(s.smooth_, g_sun.xyz);
    const float facing = thin ? abs(turned) : turned;
    if (facing > 0.0f) {
        const float3 toward = within(g_sun.xyz, g_sun.w, rt_sample(id.xy, g_more.y, kSampleSun));
        // From a place within the pixel: where the eye's ray through that place meets the surface's own plane, no
        // farther than two pixels' width at this distance.
        float3 within_pixel = float3(0.0f, 0.0f, 0.0f);
        {
            const float3 through = rt_eye_ray(g_ray, centre + rt_sample(id.xy, g_more.y, kSampleSunPixel) - 0.5f);
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
        const float occluder = occluder_at(from, toward, 200000.0f, lit || g_look.y == 0.0f ? kMaskSolid | kMaskCutout : kMaskLit, true, met);
        if (occluder >= 0.0f) {
            v = float4(0.0f, 0.0f, 0.0f, min(occluder, kNoOccluder - 1.0f));
            InterlockedAdd(g_counters[18], 1u);
        } else {
            float first;
            const float3 tint = let_through(from, toward, 200000.0f, first);
            if (first >= 0.0f) {
                v = float4(tint, min(first, kNoOccluder - 1.0f));
            }
        }
    }
    g_sun_raw[id.xy] = v;
}
