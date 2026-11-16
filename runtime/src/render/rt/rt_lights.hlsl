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
// The kept light also gives its highlight on every lit surface (the shared model's GGX lobe; widened by the source's
// size as it is seen, Karis 2013), by the same weight: LightsSpec, which the reflection's raw image takes, over the
// pixel's specular albedo, before its filter.
// The second layer (g_pass.x 1: the nearest blended surface a pixel) is done the same way, with its own reservoirs; its
// filter is the lights' time pass and one step over space.
#include "rt_common.hlsli"
#include "rt_mirror.hlsli"  // mirror_material: the pixel's material on the shared model

Texture2D<float4> g_guide : register(t9);
Texture2D<float4> g_normals : register(t10);
Texture2D<float4> g_motion : register(t11);
Texture2D<float4> g_reservoirs_before : register(t12);  // the frame before's
Texture2D<float4> g_guide_before : register(t13);       // and its guide: what surface each of those pixels was
Texture2D<float4> g_material : register(t14);           // S.material: r roughness, g metal, b class and flags
Texture2D<float4> g_base : register(t15);               // S.base: a metal's F0 colour
// The opaque layer: red, green, blue in r, b, a (the channels the filter averages); the second layer: rgb as the
// programs read them.
RT_FORMAT(rgba16f) RWTexture2D<float4> g_lights : register(u0);
// The pixel's reservoir: r, g the kept light's number in this frame's list, from 1, in two parts (each whole in a half
// float; 0: none); b its weight (what its unshadowed light is multiplied by; 0 where it was found in shadow); a how
// many candidates stand behind it (no more than 20).
RT_FORMAT(rgba16f) RWTexture2D<float4> g_reservoirs : register(u1);
// The kept light's highlight, rgb.
RT_FORMAT(rgba16f) RWTexture2D<float4> g_lights_spec : register(u2);

// The candidates a reservoir carries at most, at 60 frames a second (times rt_history_scale).
static const float kMostLightM = 20.0f;

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
    // A tile no light reaches (the host bins each light into every tile its reach covers) keeps no reservoir: nothing
    // the frame before kept could light it, and a reservoir with candidates but no light would only stand in Z below.
    const uint2 run = guide.a != 0.0f && g_glow.w != 0u && tile.x < g_grid.x && tile.y < g_grid.y
                          ? g_tiles.Load2(g_glow.y + (tile.y * g_grid.x + tile.x) * 8u)
                          : uint2(0u, 0u);
    if (run.y != 0u) {
        const float4 motion = g_motion.Load(int3(pixel, 0));
        const RtSurface s = rt_surface(pixel, guide, g_normals.Load(int3(pixel, 0)), motion.w);
        const uint lights = run.y;
        // Not on itself: a light adds nothing to its own body, the parts of its model that glow this frame (item word 14
        // bit 25); the model's other parts take its light. A pixel whose item is not known keeps its slot's rule.
        const uint here = g_item_map.Load(int3(pixel, 0)).x;
        const uint self = here == 0u || (g_items.Load(g_at.x + (here - 1u) * kItemBytes + 56u) >> 25u & 1u) != 0u ? s.slot : 0xFFFFFFFFu;
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
                const float3 light = emitter_light(g_glow.x + index * 48u, s.at, s.smooth_, self, place, slot);
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
            float3 others_normal[3] = {float3(0.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 0.0f)};
            float others_count[3] = {0.0f, 0.0f, 0.0f};
            // The light (this frame's number) a reservoir found in shadow at its own surface: it cannot have handed
            // that one light on, so it does not stand in Z for it (for any other light it does).
            uint others_dark[3] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
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
                    const float behind = min(theirs.a, kMostLightM * rt_history_scale());
                    if (!(behind > 0.0f)) {
                        continue;
                    }
                    others[j] = place_before;  // in the frame before's eye space, as its lights were
                    others_normal[j] = rt_unpack_normal(there.a);
                    others_count[j] = behind;
                    counted += behind;
                    const uint was_index = uint(theirs.r) + uint(theirs.g) * 1024u;
                    if (was_index == 0u || was_index > g_kept.y) {
                        continue;  // it kept none: its candidates count
                    }
                    const uint index = g_drops.Load(g_kept.x + (was_index - 1u) * 4u);
                    if (index >= g_glow.w) {
                        continue;  // that light is gone
                    }
                    if (!(theirs.b > 0.0f)) {
                        // Found in shadow there (visibility reuse: not handed on): that light's p_hat V was 0 at
                        // that surface, so for it alone the reservoir is not among those that could choose it (Z).
                        others_dark[j] = index;
                        continue;
                    }
                    const float3 light = emitter_light(g_glow.x + index * 48u, s.at, s.smooth_, self, place, slot);
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
                // How many of the candidates could have chosen this light (Bitterli et al. 2020's Z: the inputs whose
                // own target is not 0 there): this frame's, and each reservoir's whose surface the light reached and
                // faced when it was written (the frame before's place and reach of the light: one that moved in or is
                // new was not among those tries, and counting them would darken it), unless that surface found it in
                // shadow.
                float could = float(tries);
                if (others_count[0] + others_count[1] + others_count[2] > 0.0f) {
                    const float4 was = asfloat(g_drops.Load4(g_kept.x + max(g_kept.y, 1u) * 4u + kept * 16u));
                    for (uint j = 0u; j < 3u; ++j) {
                        const float3 to_it = was.xyz - others[j];
                        if (others_count[j] > 0.0f && others_dark[j] != kept && dot(to_it, to_it) < was.w * was.w &&
                            dot(others_normal[j], to_it) > 0.0f) {
                            could += others_count[j];
                        }
                    }
                }
                weight = min(sum / (max(could, 1.0f) * kept_target), 60000.0f);
                // Its shadow: one ray, stopping short of the source's own body. A source over 50 units across is aimed
                // at a point of its disc as the surface sees it, so its shadow is as soft as it is wide.
                float3 aim = kept_at.xyz;
                const float3 off = s.at - kept_at.xyz;
                if (kept_at.w > 50.0f && dot(off, off) > 1.0f) {  // (a surface at the centre: no disc to turn)
                    const float3 axis = off * rsqrt(dot(off, off));
                    const float3 side = normalize(abs(axis.y) < 0.9f ? cross(axis, float3(0.0f, 1.0f, 0.0f)) : cross(axis, float3(1.0f, 0.0f, 0.0f)));
                    const float3 up_ = cross(axis, side);
                    const float2 u = rt_sample(id.xy, g_more.y, kSampleLightDisc);
                    const float turn = u.y * 6.2831853f;
                    aim += (side * cos(turn) + up_ * sin(turn)) * (sqrt(u.x) * kept_at.w);
                }
                const float3 to = aim - s.at;
                const float far_ = length(to);
                const float short_of = far_ - min(max(kept_at.w, 10.0f), 60.0f);
                // Cutouts too: a railing, a card, leaves cast the lamp's shadow, their holes not.
                // The light's own body (its model's glowing parts) is passed over, the model's other parts shadow beyond
                // the light's radius (inside it they are the source itself).
                if (short_of > 1.0f &&
                    blocked_but(s.origin, to / max(far_, 1e-3f), short_of, kMaskSolid | kMaskCutout | kMaskShadowOnly, kept_slot, true,
                                kept_at)) {
                    weight = 0.0f;
                }
                // The camera under water (g_more.z bit 12): what it sees lies in the water, and a lamp's light reaches it
                // through as much water as the way from the lamp (rt_water_absorb, as the sun's rays under water take).
                if ((g_more.z & 4096u) != 0u) {
                    kept_light *= rt_water_absorb(far_);
                }
                // Far over what a picture shows is cut: the filter's moments stay finite.
                added = min(kept_light * weight, float3(32.0f, 32.0f, 32.0f));
                // Its highlight: the kept light's GGX lobe on the pixel's material (rt_mirror.hlsli mirror_material, the
                // shared model's shade_highlight: every lit class has its lobe at its roughness, F0 0.04 for a plain
                // dielectric), by the same weight; the lobe widened by the source's size as it is seen (Karis 2013).
                // The light on a face square to it is kept_light over its facing. A glowing mesh (RtEmitter kind 1: a
                // rail, a fence) a mirror's ray meets and shows sharp on a smooth surface; on a rough one that ray seldom
                // finds a thin lamp, so the light's sample carries it, from none at roughness 0.05 to all at 0.2.
                const float4 material = g_material.Load(int3(pixel, 0));
                const uint bits = uint(material.b * 255.0f + 0.5f);
                const bool mesh_lamp = kept < g_glow.w && g_drops.Load(g_glow.x + kept * 48u + 36u) == 1u;
                const float lamp_share = mesh_lamp ? smoothstep(0.05f, 0.2f, material.r) : 1.0f;
                const float3 l = (kept_at.xyz - s.at) / max(length(kept_at.xyz - s.at), 1e-3f);
                const float facing_l = dot(s.smooth_, l);
                if (weight > 0.0f && level >= 3u && lamp_share > 0.0f && mirror_lit(bits) && facing_l > 1e-3f) {
                    const uint item_ = g_item_map.Load(int3(pixel, 0)).x;
                    const uint word = item_ != 0u ? g_items.Load(g_at.x + (item_ - 1u) * kItemBytes + 60u) : 0u;
                    const ShadeMaterial m = mirror_material(bits, material.r, material.g, g_base.Load(int3(pixel, 0)).rgb, word);
                    const float3 v = -normalize(s.at);
                    const ShadeLobes lobes = shade_lobes(m, max(dot(s.smooth_, v), kShadeNvLeast));
                    const float3 spec = shade_highlight(m, lobes, s.smooth_, v, l, kept_at.w / max(far_, 1e-3f)) *
                                        (kept_light / facing_l) * (weight * lamp_share);
                    g_lights_spec[id.xy] = float4(min(spec, float3(32.0f, 32.0f, 32.0f)), 0.0f);
                }
            }
            g_reservoirs[id.xy] = float4(float((kept + 1u) & 1023u), float((kept + 1u) >> 10u), weight, min(counted, kMostLightM * rt_history_scale()));
        }
    }
    g_lights[id.xy] = float4(added.r, 0.0f, added.g, added.b);
}
