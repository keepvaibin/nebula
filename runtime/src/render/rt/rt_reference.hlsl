// SPDX-License-Identifier: GPL-3.0-only
// The reference path tracer: the truth every preset is compared with. Per pixel of
// the scene's opaque surfaces it estimates the same signals the real-time passes make, by the same lighting model
// (rt_shade.hlsli), with every bounce traced instead of cached, and accumulates them while nothing moves:
//   V  the sun's share: a ray to a uniform point of the sun's disc (from a place within the pixel, as the sun pass);
//   D  the local lights' share: one light chosen uniformly from ALL emitters of the frame, one shadow ray to a point
//      of it, weighed by the count;
//   I  the sky's and the bounce light's ratio to the stage's ambient (11.1): a cosine ray; where it meets the sky the
//      dome's ratio, where it meets a surface that surface's light by path tracing (its sun, one local light, and a
//      further bounce for its own sky, up to four bounces, Russian roulette after the second);
//   R  what a glass, metal or gloss pixel mirrors: the GGX-sampled direction (a mirror's own under roughness 0.03),
//      the same path tracing where it lands, metal and glass traced on (up to four specular bounces), the sun's glint;
//      weighed as the light pass's first cut weighs its mirror (rt_light.hlsl) until reflections (22.11).
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
Texture2D<float4> g_r_before : register(t15);  // rgb what the pixel mirrors (as the first cut weighs it), a what stays of its colour
RT_FORMAT(rgba32f) RWTexture2D<float4> g_v : register(u0);
RT_FORMAT(rgba32f) RWTexture2D<float4> g_d : register(u1);
RT_FORMAT(rgba32f) RWTexture2D<float4> g_i : register(u2);
RT_FORMAT(rgba32f) RWTexture2D<float4> g_r : register(u3);

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
        result += mul(through, k * c);
        through = mul(through, float3x3(k.x * m[0], k.y * m[1], k.z * m[2]));
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
        g_r[id.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }
    rt_shade_begin(g_pass.w);
    const RtSurface s = rt_surface(pixel, guide, g_normals.Load(here), g_motion.Load(here).w);
    const uint at = g_at.x + (item - 1u) * kItemBytes;
    const uint item_flags = g_items.Load(at + 4u);
    const uint look = g_items.Load(at + 44u), tint_word = g_items.Load(at + 48u);
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
    // What the pixel mirrors, as the light pass's first cut weighs it (head-on share, Schlick's rise, metal's tint).
    const uint class_ = look & 255u;
    const float rough = float(look >> 16u & 255u) / 255.0f;
    float head_on = 0.0f;
    float3 tint = float3(1.0f, 1.0f, 1.0f);
    if (class_ == kClassGlass) {
        head_on = 0.1f;
    } else if (class_ == kClassMetal) {
        head_on = 0.55f;
        tint = lerp(float3(1.0f, 1.0f, 1.0f), rt_bytes3(tint_word), 0.5f);
    } else if (class_ == kClassGloss) {
        head_on = 0.05f * (1.0f - rough);
    }
    const float3 look_along = normalize(s.at);
    const float3 mirror_n = dot(s.smooth_, look_along) < 0.0f ? s.smooth_ : s.normal;
    const float glance = 1.0f - saturate(-dot(mirror_n, look_along));
    const float fresnel = head_on + (1.0f - head_on) * glance * glance * glance * glance * glance;
    const float keep = head_on > 0.0f ? 1.0f - 0.6f * float(tint_word >> 24u) / 255.0f : 1.0f;

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
            } else if (!lit) {
                v = float3(0.0f, 0.0f, 0.0f);  // a painted side turned from the sun: its own shade (as the sun pass)
            }
        }
        v_sum += v;
        // D: every local light, by one chosen uniformly.
        d_sum += ref_light(s.at, s.origin, s.smooth_, s.slot, seed);
        // I: the sky and what bounces.
        i_sum += ref_ratio(s.origin, s.smooth_, seed);
        // R.
        if (head_on > 0.0f) {
            const float3 dir0 = rough < 0.03f ? reflect(look_along, mirror_n) : rt_ggx_reflect(mirror_n, -look_along, rough * rough, rt_random2(seed));
            const float3 dir = dot(dir0, s.normal) <= 0.0f ? reflect(dir0, s.normal) : dir0;
            r_sum += ref_mirror(s.at + mirror_n * s.skin, dir, rough, seed) * tint * fresnel;
        }
    }
    const float total = count + float(paths);
    const float before = count / total, now = 1.0f / total;
    g_v[id.xy] = float4(v_mean.rgb * before + v_sum * now, total);
    g_d[id.xy] = float4(d_mean.rgb * before + d_sum * now, facing);
    g_i[id.xy] = float4(i_mean.rgb * before + i_sum * now, lit ? 2.0f : 1.0f);
    g_r[id.xy] = float4(r_mean.rgb * before + r_sum * now, keep);
}
