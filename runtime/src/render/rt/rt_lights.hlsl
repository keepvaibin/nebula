// SPDX-License-Identifier: GPL-3.0-only
// The local lights pass: every emitter (the game's point lights, glowing meshes, each particle of a glowing effect) at
// each pixel's surface, by spatiotemporal reservoir resampling (ReSTIR DI: Bitterli et al. 2020; resampled importance
// sampling: Talbot et al. 2005). A pixel keeps ONE light, chosen in proportion to what each would add here unshadowed,
// out of:
//   this frame's candidates: up to eight of the tile's lights, one from each equal run of its list;
//   the reservoirs of the frame before at the pixel's own point (found by its motion) and at two points near it, where
//   the surface there is the pixel's own kind (in its plane, facing its way): each stands for all the candidates it
//   was itself chosen among, so a pixel sees what hundreds of tries would find.
// ONE ray then asks whether the kept light reaches the surface, and what is written is its light times its weight: the
// sum of the weights it was chosen among, over its own and over how many of the candidates could have chosen it (those
// whose surface it reaches: without that share a light's edge darkens). On average the sum of every light with its
// shadow, at a cost that does not grow with the lights. A light found in shadow is kept with no weight, so it is not
// handed on. The filter after the pass (rt_lights_time, rt_lights_space) takes what noise is left. Not on itself.
// The second layer (g_pass.x 1) has no filter after it: there up to 32 of the lights are summed as they are, unshadowed,
// straight into the picture the programs read.
#include "rt_common.hlsli"

Texture2D<float4> g_guide : register(t9);
Texture2D<float4> g_normals : register(t10);
Texture2D<float4> g_motion : register(t11);
Texture2D<float4> g_reservoirs_before : register(t12);  // the frame before's
Texture2D<float4> g_guide_before : register(t13);       // and its guide: what surface each of those pixels was
// The opaque layer: red, green, blue in r, b, a (the channels the filter averages); the second layer: rgb as the
// programs read them.
RT_FORMAT(rgba16f) RWTexture2D<float4> g_lights : register(u0);
// The pixel's reservoir: r, g the kept light's number in this frame's list, from 1, in two parts (each whole in a half
// float; 0: none); b its weight (what its unshadowed light is multiplied by; 0 where it was found in shadow); a how
// many candidates stand behind it (no more than 20).
RT_FORMAT(rgba16f) RWTexture2D<float4> g_reservoirs : register(u1);

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
    if (!second) {
        g_reservoirs[id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);  // none, until the lights say otherwise
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
        if (second) {
            const uint tries = min(lights, 32u);
            const float each = float(lights) / float(max(tries, 1u));
            for (uint k = 0u; k < tries; ++k) {
                const uint index = g_tiles.Load(g_glow.z + (run.x + min(uint(float(k) * each), lights - 1u)) * 4u);
                added += emitter_light(g_glow.x + index * 48u, s.at, s.smooth_, s.slot, place, slot) * each;
            }
        } else {
            uint seed = rt_hash(id.x + id.y * 8192u + g_more.y * 9781u + 4099u);
            float sum = 0.0f;      // of the weights: what each would add here, over its chance of being tried
            float counted = 0.0f;  // the candidates behind what is combined
            uint kept = 0xFFFFFFFFu, kept_slot = 0u;
            float3 kept_light = float3(0.0f, 0.0f, 0.0f);
            float kept_target = 0.0f;
            float4 kept_at = float4(0.0f, 0.0f, 0.0f, 0.0f);
            // This frame's candidates.
            const uint tries = min(lights, 8u);
            const float each = float(lights) / float(max(tries, 1u));
            for (uint k = 0u; k < tries; ++k) {
                seed = rt_hash(seed + k * 7919u + 1u);
                const float inside = lights <= tries ? 0.0f : rt_unit(seed);
                const uint index = g_tiles.Load(g_glow.z + (run.x + min(uint((float(k) + inside) * each), lights - 1u)) * 4u);
                const float3 light = emitter_light(g_glow.x + index * 48u, s.at, s.smooth_, s.slot, place, slot);
                const float mine = dot(light, kLuminance);
                if (!(mine > 0.0f)) {
                    continue;
                }
                const float w = mine * float(lights);  // a light is tried with the chance of one in `lights`
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
            if (g_kept.z != 0u && motion.z > 1.0f) {
                uint wide, high;
                g_reservoirs_before.GetDimensions(wide, high);
                const float away_before = motion.z;
                const float2 was = float2(id.xy) + 0.5f + motion.xy;
                const float3 before = rt_eye_ray(g_ray_before, was) * away_before;
                const float3 facing_before = float3(dot(g_before[0].xyz, s.smooth_), dot(g_before[1].xyz, s.smooth_), dot(g_before[2].xyz, s.smooth_));
                for (uint j = 0u; j < 3u; ++j) {
                    float2 from_at = was;
                    if (j != 0u) {
                        seed = rt_hash(seed + 977u);
                        const float turn = float(seed & 0xFFFFu) / 65536.0f * 6.2831853f;
                        from_at += float2(cos(turn), sin(turn)) * (sqrt(float(seed >> 16u) / 65536.0f) * 12.0f);
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
                // Its shadow: one ray, stopping short of the source's own body.
                const float3 to = kept_at.xyz - s.at;
                const float far_ = length(to);
                const float short_of = far_ - min(max(kept_at.w, 10.0f), 60.0f);
                uint met;
                if (short_of > 1.0f && blocked(s.origin, to / max(far_, 1e-3f), short_of, kMaskSolid, false, met) &&
                    g_items.Load(g_at.x + met * kItemBytes) != kept_slot) {
                    weight = 0.0f;
                }
                // Far over what a picture shows is cut: the filter's moments stay finite.
                added = min(kept_light * weight, float3(32.0f, 32.0f, 32.0f));
            }
            g_reservoirs[id.xy] = float4(float((kept + 1u) & 1023u), float((kept + 1u) >> 10u), weight, min(counted, 20.0f));
        }
    }
    g_lights[id.xy] = second ? float4(added, 0.0f) : float4(added.r, 0.0f, added.g, added.b);
}
