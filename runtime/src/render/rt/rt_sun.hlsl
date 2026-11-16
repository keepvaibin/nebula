// SPDX-License-Identifier: GPL-3.0-only
// The sun pass: how much of the stage's one shadow sun reaches each pixel's surface. One ray a pixel
// toward a point of the sun's disc, from a place within the pixel (both by rt_sample: another each frame, so the
// average over time holds the share of the disc and of the pixel a hard edge covers). Writes V raw:
//   rgb  1 where the ray reaches the sun, 0 where something solid stands on it, else what the blended things on it
//        let through (let_through: a red crystal's shadow is red);
//   a    how far along the ray the nearest such thing stood, in units (kNoOccluder: nothing): the sun filter's blur
//        follows the penumbra's true width by it (rt_sun_time, rt_sun_blur).
// A surface turned from the sun takes none of its key whatever V is: no ray, V = 1 and no occluder, so the filter
// draws no edge there; the light pass puts the facing in (rt_light.hlsl).
#include "rt_common.hlsli"

Texture2D<float4> g_guide : register(t9);
Texture2D<float4> g_normals : register(t10);
Texture2D<float4> g_motion : register(t11);
Texture2D<float4> g_material : register(t12);
RT_FORMAT(rgba16f) RWTexture2D<float4> g_sun_raw : register(u0);
// The farthest caster's distance written: the largest half float under kNoOccluder (kNoOccluder - 1 rounds up to
// kNoOccluder in this image, and a caster past 65488 units then gave the blur no distance: rt_sun_time).
static const float kFarthestOccluder = 65472.0f;

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
    if (second) {
        const uint layer_item = g_item_map.Load(int3(pixel, 0)).x;
        const bool layer_water = (g_items.Load(g_at.x + (layer_item - 1u) * kItemBytes + 44u) & 255u) == kClassWater;
        rt_count_occluder(layer_water, guide.a == 0.0f || (g_more.z & 1u) == 0u ? kOccluderNoSurface : kOccluderLayerTwo);
        rt_count_layer_item(layer_water, layer_item - 1u);
    }
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
    // A crown of leaves or a cloud (item word 14 bits 26-31) is lit from its far side too.
    const uint own_item = g_item_map.Load(int3(pixel, 0)).x;
    // Water's surface on the second layer: the light its body takes in, by the surface itself, not by a ripple's tilt
    // (the waves' slopes are the mirror's; by them the body's light went dark and bright pixel by pixel).
    const bool on_water = rt_second_layer() && own_item != 0u &&
                          (g_items.Load(g_at.x + (own_item - 1u) * kItemBytes + 44u) & 255u) == kClassWater;
    // A sheet drawn from both sides (a leaf) is lit from either; its ray leaves from the side the sun is on. So is
    // water: its body takes the sun in whichever way its surface faces (the far side of a sphere of water lets it in).
    const float turned = dot(on_water ? s.normal : s.smooth_, g_sun.xyz);
    const float through = own_item != 0u ? float(g_items.Load(g_at.x + (own_item - 1u) * kItemBytes + 56u) >> 26u) / 63.0f : 0.0f;
    const float facing = thin || through > 0.0f || on_water ? abs(turned) : turned;
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
        const float3 from_surface = (turned >= 0.0f ? s.origin : s.at - s.normal * s.skin) + within_pixel;
        // A two-sided cutout's ray starts kCardClear out; distances stay counted from the surface.
        const uint card_item = thin ? g_item_map.Load(int3(pixel, 0)).x : 0u;
        const float clear = card_item != 0u && (g_items.Load(g_at.x + (card_item - 1u) * kItemBytes + 4u) & kItemAlphaTested) != 0u
                                ? kCardClear : 0.0f;
        const float3 from = from_surface + toward * clear;
        // A lit surface is shadowed by everything solid; so is a painted one, unless its model's paint holds the
        // shadows things cast on it (hit flag NEBULA_HIT_PAINTED_SHADOW, item word 11 bits 8-15): then by lit things
        // only, what the painting could not know.
        bool paint_holds = false;
        if (!lit) {
            const uint item = g_item_map.Load(int3(pixel, 0)).x;
            paint_holds = item != 0u && (g_items.Load(g_at.x + (item - 1u) * kItemBytes + 44u) >> 8u & kHitPaintedShadow) != 0u;
        }
        uint met;
        // A billboard's own sun card is no shadow on it; a card's or a crown's own clump none either.
        // The nearest caster, not the first the walk meets: the blur's width is 2 tan x its distance, and a first hit
        // is any layer of a crown, often the farthest (NRD's SIGMA inputs: no ACCEPT_FIRST_HIT for the distance; B3).
        const bool clump = through > 0.0f || clear > 0.0f;
        // A character's ray passes over its own body at its start (rt_common.hlsli g_ray_own_slot).
        const uint own_at = g_at.x + (own_item - 1u) * kItemBytes;
        g_ray_own_slot = own_item != 0u && (g_items.Load(own_at + 4u) & kItemSetCharacter) != 0u ? g_items.Load(own_at) : 0xFFFFFFFFu;
        g_ray_own_reach = kCharacterSkin;
        const float occluder = occluder_at_but(from, toward, 200000.0f,
                                               (paint_holds ? kMaskLit : kMaskSolid | kMaskCutout) | kMaskShadowOnly, false,
                                               own_item != 0u ? (own_item - 1u) | (clump ? kOwnWhole : 0u) : 0xFFFFFFFFu, met);
        g_ray_own_slot = 0xFFFFFFFFu;
        if (occluder >= 0.0f) {
            v = float4(0.0f, 0.0f, 0.0f, min(occluder + clear, kFarthestOccluder));
            InterlockedAdd(g_counters[18], 1u);
            rt_count_occluder(on_water, met);
        } else {
            float first;
            float3 tint = float3(1.0f, 1.0f, 1.0f);  // below high, blended things cast no shadow
            first = -1.0f;
            if (rt_part(kPartSun) >= 3u) {
                tint = let_through_from(from, toward, 200000.0f, on_water, first);
            }
            const bool dimmed = first >= 0.0f && dot(tint, float3(0.2126f, 0.7152f, 0.0722f)) < 0.05f;
            if (first >= 0.0f) {
                v = float4(tint, min(first + clear, kFarthestOccluder));
            }
            rt_count_occluder(on_water, dimmed ? kOccluderBlended : kOccluderReached);
        }
    } else {
        rt_count_occluder(on_water, kOccluderTurned);
    }
    g_sun_raw[id.xy] = v;
}
