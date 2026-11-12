// SPDX-License-Identifier: GPL-3.0-only
// The reference path tracer's paths (the master plan's section 18): what a surface shows by path tracing, the
// indirect ratio by path tracing, one local light of all, what a mirror ray sees. Shared by the reference
// (rt_reference.hlsl) and its T (rt_transmit_ref.hlsl). Included after rt_shade.hlsli.
#ifndef NEBULA_RT_REFERENCE_PATHS_HLSLI
#define NEBULA_RT_REFERENCE_PATHS_HLSLI

static const float kRefReach = 4000000.0f;

// What the indirect ratio is a ratio to: O's unit, as the real-time pass divides (rt_o_unit; b59).
float3 ref_ambient() {
    return rt_o_unit();
}

// One local light, chosen uniformly from all of the frame's, with its shadow: on average the sum of every light.
float3 ref_light(float3 at, float3 from, float3 n, uint self, inout uint seed) {
    const uint count = g_glow.w;
    if (count == 0u) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    const uint index = min(uint(rt_random(seed) * float(count)), count - 1u);
    float4 place;
    uint slot;
    const float3 light = rt_emitter_light(index, at, n, self, place, slot);
    if (!(rt_lum(light) > 0.0f)) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    float reach;
    const float3 toward = rt_toward_light(place, from, rt_random2(seed), reach);
    const float3 through = reach > 1.0f ? rt_through(from, toward, reach, slot) : float3(1.0f, 1.0f, 1.0f);
    return light * through * float(count);
}

// The dome's luminance counted from one pixel in 64.
static bool s_count_dome = false;

// What a surface shows by path tracing, as an affine function of its own sky ratio O (which the caller traces on):
// out = c + M * O. A thing that shows itself has M = 0. `lighting`: the path lights something (the emitter list
// stands for a listed glow). An emissive-class surface shows itself; any other class is shaded, whatever its glow
// row (a ray sees what the screen shows; FB b77). `e`: an area glow's emission over its body, for a path that lights
// something (rt_hit_area_glow: as it is, not against the scene-lit mean).
void ref_surface(RtHit h, bool lighting, inout uint seed, out float3 c, out float3x3 m, out float3 e) {
    e = rt_hit_area_glow(h, lighting);
    if (h.glows && !(lighting && h.listed)) {
        c = rt_shown(h.emission) * g_shade.gains.x;  // linear, as the real-time hit's (M1; FB b65b)
        m = (float3x3)0;
        return;
    }
    const bool lit = (h.item_flags & kItemLit) != 0u;
    const float nl = rt_hit_facing(h);
    const float3 v = nl > 0.0f ? rt_hit_sun(h, true, rt_random2(seed)) : float3(1.0f, 1.0f, 1.0f);
    const float3 d = ref_light(h.at, rt_off(h.at, h.ng), h.ns, h.slot, seed);
    float3 s0;
    float3x3 mo;
    rt_share_terms(lit, nl, v, s0, mo);
    const float3 base = rt_hit_albedo(h);  // the art's colour, or the model's linear one (b59)
    c = base * (s0 + d);
    m = float3x3(base.x * mo[0], base.y * mo[1], base.z * mo[2]);
}

// The indirect ratio I at a surface point (11.1), by path tracing: ratio = S(dir) where the ray meets the sky,
// kBounce * L / A where it meets a surface, L itself depending on that surface's own ratio (traced on).
float3 ref_ratio(float3 origin, float3 n, inout uint seed) {
    const float3 a = ref_ambient();
    const float3 k = g_shade.gains.z / a;  // kBounce * L / A, per channel
    float3 result = float3(0.0f, 0.0f, 0.0f);
    float3x3 through = float3x3(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    float3 from = origin, normal = n;
    for (uint bounce = 0u; bounce < 4u; ++bounce) {
        const float3 dir = rt_cosine(normal, rt_random2(seed));
        uint item, triangle_;
        float2 where_;
        float far_;
        const uint met = rt_gather(from, dir, item, triangle_, where_, far_);
        // What the water on this step lets through (A4 W4; FB b78), as the real-time gather.
        const float3 water = rt_water_way(from, dir, met == 2u ? far_ : kIndirectReach);
        through = float3x3(through[0] * water, through[1] * water, through[2] * water);
        if (met == 0u) {
            result += mul(through, float3(1.0f, 1.0f, 1.0f));  // nothing at all: the stage's ambient itself
            break;
        }
        RtHit h;
        rt_hit_read(item, triangle_, where_, far_, from, dir, 0.05f, h);
        if (met == 1u || (h.item_flags & kItemSky) != 0u) {
            const float3 dome = rt_lin(rt_dome(h));  // linear, as the mean it is a ratio to (b65)
            if (s_count_dome) {
                rt_count_dome(dome);
            }
            result += mul(through, rt_sky_ratio(dome));
            break;
        }
        float3 c, e;
        float3x3 m;
        ref_surface(h, true, seed, c, m, e);
        if (h.klass == kRtMetal || h.klass == kRtGlass) {
            c += rt_hit_mirror_stand_in(h);
        }
        // What shines of itself counts as it is; a surface lit by the scene against the mean of such hits, counted at
        // the first as the real-time pass shades it (its own ratio kHitSky) (FB b46).
        const bool shines = h.glows && !h.listed;
        if (!shines && bounce == 0u && s_count_dome) {
            rt_count_hit(k * (c + mul(m, float3(g_shade.gains.y, g_shade.gains.y, g_shade.gains.y))));
        }
        const float3 kh = shines ? k : k * g_shade.authored.w;
        result += mul(through, kh * c + k * e);  // an area glow's emission as it is (b77)
        through = mul(through, float3x3(kh.x * m[0], kh.y * m[1], kh.z * m[2]));
        if (h.glows && !h.listed) {
            break;
        }
        if (bounce >= 1u) {
            const float3 rows = abs(through[0]) + abs(through[1]) + abs(through[2]);
            const float keep = clamp(max(rows.x, max(rows.y, rows.z)), 0.05f, 0.95f);
            if (rt_random(seed) >= keep) {
                break;
            }
            through /= keep;
        }
        from = rt_off(h.at, h.ng);
        normal = h.ns;
    }
    return result;
}

// What a mirror ray sees from `from` along `dir`: the sky, or the surface by path tracing; metal and glass traced on.
float3 ref_mirror(float3 from, float3 dir, float roughness, inout uint seed) {
    float3 result = float3(0.0f, 0.0f, 0.0f), weight = float3(1.0f, 1.0f, 1.0f);
    for (uint bounce = 0u; bounce < 4u; ++bounce) {
        uint item, triangle_;
        float2 where_;
        float far_;
        if (!rt_closest(from, dir, kRefReach, kMaskSolid | kMaskCutout | kMaskSky, item, triangle_, where_, far_)) {
            result += weight * rt_shown(g_sky.rgb);
            break;
        }
        RtHit h;
        rt_hit_read(item, triangle_, where_, far_, from, dir, abs(g_ray.x) + 0.35f * roughness, h);
        if ((h.item_flags & kItemSky) != 0u) {
            float3 seen = rt_shown(rt_dome(h));
            // The sun itself on the reflected ray, where nothing hides it (12.2 item 4; the model's sun: b59).
            if (rt_sees_sun(dir)) {
                seen += rt_sun_glint();
            }
            result += weight * seen;
            break;
        }
        float3 c, e;  // e 0: a mirror's ray sees an area glow's body alone (b77)
        float3x3 m;
        ref_surface(h, false, seed, c, m, e);
        float3 seen = c;
        if (!h.glows) {
            seen += mul(m, ref_ratio(rt_off(h.at, h.ng), h.ns, seed));
        }
        result += weight * seen;
        if (h.glows || (h.klass != kRtMetal && h.klass != kRtGlass)) {
            break;
        }
        // On: a metal in its own colour, glass by its Fresnel share seen head on, times kShine (12.4).
        const float f0 = ((h.ior - 1.0f) / (h.ior + 1.0f)) * ((h.ior - 1.0f) / (h.ior + 1.0f)) * 2.0f;
        weight *= h.klass == kRtMetal ? rt_shown(h.base) : float3(f0, f0, f0);
        if (!(rt_lum(weight) > 0.002f)) {
            break;
        }
        const float alpha = h.roughness * h.roughness;
        dir = h.roughness < 0.03f ? reflect(dir, h.ns) : rt_ggx_reflect(h.ns, -dir, alpha, rt_random2(seed));
        if (dot(dir, h.ng) <= 0.0f) {
            dir = reflect(dir, h.ng);
        }
        from = rt_off(h.at, h.ng);
        roughness = max(roughness, h.roughness);
    }
    return result;
}

#endif  // NEBULA_RT_REFERENCE_PATHS_HLSLI
