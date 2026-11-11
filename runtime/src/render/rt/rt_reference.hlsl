// SPDX-License-Identifier: GPL-3.0-only
// The reference path tracer (the master plan's section 18): the truth every preset is compared with. Per pixel of
// the scene's opaque surfaces it estimates the same signals the real-time passes make, by the same lighting model
// (rt_shade.hlsli), with every bounce traced instead of cached, and accumulates them while nothing moves:
//   V  the sun's share: a ray to a uniform point of the sun's disc (from a place within the pixel, as the sun pass);
//      a side turned from the sun 1 without a ray, as the sun pass (the Facing picture puts the facing in);
//   D  the local lights' share: one light chosen uniformly from ALL emitters of the frame, one shadow ray to a point
//      of it, weighed by the count;
//   I  the sky's and the bounce light's ratio to the stage's ambient (11.1): a cosine ray; where it meets the sky the
//      dome's ratio, where it meets a surface that surface's light by path tracing (its sun, one local light, and a
//      further bounce for its own sky, up to four bounces, Russian roulette after the second);
//   R  what a glass, water, metal or gloss pixel mirrors (every class the reflections pass traces on High): the
//      radiance seen along the GGX-sampled direction (a mirror's own under roughness 0.03; water's 0), the same path
//      tracing where it lands, metal and glass traced on (up to four specular bounces), the sun's glint; unweighed,
//      as the reflections pass's R raw; a 1 where the pixel mirrors, 0 elsewhere. rt_reflect_out.hlsl composites the
//      mean as it does a preset's R (rt_bounce.cpp rt_reference_publish).
// The host starts the count again whenever anything differs (another list, the camera, a cut, the size) and keeps
// adding, up to 4,096 samples, while the same list is walked again (--diagnostics rt-reference holds each picture
// that is taken). rt_reference_out.hlsl turns the means into the images the programs read, after the filters.
// Pass constants: g_pass.z bit 0 start again; g_pass.w the shading frame's first byte in t4; g_pass_value[0].x paths
// a frame, .y the most samples kept.
#include "rt_shade.hlsli"

Texture2D<float4> g_guide : register(t9);
Texture2D<float4> g_normals : register(t10);
Texture2D<float4> g_motion : register(t11);
Texture2D<float4> g_v_before : register(t12);  // rgb V, a the samples behind every mean of the pixel
Texture2D<float4> g_d_before : register(t13);  // rgb D, a the surface's facing to the sun (n.s, both sides for a sheet)
Texture2D<float4> g_i_before : register(t14);  // rgb I (unclamped), a 0 no surface, 1 painted, 2 lit
Texture2D<float4> g_r_before : register(t15);  // rgb the radiance the pixel's mirror ray sees, a 1 where it mirrors
RT_FORMAT(rgba32f) RWTexture2D<float4> g_v : register(u0);
RT_FORMAT(rgba32f) RWTexture2D<float4> g_d : register(u1);
RT_FORMAT(rgba32f) RWTexture2D<float4> g_i : register(u2);
RT_FORMAT(rgba32f) RWTexture2D<float4> g_r : register(u3);

#include "rt_reference_paths.hlsli"

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint width, height;
    g_v.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) {
        return;
    }
    const int2 pixel = int2(id.xy);
    const int3 here = int3(pixel, 0);
    const float4 guide = g_guide.Load(here);
    const uint item = g_item_map.Load(here).x;
    if (guide.a == 0.0f || item == 0u) {
        g_v[id.xy] = float4(1.0f, 1.0f, 1.0f, 0.0f);
        g_d[id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        g_i[id.xy] = float4(1.0f, 1.0f, 1.0f, 0.0f);
        g_r[id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
    rt_shade_begin(g_pass.w);
    const RtSurface s = rt_surface(pixel, guide, g_normals.Load(here), g_motion.Load(here).w);
    const uint at = g_at.x + (item - 1u) * kItemBytes;
    const uint item_flags = g_items.Load(at + 4u);
    const uint look = g_items.Load(at + 44u);
    float4 v_mean = g_v_before.Load(here), d_mean = g_d_before.Load(here), i_mean = g_i_before.Load(here), r_mean = g_r_before.Load(here);
    const float count = (g_pass.z & 1u) != 0u ? 0.0f : v_mean.a;
    const bool lit = (item_flags & kItemLit) != 0u;
    const bool thin = (item_flags & kItemThin) != 0u;
    const float turned = dot(s.smooth_, g_sun.xyz);
    const float facing = thin ? abs(turned) : turned;
    if (count >= g_pass_value[0].y) {
        // Converged: the means stay.
        g_v[id.xy] = v_mean;
        g_d[id.xy] = d_mean;
        g_i[id.xy] = i_mean;
        g_r[id.xy] = r_mean;
        return;
    }
    uint seed = rt_seed(id.xy, g_more.y, 17u);
    s_count_dome = ((id.x | id.y) & 7u) == 0u;
    // What the pixel mirrors (R): the radiance its mirror ray sees, as the reflections pass's R raw (rt_reflect.hlsl
    // reflects: on High glass and water, metal and gloss); rt_reflect_out.hlsl composites the mean.
    const uint class_ = look & 255u;
    const bool mirrors = class_ == kClassGlass || class_ == kClassWater || class_ == kClassMetal || class_ == kClassGloss;
    const float rough = class_ == kClassWater ? 0.0f : float(look >> 16u & 255u) / 255.0f;
    const float3 look_along = normalize(s.at);
    const float3 mirror_n = dot(s.smooth_, look_along) < 0.0f ? s.smooth_ : s.normal;

    const uint paths = max(uint(g_pass_value[0].x), 1u);
    float3 v_sum = float3(0.0f, 0.0f, 0.0f), d_sum = v_sum, i_sum = v_sum, r_sum = v_sum;
    for (uint p = 0u; p < paths; ++p) {
        // V: from a place within the pixel, toward a uniform point of the sun's disc.
        float3 v = float3(1.0f, 1.0f, 1.0f);
        if (rt_sun_traced()) {
            if (facing > 0.0f) {
                const float3 through_pixel = rt_eye_ray(g_ray, float2(id.xy) + rt_random2(seed));
                const float slant = dot(s.normal, through_pixel);
                float3 within_pixel = float3(0.0f, 0.0f, 0.0f);
                if (abs(slant) > 1e-4f) {
                    const float3 moved = through_pixel * (dot(s.normal, s.at) / slant) - s.at;
                    const float most = 2.0f * abs(g_ray.x) * s.away;
                    const float size = length(moved);
                    within_pixel = size > most ? moved * (most / size) : moved;
                }
                const float3 from = (turned >= 0.0f ? s.origin : s.at - s.normal * s.skin) + within_pixel;
                v = rt_through_of(from, rt_within(g_sun.xyz, g_sun.w, rt_random2(seed)), kRtFarAway, 0xFFFFFFFFu, rt_sun_mask(lit));
            }
        }
        v_sum += v;
        // D: every local light, by one chosen uniformly.
        d_sum += ref_light(s.at, s.origin, s.smooth_, s.slot, seed);
        // I: the sky and what bounces.
        i_sum += ref_ratio(s.origin, s.smooth_, seed);
        // R.
        if (mirrors) {
            const float3 dir0 = rough < 0.03f ? reflect(look_along, mirror_n) : rt_ggx_reflect(mirror_n, -look_along, rough * rough, rt_random2(seed));
            const float3 dir = dot(dir0, s.normal) <= 0.0f ? reflect(dir0, s.normal) : dir0;
            r_sum += ref_mirror(s.at + mirror_n * s.skin, dir, rough, seed);
        }
    }
    const float total = count + float(paths);
    const float before = count / total, now = 1.0f / total;
    g_v[id.xy] = float4(v_mean.rgb * before + v_sum * now, total);
    g_d[id.xy] = float4(d_mean.rgb * before + d_sum * now, facing);
    g_i[id.xy] = float4(i_mean.rgb * before + i_sum * now, lit ? 2.0f : 1.0f);
    g_r[id.xy] = float4(r_mean.rgb * before + r_sum * now, mirrors ? 1.0f : 0.0f);
}
