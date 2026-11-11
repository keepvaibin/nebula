// SPDX-License-Identifier: GPL-3.0-only
// The local lights pass: every emitter (the game's point lights, glowing meshes, each particle of a glowing effect) at
// each pixel's surface, by spatiotemporal reservoir resampling (ReSTIR DI: Bitterli et al. 2020; resampled importance
// sampling: Talbot et al. 2005). A pixel keeps ONE light, chosen in proportion to what each would add here unshadowed,
// out of:
//   this frame's candidates: up to eight of the tile's lights; past eight, the tile's four strongest (the host puts
//   them first in its list) each for certain, and four of the rest, one from each equal run;
//   the reservoirs of the frame before at the pixel's own point (found by its motion) and at two points near it, where
//   the surface there is the pixel's own kind (in its plane, facing its way): each stands for all the candidates it
//   was itself chosen among, so a pixel sees what hundreds of tries would find.
// ONE ray then asks whether the kept light reaches the surface (aimed at a point of its disc where the source is large,
// so a lamp's shadow is as soft as the lamp is wide), and what is written is its light times its weight: the
// sum of the weights it was chosen among, over its own and over how many of the candidates could have chosen it (those
// whose surface it reaches: without that share a light's edge darkens). On average the sum of every light with its
// shadow, at a cost that does not grow with the lights. A light found in shadow is kept with no weight, so it is not
// handed on. The filter after the pass (rt_lights_time, rt_lights_space) takes what noise is left. Not on itself.
// On gloss, metal and glass the kept light also gives its highlight (GGX, Schlick; the lobe widened by the source's
// size as it is seen, Karis 2013), by the same weight: LightsSpec, which the reflection's raw image takes before its
// filter.
// The second layer (g_pass.x 1: the nearest blended surface a pixel) is done the same way, with its own reservoirs; its
// filter is the lights' time pass and one step over space.
#include "rt_common.hlsli"

Texture2D<float4> g_guide : register(t9);
Texture2D<float4> g_normals : register(t10);
Texture2D<float4> g_motion : register(t11);
Texture2D<float4> g_reservoirs_before : register(t12);  // the frame before's
Texture2D<float4> g_guide_before : register(t13);       // and its guide: what surface each of those pixels was
Texture2D<float4> g_material : register(t14);           // S.material: r roughness, b class and flags
// The opaque layer: red, green, blue in r, b, a (the channels the filter averages); the second layer: rgb as the
// programs read them.
RT_FORMAT(rgba16f) RWTexture2D<float4> g_lights : register(u0);
// The pixel's reservoir: r, g the kept light's number in this frame's list, from 1, in two parts (each whole in a half
// float; 0: none); b its weight (what its unshadowed light is multiplied by; 0 where it was found in shadow); a how
// many candidates stand behind it (no more than 20).
RT_FORMAT(rgba16f) RWTexture2D<float4> g_reservoirs : register(u1);
// The kept light's highlight, rgb.
RT_FORMAT(rgba16f) RWTexture2D<float4> g_lights_spec : register(u2);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint width, height;
    g_lights.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) {
        return;
    }
    const int2 pixel = int2(id.xy);
    const bool second = rt_second_layer();
    if (second && g_item_map.Load(int3(pixel, 0)).x == 0u) {
        return;
    }
    g_reservoirs[id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);  // none, until the lights say otherwise
    g_lights_spec[id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
    const uint level = rt_part(kPartLights);  // off: none; low: four candidates, the frame before's alone
    if (level == 0u) {
        g_lights[id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
    // Low (R-2): a checkerboard of rays on the opaque layer, the other half each frame. A pixel left out carries the frame
    // before's reservoir at its point, its light renumbered into this frame's list, and marks its share (g -1): the
    // lights' time pass takes its left or right neighbour's, whichever lies nearer its depth.
    if (level == 1u && !second && ((id.x + id.y + g_more.y) & 1u) != 0u) {
        if (rt_history()) {
            const float4 motion = g_motion.Load(int3(pixel, 0));
            uint wide, high;
            g_reservoirs_before.GetDimensions(wide, high);
            const int2 from = int2(floor(float2(id.xy) + 0.5f + motion.xy));
            if (motion.z > 1.0f && from.x >= 0 && from.y >= 0 && from.x < int(wide) && from.y < int(high)) {
                const float4 theirs = g_reservoirs_before.Load(int3(from, 0));
                const uint was_index = uint(theirs.r) + uint(theirs.g) * 1024u;
                if (was_index != 0u && was_index <= g_kept.y) {
                    const uint index = g_drops.Load(g_kept.x + (was_index - 1u) * 4u);
                    if (index < g_glow.w) {
                        g_reservoirs[id.xy] = float4(float((index + 1u) & 1023u), float((index + 1u) >> 10u), theirs.b, theirs.a);
                    }
                }
            }
        }
        g_lights[id.xy] = float4(0.0f, -1.0f, 0.0f, 0.0f);
        return;
    }
    const float4 guide = g_guide.Load(int3(pixel, 0));
    const uint2 tile = id.xy / max(g_grid.z, 1u);
    float3 added = float3(0.0f, 0.0f, 0.0f);
    if (guide.a != 0.0f && g_glow.w != 0u && tile.x < g_grid.x && tile.y < g_grid.y) {
        const float4 motion = g_motion.Load(int3(pixel, 0));
        const RtSurface s = rt_surface(pixel, guide, g_normals.Load(int3(pixel, 0)), motion.w);
        const uint2 run = g_tiles.Load2(g_glow.y + (tile.y * g_grid.x + tile.x) * 8u);
        const uint lights = run.y;
        float4 place;
        uint slot;
        {
            uint seed = rt_hash(id.x + id.y * 8192u + g_more.y * 9781u + 4099u);
            float sum = 0.0f;      // of the weights: what each would add here, over its chance of being tried
            float counted = 0.0f;  // the candidates behind what is combined
            uint kept = 0xFFFFFFFFu, kept_slot = 0u;
            float3 kept_light = float3(0.0f, 0.0f, 0.0f);
            float kept_target = 0.0f;
            float4 kept_at = float4(0.0f, 0.0f, 0.0f, 0.0f);
            // This frame's candidates.
            const uint tries = min(lights, level >= 2u ? 8u : 4u);
            const uint strong = lights > tries ? tries / 2u : 0u;  // the list's first, the tile's strongest: each for certain
            const float each = float(lights - strong) / float(max(tries - strong, 1u));  // what one of the others stands for
            const float tried = rt_sample(id.xy, g_more.y, kSampleLightTry).x;  // where in its run each candidate is
            for (uint k = 0u; k < tries; ++k) {
                seed = rt_hash(seed + k * 7919u + 1u);
                uint in_list = k;
                float stands = 1.0f;
                if (k >= strong) {
                    const float inside = lights <= tries ? 0.0f : frac(tried + float(k) * 0.618034f);
                    in_list = strong + min(uint((float(k - strong) + inside) * each), lights - strong - 1u);
                    stands = each;
                }
                const uint index = g_tiles.Load(g_glow.z + (run.x + in_list) * 4u);
                const float3 light = emitter_light(g_glow.x + index * 48u, s.at, s.smooth_, s.slot, place, slot);
                const float mine = dot(light, kLuminance);
                if (!(mine > 0.0f)) {
                    continue;
                }
                // Its weight: what it adds here by how many of the list it stands for, as a share of the candidates.
                const float w = mine * stands * float(tries);
                sum += w;
                seed = rt_hash(seed);
                if (rt_unit(seed) * sum < w) {
                    kept = index;
                    kept_light = light;
                    kept_target = mine;
                    kept_at = place;
                    kept_slot = slot;
                }
            }
            counted = float(tries);
            // The frame before's reservoirs.
            float3 others[3] = {float3(0.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 0.0f)};
            float others_count[3] = {0.0f, 0.0f, 0.0f};
            if (rt_history() && motion.z > 1.0f) {
                uint wide, high;
                g_reservoirs_before.GetDimensions(wide, high);
                const float away_before = motion.z;
                const float2 was = float2(id.xy) + 0.5f + motion.xy;
                const float3 before = rt_eye_ray(g_ray_before, was) * away_before;
                const float3 facing_before = float3(dot(g_before[0].xyz, s.smooth_), dot(g_before[1].xyz, s.smooth_), dot(g_before[2].xyz, s.smooth_));
                for (uint j = 0u; j < (level >= 2u ? 3u : 1u); ++j) {
                    float2 from_at = was;
                    if (j != 0u) {
                        seed = rt_hash(seed + 977u);
                        const float2 near_ = rt_sample(id.xy, g_more.y, kSampleLightNear + (j == 2u ? kSampleSecond : 0u));
                        const float turn = near_.x * 6.2831853f;
                        from_at += float2(cos(turn), sin(turn)) * (sqrt(near_.y) * 12.0f);
                    }
                    const int2 from = int2(floor(from_at));
                    if (from.x < 0 || from.y < 0 || from.x >= int(wide) || from.y >= int(high)) {
                        continue;
                    }
                    const float4 there = g_guide_before.Load(int3(from, 0));
                    if (there.a == 0.0f) {
                        continue;
                    }
                    const float3 place_before = rt_eye_ray(g_ray_before, float2(from) + 0.5f) * there.r;
                    if (abs(dot(facing_before, place_before - before)) > 0.02f * away_before + 2.0f ||
                        dot(facing_before, rt_unpack_normal(there.a)) < 0.85f) {
                        continue;
                    }
                    const float4 theirs = g_reservoirs_before.Load(int3(from, 0));
                    const float behind = min(theirs.a, 20.0f);
                    if (!(behind > 0.0f)) {
                        continue;
                    }
                    others[j] = place_before;  // in the frame before's eye space, as its lights were
                    others_count[j] = behind;
                    counted += behind;
                    const uint was_index = uint(theirs.r) + uint(theirs.g) * 1024u;
                    if (was_index == 0u || was_index > g_kept.y || !(theirs.b > 0.0f)) {
                        continue;  // it kept none, or one found in shadow: its candidates count, its light is not handed on
                    }
                    const uint index = g_drops.Load(g_kept.x + (was_index - 1u) * 4u);
                    if (index >= g_glow.w) {
                        continue;  // that light is gone
                    }
                    const float3 light = emitter_light(g_glow.x + index * 48u, s.at, s.smooth_, s.slot, place, slot);
                    const float mine = dot(light, kLuminance);
                    const float w = mine * theirs.b * behind;
                    if (!(w > 0.0f)) {
                        continue;
                    }
                    sum += w;
                    seed = rt_hash(seed);
                    if (rt_unit(seed) * sum < w) {
                        kept = index;
                        kept_light = light;
                        kept_target = mine;
                        kept_at = place;
                        kept_slot = slot;
                    }
                }
            }
            float weight = 0.0f;
            if (kept != 0xFFFFFFFFu) {
                // How many of the candidates could have chosen this light: this frame's, and each reservoir's whose
                // surface the light reached when it was written (the frame before's place and reach of the light: one
                // that moved in or is new was not among those tries, and counting them would darken it).
                const float4 was = asfloat(g_drops.Load4(g_kept.x + max(g_kept.y, 1u) * 4u + kept * 16u));
                float could = float(tries);
                for (uint j = 0u; j < 3u; ++j) {
                    const float3 to_it = was.xyz - others[j];
                    if (others_count[j] > 0.0f && dot(to_it, to_it) < was.w * was.w) {
                        could += others_count[j];
                    }
                }
                weight = min(sum / (max(could, 1.0f) * kept_target), 60000.0f);
                // Its shadow: one ray, stopping short of the source's own body. A source over 50 units across is aimed
                // at a point of its disc as the surface sees it, so its shadow is as soft as it is wide.
                float3 aim = kept_at.xyz;
                if (kept_at.w > 50.0f) {
                    const float3 axis = normalize(s.at - kept_at.xyz);
                    const float3 side = normalize(abs(axis.y) < 0.9f ? cross(axis, float3(0.0f, 1.0f, 0.0f)) : cross(axis, float3(1.0f, 0.0f, 0.0f)));
                    const float3 up_ = cross(axis, side);
                    const float2 u = rt_sample(id.xy, g_more.y, kSampleLightDisc);
                    const float turn = u.y * 6.2831853f;
                    aim += (side * cos(turn) + up_ * sin(turn)) * (sqrt(u.x) * kept_at.w);
                }
                const float3 to = aim - s.at;
                const float far_ = length(to);
                const float short_of = far_ - min(max(kept_at.w, 10.0f), 60.0f);
                uint met;
                if (short_of > 1.0f && blocked(s.origin, to / max(far_, 1e-3f), short_of, kMaskSolid, false, met) &&
                    g_items.Load(g_at.x + met * kItemBytes) != kept_slot) {
                    weight = 0.0f;
                }
                // Far over what a picture shows is cut: the filter's moments stay finite.
                added = min(kept_light * weight, float3(32.0f, 32.0f, 32.0f));
                // Its highlight on gloss, metal and glass: D_ggx(n.h) F(v.h) / (4 n.v) of the light arriving (the kept
                // light without its wrap of facing), by the same weight.
                const float4 material = g_material.Load(int3(pixel, 0));
                const uint class_ = uint(material.b * 255.0f + 0.5f) & 15u;
                if (weight > 0.0f && level >= 3u && (class_ == kClassGloss || class_ == kClassMetal || class_ == kClassGlass ||
                                                        class_ == kClassWater)) {
                    const float3 l = (kept_at.xyz - s.at) / max(length(kept_at.xyz - s.at), 1e-3f);
                    const float3 v = -normalize(s.at);
                    const float nl = dot(s.smooth_, l), nv = dot(s.smooth_, v);
                    if (nl > 0.0f && nv > 0.0f) {
                        const float3 h = normalize(l + v);
                        const float nh = saturate(dot(s.smooth_, h)), vh = saturate(dot(v, h));
                        const float a = saturate(max(material.r * material.r, 0.002f) + kept_at.w / (2.0f * far_));
                        const float a2 = a * a;
                        const float lobe = nh * nh * (a2 - 1.0f) + 1.0f;
                        const float d = a2 / (3.14159265f * lobe * lobe);
                        const float f0 = class_ == kClassMetal ? 0.55f : class_ == kClassWater ? 0.02f : 0.04f;
                        const float glance = 1.0f - vh;
                        const float f = f0 + (1.0f - f0) * glance * glance * glance * glance * glance;
                        const float3 arriving = kept_light / (0.25f + 0.75f * saturate(nl));
                        const float3 spec = arriving * (d * f * 0.25f / max(nv, 0.05f)) * weight;
                        g_lights_spec[id.xy] = float4(min(spec, float3(32.0f, 32.0f, 32.0f)), 0.0f);
                    }
                }
            }
            g_reservoirs[id.xy] = float4(float((kept + 1u) & 1023u), float((kept + 1u) >> 10u), weight, min(counted, 20.0f));
        }
    }
    g_lights[id.xy] = float4(added.r, 0.0f, added.g, added.b);
}
