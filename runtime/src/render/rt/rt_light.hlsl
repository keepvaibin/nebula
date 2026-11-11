// SPDX-License-Identifier: GPL-3.0-only
// The light pass: the shares the programs read, from the sun's V (filtered by rt_sun_time and rt_sun_blur) with the
// sky's light near the surface, and what glass, metal and gloss mirror. Until the sky and bounce light pass
// (rt_indirect) and the reflection pass (rt_reflect) replace them, the sky's part is two alternating occlusion rays
// and the mirror a first cut. The shares the programs read (program.hlsli traced_colour, program_lit.hlsli):
//   r  the sun's share on a lit surface (its program sums the key by it): V;
//   g  the fill's: all of it;
//   b  0: every point light is an emitter, lit through the added light (rt_lights.hlsl);
//   a  the ground's share on every surface that takes shadows: a painted surface's own shadow ratio where the sun's
//      shadow falls (its painting holds the sun's facing: from g_look.z of full facing it keeps all of its colour,
//      g_look.x where the sun leaves it), times what the sky's light keeps where something stands near or over it.
// The opaque layer's sky rays are noisy and V is not: this pass writes, for the filter (rt_light_time,
// rt_light_space), r V, b how the surface faces the sun (0 to 1 a painted surface's sunlit share at full V; -1 to
// -2 a lit surface's, -1 - n.l) and a the sky's share; the filter averages a alone and its last step puts the
// shares together. The second layer has no filter: there they are put together here.
#include "rt_common.hlsli"

Texture2D<float4> g_guide : register(t9);
Texture2D<float4> g_normals : register(t10);
Texture2D<float4> g_motion : register(t11);
Texture2D<float4> g_sun_v : register(t12);     // rgb V: filtered on the opaque layer, raw on the second
Texture2D<float4> g_material : register(t13);
RT_FORMAT(rgba16f) RWTexture2D<float4> g_out : register(u0);
// What the pixel mirrors: rgb what its mirror ray sees by how much the surface mirrors; a how much of the surface's own
// colour stays under it, plus 2 in the second layer (program.hlsli traced_mirror).
RT_FORMAT(rgba16f) RWTexture2D<float4> g_mirror : register(u1);

// What a ray sees: the colour of the nearest thing on it, as the scene's light leaves it. The scene's solid things
// and its cutouts where their maps say they are, else the sky's dome, else g_sky. A thing shows its first map at the
// place met (the level of the map by how wide the ray has grown: `wide` at its start, `widens` a unit of way), or its
// material's colour; under the sun where the sun reaches the place met, else what a shadow keeps; the sky and what
// shows itself without light as they are.
float3 seen_along(float3 origin, float3 direction, float wide, float widens) {
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0f;
    ray.TMax = 4000000.0f;
    RayQuery<RAY_FLAG_NONE> query;
    query.TraceRayInline(g_scene, RAY_FLAG_NONE, kMaskSolid | kMaskCutout | kMaskSky, ray);
    while (query.Proceed()) {
        if (query.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
            cutout_there(query.CandidateInstanceID(), query.CandidatePrimitiveIndex(), query.CandidateTriangleBarycentrics())) {
            query.CommitNonOpaqueTriangleHit();
        }
    }
    InterlockedAdd(g_counters[23], 1u);
    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT) {
        return g_sky.rgb;
    }
    const uint at = g_at.x + query.CommittedInstanceID() * kItemBytes;
    const uint4 first = g_items.Load4(at), second = g_items.Load4(at + 16u), third = g_items.Load4(at + 32u);
    const uint base_word = g_items.Load(at + 48u);
    const uint class_ = third.w & 255u;
    const bool sky = (first.y & kItemSky) != 0u;
    float3 colour = float3(float(base_word & 255u), float(base_word >> 8u & 255u), float(base_word >> 16u & 255u)) / 255.0f;
    float3 facing = -direction;
    const uint arena = first.z;
    if (arena < 8u) {
        const uint base = mesh_at(arena);
        const uint3 corner = triangle_corners(first, second, query.CommittedPrimitiveIndex());
        float3 place[3];
        float2 coordinate[3];
        for (uint c = 0u; c < 3u; ++c) {
            const uint row = vertex_rows(arena, base, second.w, third.z, corner[c]);
            const float4 r0 = asfloat(g_rows.Load4(row)), r1 = asfloat(g_rows.Load4(row + 16u)), r2 = asfloat(g_rows.Load4(row + 32u));
            const float4 p = float4(asfloat(arena_words(arena, base + second.z + corner[c] * 12u)), 1.0f);
            place[c] = float3(dot(r0, p), dot(r1, p), dot(r2, p));
            coordinate[c] = asfloat(arena_words(arena, base + first.w + corner[c] * 24u + 12u).yz);
        }
        const float3 across = cross(place[1] - place[0], place[2] - place[0]);
        const float area = length(across);
        if (area > 1e-9f) {
            const float3 n = across / area;
            facing = dot(n, direction) < 0.0f ? n : -n;
        }
        if (third.x != 0xFFFFFFFFu) {
            const float2 where_ = query.CommittedTriangleBarycentrics();
            const float2 uv = coordinate[0] * (1.0f - where_.x - where_.y) + coordinate[1] * where_.x + coordinate[2] * where_.y;
            // The map's level: how many of its texels the ray's width covers at the place met.
            uint map_width, map_height;
            g_textures[NonUniformResourceIndex(third.x)].GetDimensions(map_width, map_height);
            const float2 e1 = coordinate[1] - coordinate[0], e2 = coordinate[2] - coordinate[0];
            const float texels = abs(e1.x * e2.y - e1.y * e2.x) * float(map_width) * float(map_height);
            const float across_ray = wide + widens * query.CommittedRayT();
            const float level = 0.5f * log2(max(texels / max(area, 1e-9f) * across_ray * across_ray, 1.0f));
            colour = g_textures[NonUniformResourceIndex(third.x)].SampleLevel(g_wrap, uv, level).rgb;
        }
    }
    if (sky || class_ == kClassEmissive) {
        return colour;
    }
    // Its light: the sun where it reaches the place met, as a painted surface takes it (rt_sun.hlsl).
    float sunlit = 1.0f;
    if ((g_more.z & 1u) != 0u) {
        const float lambert = dot(facing, g_sun.xyz);
        sunlit = 0.0f;
        if (lambert > 0.0f) {
            const float3 met_at = origin + direction * query.CommittedRayT();
            uint met;
            if (!blocked(met_at + facing * max(0.002f * query.CommittedRayT(), 0.5f), g_sun.xyz, 200000.0f, kMaskSolid | kMaskCutout, true, met)) {
                sunlit = saturate(lambert / g_look.z);
            }
        }
    }
    return colour * lerp(g_look.x, 1.0f, sunlit);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint width, height;
    g_out.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) {
        return;
    }
    const int2 pixel = int2(id.xy);
    const bool second = rt_second_layer();
    const uint item = g_item_map.Load(int3(pixel, 0)).x;
    if (second && item == 0u) {
        return;  // no blended surface here: the picture keeps the opaque layer's light
    }
    float4 share = float4(1.0f, 1.0f, 1.0f, 1.0f);
    float4 mirror = float4(0.0f, 0.0f, 0.0f, 1.0f);
    const float4 guide = g_guide.Load(int3(pixel, 0));
    if (guide.a != 0.0f) {
        const RtSurface s = rt_surface(pixel, guide, g_normals.Load(int3(pixel, 0)), g_motion.Load(int3(pixel, 0)).w);
        const float3 v3 = g_sun_v.Load(int3(pixel, 0)).rgb;
        const float v = (v3.r + v3.g + v3.b) * (1.0f / 3.0f);
        // How the surface faces the sun: a lit program sums its key by n.l itself (its share is V); a painted surface
        // keeps all of its colour from g_look.z of full facing up. No sun: all of it.
        const uint flags = uint(g_material.Load(int3(pixel, 0)).b * 255.0f + 0.5f);
        const bool lit = (flags & kSurfaceLit) != 0u;
        float facing = 1.0f;
        if ((g_more.z & 1u) != 0u) {
            const float turned = dot(s.smooth_, g_sun.xyz);
            const float lambert = saturate((flags & kSurfaceTwoSided) != 0u ? abs(turned) : turned);
            facing = lit ? lambert : saturate(lambert / g_look.z);
        }
        const float in_sun = v * facing;  // how much of the sun's light the surface takes
        // The lamp of the pixel's light set: every point light is an emitter (bit 6), lit through the added light.
        share.b = 0.0f;
        share.r = v;

        // The sky's light. A frame takes one of two rays (the average over time holds both): over the half-space the
        // surface faces, short: what stands close by hides that much of the sky (corners, the foot of a wall); or toward
        // the sky itself, about straight up, long: what stands over the surface hides the sky's brightest part, the soft
        // shadow everything has on an overcast day and in a sunny day's shade. So a thing the sun does not reach still
        // stands on a shadow of its own. Not in the second layer.
        float seen = 1.0f;
        if (!second && (g_more.z & 8u) != 0u && g_look.w > 0.0f) {
            const float2 chance = rt_sample(id.xy, g_more.y, kSampleSky);
            if (rt_sample(id.xy, g_more.y, kSampleSky + kSampleSecond).x < 0.5f) {
                const float open = clear_to(s.origin, around(s.smooth_, chance), g_look.w) / g_look.w;
                seen = lerp(0.5f, 1.0f, open * (2.0f - open));
            } else {
                const float3 over = within(g_up.xyz, 0.6f, chance);
                seen = clear_to(s.origin, over, 1200.0f) < 1200.0f ? 0.42f : 1.0f;  // what stands within 12 m over it
            }
        }
        if (second) {
            // In the sun the sky's part of the light is small.
            const float painted = lit ? 1.0f : lerp(g_look.x, 1.0f, in_sun);
            share.a = painted * lerp(seen, 1.0f, 0.65f * in_sun);
        } else {
            share.b = lit ? -1.0f - facing : facing;  // for the filter's last step (rt_light_space.hlsl)
            share.a = seen;
        }

        // What the surface mirrors: glass, metal and glossy things show the scene and the sky along the eye's ray turned
        // at the surface, more at a glancing look (Schlick's term), a metal in its own colour; and the sun's own glint
        // where that ray runs at the sun unhindered.
        if ((g_more.z & 32u) != 0u) {
            const uint look = g_items.Load(g_at.x + (item - 1u) * kItemBytes + 44u);
            const uint tint_word = g_items.Load(g_at.x + (item - 1u) * kItemBytes + 48u);
            const uint class_ = look & 255u;
            const float rough = float(look >> 16u & 255u) / 255.0f;
            float head_on = 0.0f;  // how much it mirrors seen head on
            float3 tint = float3(1.0f, 1.0f, 1.0f);
            if (class_ == kClassGlass) {
                head_on = 0.1f;
            } else if (class_ == kClassMetal) {
                head_on = 0.55f;
                tint = lerp(float3(1.0f, 1.0f, 1.0f), float3(float(tint_word & 255u), float(tint_word >> 8u & 255u), float(tint_word >> 16u & 255u)) / 255.0f, 0.5f);
            } else if (class_ == kClassGloss) {
                head_on = 0.05f * (1.0f - rough);
            }
            if (head_on > 0.0f) {
                const float3 look_along = normalize(s.at);
                const float3 n = dot(s.smooth_, look_along) < 0.0f ? s.smooth_ : s.normal;
                const float glance = 1.0f - saturate(-dot(n, look_along));
                const float share_ = head_on + (1.0f - head_on) * glance * glance * glance * glance * glance;
                const float3 turned_ray = reflect(look_along, n);
                const float pixel_wide = abs(g_ray.x);  // a pixel's width a unit of way from the eye
                float3 seen_ = seen_along(s.at + n * s.skin, turned_ray, s.away * pixel_wide, pixel_wide);
                uint met;
                if ((g_more.z & 1u) != 0u && dot(turned_ray, g_sun.xyz) > 0.9995f &&
                    !blocked(s.at + n * s.skin, g_sun.xyz, 200000.0f, kMaskSolid | kMaskCutout, true, met)) {
                    seen_ += float3(4.0f, 3.8f, 3.4f);  // the sun itself, far brighter than the picture's white
                }
                mirror.rgb = seen_ * tint * share_;
                // The painted reflection it was drawn with gives way to the real one: that much less of the colour under
                // the mirror (a blended surface: of its alpha, so glass clears).
                mirror.a = 1.0f - 0.6f * float(tint_word >> 24u) / 255.0f;
            }
        }
    }
    if ((g_more.z & 128u) != 0u && item != 0u && guide.a != 0.0f) {
        share = float4(0.0f, 0.0f, 0.0f, 0.0f);  // the emitters alone (the filter's last step says so too)
    }
    g_out[id.xy] = share;
    g_mirror[id.xy] = float4(mirror.rgb, mirror.a + (second ? 2.0f : 0.0f));
}
