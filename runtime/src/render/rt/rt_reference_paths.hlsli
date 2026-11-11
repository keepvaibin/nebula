// SPDX-License-Identifier: GPL-3.0-only
// The reference path tracer's paths (the master plan's section 18): what a surface shows by path tracing, the
// indirect ratio by path tracing, one local light of all, what a mirror ray sees. Shared by the reference
// (rt_reference.hlsl) and its T (rt_transmit_ref.hlsl). Included after rt_shade.hlsli.
#ifndef NEBULA_RT_REFERENCE_PATHS_HLSLI
#define NEBULA_RT_REFERENCE_PATHS_HLSLI

static const float kRefReach = 4000000.0f;

// The stage's authored ambient: what the indirect ratio is a ratio to.
float3 ref_ambient() {
    return max(g_shade.authored.rgb, 0.02f);
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
// stands for a listed glow; only a mirror's ray sees it shine).
void ref_surface(RtHit h, bool lighting, inout uint seed, out float3 c, out float3x3 m) {
    if (h.glows && !(lighting && h.listed)) {
        c = h.emission * g_shade.gains.x;
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
    c = h.base * (s0 + d);
    m = float3x3(h.base.x * mo[0], h.base.y * mo[1], h.base.z * mo[2]);
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
        if (met == 0u) {
            result += mul(through, float3(1.0f, 1.0f, 1.0f));  // nothing at all: the stage's ambient itself
            break;
        }
        RtHit h;
        rt_hit_read(item, triangle_, where_, far_, from, dir, 0.05f, h);
        if (met == 1u || (h.item_flags & kItemSky) != 0u) {
            const float3 dome = rt_dome(h);
            if (s_count_dome) {
                rt_count_dome(dome);
            }
            result += mul(through, rt_sky_ratio(dome));
            break;
        }
        float3 c;
        float3x3 m;
        ref_surface(h, true, seed, c, m);
        if (h.klass == kRtMetal || h.klass == kRtGlass) {
            c += g_shade.ambient.rgb * h.share;
        }
        // What shines of itself counts as it is; a surface lit by the scene against the mean of such hits, counted at
        // the first as the real-time pass shades it (its own ratio kHitSky) (FB b46).
        const bool shines = h.glows && !h.listed;
        if (!shines && bounce == 0u && s_count_dome) {
            rt_count_hit(k * (c + mul(m, float3(g_shade.gains.y, g_shade.gains.y, g_shade.gains.y))));
        }
        const float3 kh = shines ? k : k * g_shade.authored.w;
        result += mul(through, kh * c);
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
            result += weight * g_sky.rgb;
            break;
        }
        RtHit h;
        rt_hit_read(item, triangle_, where_, far_, from, dir, abs(g_ray.x) + 0.35f * roughness, h);
        if ((h.item_flags & kItemSky) != 0u) {
            float3 seen = rt_dome(h);
            // The sun itself on the reflected ray, where nothing hides it (12.2 item 4).
            if (rt_sun_traced() && dot(dir, g_sun.xyz) > cos(atan(3.0f * g_sun.w))) {
                seen += g_shade.key.rgb * g_shade.look.w;
            }
            result += weight * seen;
            break;
        }
        float3 c;
        float3x3 m;
        ref_surface(h, false, seed, c, m);
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
        weight *= h.klass == kRtMetal ? h.base : float3(f0, f0, f0);
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
