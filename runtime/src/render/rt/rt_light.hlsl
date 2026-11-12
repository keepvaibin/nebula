// SPDX-License-Identifier: GPL-3.0-only
// The light pass: the shares the programs read, from the sun's V (filtered by rt_sun_time and rt_sun_blur), and what
// glass, metal and gloss mirror where the reflection pass (rt_reflect) does not run (a first cut). The sky's share O
// is the sky and bounce light pass's (rt_indirect; rt_light_space.hlsl's last step reads it); where that part is off,
// 1 (the authored ambient; screen-space occlusion runs then). What the programs read (program.hlsli traced_colour,
// program_lit.hlsli), both layers:
//   Light   rgb V, the sun's share as a colour (what blended things on its ray let through tints it); a O, the sky's
//           share (what stands near or over the surface hides of it). The programs put sun, sky, fill and the
//           stage's colours together themselves (plan 3.4); every point light is an emitter (the added light);
//   Facing  r how the surface faces the sun as a painted surface takes it (from g_look.z of full facing up: all of
//           it); a lit program takes n.l itself. For the Physical style (FB 22.19, plan 14.6): g max(n.s, 0), b
//           max(n.d, 0) with d the light the mesh's paint holds (item word 16), a its share (0: it stays painted).
// For the opaque layer this pass writes, for the filter (rt_light_time, rt_light_space), r V (one number), b the facing
// code (0 to 1 painted; -1 to -2 lit, -1 - n.l) and a 1; the filter's last step writes V (rgb, from the sun's filter)
// with the sky's share. The second layer's are written here (O 1).
#include "rt_common.hlsli"
#include "rt_mirror.hlsli"  // mirror_terms: the share a rough surface mirrors over its lobe (FB b47)

Texture2D<float4> g_guide : register(t9);
Texture2D<float4> g_normals : register(t10);
Texture2D<float4> g_motion : register(t11);
Texture2D<float4> g_sun_v : register(t12);     // rgb V: filtered on the opaque layer, raw on the second
Texture2D<float4> g_material : register(t13);
Texture2D<float4> g_lights_spec : register(t14);  // the local lights' highlights (rt_lights.hlsl), the opaque layer
RT_FORMAT(rgba16f) RWTexture2D<float4> g_out : register(u0);
// What the pixel mirrors: rgb what its mirror ray sees by how much the surface mirrors; a how much of the surface's own
// colour stays under it, plus 2 in the second layer (program.hlsli traced_mirror).
RT_FORMAT(rgba16f) RWTexture2D<float4> g_mirror : register(u1);
RT_FORMAT(rgba8) RWTexture2D<float4> g_facing : register(u2);  // how a painted surface faces the sun, and its paint light
RT_FORMAT(r8) RWTexture2D<unorm float> g_body : register(u3);  // the second layer's body (glass and water, FC's c16)
Texture2D<float4> g_guide_opaque : register(t15);  // the opaque layer's guide: under water, how deep it is

// Glass and water on the second layer (plan 13.2, 13.3; FC's c16): the Schlick reflectance of the material's ior times
// its shine, at most 1, and its body. Item word 15 (FC's c15): ior bits 12-19 as (ior - 1) * 100, shine bits 20-25 as
// shine * 4 (63: the class's own), body bits 26-31 as body * 62 (63: the class's own); 0 when nothing wrote it.
// Water seen from inside (the camera under water, g_more.z bit 12): the index inverted, all mirrored past the critical
// angle, Schlick by the angle out in the air; no body (the water before the surface is the game's camera filter; past
// it lies air).
struct ClearSurface {
    float f;     // how much of R it shows
    float body;  // how much of its own colour covers what lies behind
};

ClearSurface clear_surface(uint item, uint class_, float facing_eye, float depth) {
    const uint word = g_items.Load(g_at.x + (item - 1u) * kItemBytes + 60u);
    const bool water = class_ == kClassWater;
    const uint ior_bits = word >> 12u & 255u, shine_bits = word >> 20u & 63u, body_bits = word >> 26u;
    const bool written = ior_bits != 0u;
    const float ior = written ? 1.0f + float(ior_bits) / 100.0f : (water ? 1.33f : 1.5f);
    // Schlick's share of the index (R2; FB b61): glass 1, water the owner's 1.2 (dev keys glass_shine, water_shine).
    const float shine = written && shine_bits != 63u ? float(shine_bits) / 4.0f
                                                     : (water ? rt_dev_or(kDevWaterShine, 1.2f) : rt_dev_or(kDevGlassShine, 1.0f));
    const float f0 = (ior - 1.0f) * (ior - 1.0f) / ((ior + 1.0f) * (ior + 1.0f));
    const float cos_in = saturate(facing_eye);
    ClearSurface c;
    if (water && (g_more.z & 4096u) != 0u) {
        const float sin_out2 = ior * ior * (1.0f - cos_in * cos_in);
        const float away_out = sin_out2 >= 1.0f ? 1.0f : 1.0f - sqrt(1.0f - sin_out2);
        c.f = sin_out2 >= 1.0f ? 1.0f : min(shine * (f0 + (1.0f - f0) * away_out * away_out * away_out * away_out * away_out), 1.0f);
        c.body = 0.0f;
        return c;
    }
    const float away = 1.0f - cos_in;
    c.f = min(shine * (f0 + (1.0f - f0) * away * away * away * away * away), 1.0f);
    // A clear thing's colour is light it takes away (R1; FB b61): where the transmit pass gives T (High; not the
    // emitters-alone or motion pictures) no body at all, the way through it is in T; else water by its depth, glass
    // none unless its hit record writes one.
    const bool traced_t = rt_part(kPartTransmit) >= 3u && (g_more.z & (128u | 512u)) == 0u;
    if (traced_t) {
        c.body = 0.0f;
    } else if (water) {
        c.body = depth > 0.0f ? 1.0f - exp(-depth / rt_water_clear()) : 1.0f;  // the stage's water clear (b58)
    } else {
        c.body = written && body_bits != 63u ? float(body_bits) / 62.0f : 0.0f;
    }
    return c;
}

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
            cutout_there(query.CandidateInstanceID(), query.CandidatePrimitiveIndex(), query.CandidateTriangleBarycentrics(),
                         ray.Origin + ray.Direction * query.CandidateTriangleRayT())) {
            query.CommitNonOpaqueTriangleHit();
        }
    }
    InterlockedAdd(g_counters[23], 1u);
    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT) {
        return rt_lin(g_sky.rgb);  // linear, as the programs read the mirror picture (M1; FB b59b)
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
        return rt_lin(colour);
    }
    // Its light: the sun where it reaches the place met, as a painted surface takes it (rt_sun.hlsl).
    float sunlit = 1.0f;
    if ((g_more.z & 1u) != 0u) {
        const float lambert = dot(facing, g_sun.xyz);
        sunlit = 0.0f;
        if (lambert > 0.0f) {
            const float3 met_at = origin + direction * query.CommittedRayT();
            uint met;
            if (!blocked(met_at + facing * max(0.002f * query.CommittedRayT(), 0.5f), g_sun.xyz, 200000.0f,
                         kMaskSolid | kMaskCutout | kMaskShadowOnly, true, met)) {
                sunlit = saturate(lambert / g_look.z);
            }
        }
    }
    return rt_lin(colour) * lerp(g_look.x, 1.0f, sunlit);
}

// The local lights' highlight at the pixel, over it and its four neighbours on the same object (one light a pixel is
// noisy). Until the reflection pass's R raw takes LightsSpec before its filter, it goes on the mirror.
float3 highlight_near(int2 pixel, float4 guide) {
    uint width, height;
    g_lights_spec.GetDimensions(width, height);
    float3 sum = g_lights_spec.Load(int3(pixel, 0)).rgb;
    float taps = 1.0f;
    const int2 steps[4] = {int2(1, 0), int2(-1, 0), int2(0, 1), int2(0, -1)};
    for (uint k = 0u; k < 4u; ++k) {
        const int2 at = pixel + steps[k];
        if (at.x < 0 || at.y < 0 || at.x >= int(width) || at.y >= int(height)) {
            continue;
        }
        const float4 near = g_guide.Load(int3(at, 0));
        if (near.a != 0.0f && near.g == guide.g && near.b == guide.b) {
            sum += g_lights_spec.Load(int3(at, 0)).rgb;
            taps += 1.0f;
        }
    }
    return sum / taps;
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
    float4 facing_out = float4(1.0f, 0.0f, 0.0f, 0.0f);
    bool clear = false;  // glass or water on the second layer: the mirror is (F R, 4 + F)
    float body = 1.0f;
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
            // Drawn from both sides: lit from either; a two-sided cutout's far side by kCardThrough (D5).
            const bool two_sided = (flags & kSurfaceTwoSided) != 0u;
            const bool card = two_sided && item != 0u && (g_items.Load(g_at.x + (item - 1u) * kItemBytes + 4u) & kItemAlphaTested) != 0u;
            const float lambert = saturate(!two_sided ? turned : card && turned < 0.0f ? -kCardThrough * turned : abs(turned));
            facing = lit ? lambert : saturate(lambert / g_look.z);
        }
        facing_out.r = facing;
        // A painted mesh whose paint is one light, placed by one matrix (item word 10: 0): that light's direction
        // turned to the eye by the item's normal rows (word 7), and the sun's (FB 22.19).
        if (!lit && item != 0u) {
            const uint at = g_at.x + (item - 1u) * kItemBytes;
            const uint paint = g_items.Load(at + 64u);
            if ((paint & 255u) != 0u && g_items.Load(at + 40u) == 0u) {
                const float2 o = float2(int2(paint << 12u, paint) >> 20) / 2047.0f;
                float3 d = float3(o, 1.0f - abs(o.x) - abs(o.y));
                if (d.z < 0.0f) {
                    d.xy = (1.0f - abs(d.yx)) * float2(d.x >= 0.0f ? 1.0f : -1.0f, d.y >= 0.0f ? 1.0f : -1.0f);
                }
                const uint row = mesh_at(8u) + g_items.Load(at + 28u) * 16u;
                const float3 n0 = asfloat(g_rows.Load3(row + 48u)), n1 = asfloat(g_rows.Load3(row + 64u)), n2 = asfloat(g_rows.Load3(row + 80u));
                const float3 eye_d = normalize(float3(dot(n0, d), dot(n1, d), dot(n2, d)));
                const float sun = (g_more.z & 1u) != 0u ? max(dot(s.smooth_, g_sun.xyz), 0.0f) : 0.0f;
                facing_out.gba = float3(sun, max(dot(s.smooth_, eye_d), 0.0f), float(paint & 255u) / 255.0f);
            }
        }
        // The lamp of the pixel's light set: every point light is an emitter (bit 6), lit through the added light.
        share.b = 0.0f;
        share.r = v;

        // The sky's share: the sky and bounce light pass's (22.10; rt_light_space.hlsl's last step), else 1.
        const float seen = 1.0f;
        if (second) {
            share = float4(v3, seen);  // as the programs read it: no filter after this layer's
        } else {
            share.b = lit ? -1.0f - facing : facing;  // for the filter's last step (rt_light_space.hlsl)
            share.a = seen;
        }

        // What the surface mirrors: glass, metal and glossy things show the scene and the sky along the eye's ray turned
        // at the surface, more at a glancing look (Schlick's term), a metal in its own colour; and the sun's own glint
        // where that ray runs at the sun unhindered.
        if ((g_more.z & 32u) != 0u && (second || (g_more.z & 1024u) == 0u)) {  // bit 10: the reflections pass has the opaque layer
            const uint look = g_items.Load(g_at.x + (item - 1u) * kItemBytes + 44u);
            const uint tint_word = g_items.Load(g_at.x + (item - 1u) * kItemBytes + 48u);
            const uint class_ = look & 255u;
            const float rough = float(look >> 16u & 255u) / 255.0f;
            float head_on = 0.0f;  // how much it mirrors seen head on
            float3 tint = float3(1.0f, 1.0f, 1.0f);
            if (class_ == kClassGlass) {
                head_on = 0.1f;
            } else if (class_ == kClassWater) {
                head_on = second ? 0.02f : 0.0f;  // the opaque layer's water is the painted floor under it
            } else if (class_ == kClassMetal) {
                head_on = 0.55f;
                tint = lerp(float3(1.0f, 1.0f, 1.0f), rt_lin(float3(float(tint_word & 255u), float(tint_word >> 8u & 255u), float(tint_word >> 16u & 255u)) / 255.0f), 0.5f);
            } else if (class_ == kClassGloss) {
                head_on = 0.05f * (1.0f - rough);
            }
            if (head_on > 0.0f) {
                const float3 look_along = normalize(s.at);
                const float3 n = dot(s.smooth_, look_along) < 0.0f ? s.smooth_ : s.normal;
                const float glance = 1.0f - saturate(-dot(n, look_along));
                const float2 terms = mirror_terms(rough, 1.0f - glance);  // over its lobe: less at a glance when rough (b47)
                const float share_ = saturate(head_on * terms.x + terms.y);
                const float3 turned_ray = reflect(look_along, n);
                const float pixel_wide = abs(g_ray.x);  // a pixel's width a unit of way from the eye
                float3 seen_ = seen_along(s.at + n * s.skin, turned_ray, s.away * pixel_wide, pixel_wide);
                uint met;
                if ((g_more.z & 1u) != 0u && dot(turned_ray, g_sun.xyz) > 0.9995f &&
                    !blocked(s.at + n * s.skin, g_sun.xyz, 200000.0f, kMaskSolid | kMaskCutout | kMaskShadowOnly, true, met)) {
                    seen_ += float3(4.0f, 3.8f, 3.4f);  // the sun itself, far brighter than the picture's white
                }
                mirror.rgb = seen_ * tint * share_;
                // The painted reflection it was drawn with gives way to the real one: that much less of the colour under
                // the mirror (a blended surface: of its alpha, so glass clears); and what it mirrors is taken from the
                // colour under it, never added on top (C2; FB b52b).
                mirror.a = (1.0f - rt_dev_or(kDevPaintedMirror, 0.6f) * float(tint_word >> 24u) / 255.0f) * (1.0f - saturate(dot(tint * share_, float3(0.2126f, 0.7152f, 0.0722f))));
                if (second && (class_ == kClassGlass || class_ == kClassWater)) {
                    const float4 under = g_guide_opaque.Load(int3(pixel, 0));
                    const ClearSurface c = clear_surface(item, class_, -dot(n, look_along), under.a != 0.0f ? under.r - guide.r : 0.0f);
                    mirror = float4(seen_ * c.f, 4.0f + c.f);
                    body = c.body;
                    clear = true;
                }
            }
        }
    }
    if (guide.a != 0.0f && (second || (g_more.z & 1024u) == 0u)) {  // under bit 10 R carries the highlights
        mirror.rgb += highlight_near(pixel, guide);
    }
    if ((g_more.z & 128u) != 0u && item != 0u && guide.a != 0.0f) {
        share = float4(0.0f, 0.0f, 0.0f, 0.0f);  // the emitters alone (the filter's last step says so too)
    }
    g_out[id.xy] = share;
    g_facing[id.xy] = facing_out;
    g_mirror[id.xy] = float4(mirror.rgb, mirror.a + (second && !clear ? 2.0f : 0.0f));
    if (second) {
        g_body[id.xy] = body;
    }
}
