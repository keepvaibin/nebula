// SPDX-License-Identifier: GPL-3.0-only
// The reference path tracer: the truth every preset is compared with. Per pixel of
// the scene's opaque surfaces it estimates the same signals the real-time passes make, by the same model
// (rt_reference_paths.hlsli on shade_core.hlsli), with every bounce traced instead of cached, and accumulates them while
// nothing moves:
//   V  the sun's share: a ray to a uniform point of the sun's disc (from a place within the pixel, as the sun pass);
//      a side turned from the sun 1 without a ray, as the sun pass (the Facing picture puts the facing in);
//   D  the local lights' share: one light chosen uniformly from ALL emitters of the frame, one shadow ray to a point
//      of it, weighed by the count;
//   I  the sky and bounce light, light units: the radiance arriving along a cosine ray, the whole series by path
//      tracing (rt_reference_paths.hlsli ref_irradiance), each gathered ray as the real-time gather's;
//   R  what every lit pixel's specular lobe sees: the radiance along the GGX-sampled direction (a mirror's own under
//      roughness 0.03; water's 0), the same path tracing where it lands, demodulated as the reflections pass's R raw;
//      a 1 where the pixel has a lobe, 0 elsewhere. rt_reflect_out.hlsl composites the mean as it does a preset's R
//      (rt_bounce.cpp rt_reference_publish), at every roughness.
// The host starts the count again whenever anything differs (another list, the camera, a cut, the size) and keeps
// adding, up to 4,096 samples, while the same list is walked again (--diagnostics rt-reference holds each picture
// that is taken). rt_reference_out.hlsl turns the means into the images the programs read, after the filters.
// Pass constants: g_pass.z bit 0 start again, bit 2 the host reads the dome's counts; g_pass.w the shading frame's first byte in t4; g_pass_value[0].x paths
// a frame, .y the most samples kept.
#include "rt_shade.hlsli"

Texture2D<float4> g_guide : register(t9);
Texture2D<float4> g_normals : register(t10);
Texture2D<float4> g_motion : register(t11);
Texture2D<float4> g_v_before : register(t12);  // rgb V, a the samples behind every mean of the pixel
Texture2D<float4> g_d_before : register(t13);  // rgb D, a the surface's facing to the sun (n.s, both sides for a sheet)
Texture2D<float4> g_i_before : register(t14);  // rgb I, a 0 no surface, 1 a surface
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
    // The counts only while the host reads them (g_pass.z bit 2): a hold past its measuring walks keeps the means.
    const bool counting = (g_pass.z & 4u) != 0u;
    if (counting && all(id.xy < uint2(64u, 64u))) {
        rt_shade_begin(g_pass.w);
        rt_count_dome_directions(id.xy);  // the dome's mean, as the indirect pass counts it
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
    const bool lit = (item_flags & kItemLit) != 0u;  // the sun's mask (rt_sun_mask)
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
    // What the pixel's lobe sees (R), as the reflections pass's R raw, for every class that takes light;
    // rt_reflect_out.hlsl composites the mean.
    const uint class_ = look & 255u;
    const bool mirrors = class_ != kClassEmissive && class_ != kRtPainted;
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
        d_sum += ref_light(s.at, s.origin, s.smooth_, ref_self(pixel, s.slot), seed);
        // I: the sky and what bounces.
        i_sum += ref_irradiance(s, s.smooth_, seed);
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
    g_i[id.xy] = float4(i_mean.rgb * before + i_sum * now, 1.0f);
    g_r[id.xy] = float4(r_mean.rgb * before + r_sum * now, mirrors ? 1.0f : 0.0f);
}
