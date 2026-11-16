// SPDX-License-Identifier: GPL-3.0-only
// The reference path tracer's paths: what a surface shows by path tracing, the
// indirect ratio by path tracing, one local light of all, what a mirror ray sees. Shared by the reference
// (rt_reference.hlsl) and its T (rt_transmit.hlsl under RT_TRANSMIT_REFERENCE). Included after rt_shade.hlsli.
#ifndef NEBULA_RT_REFERENCE_PATHS_HLSLI
#define NEBULA_RT_REFERENCE_PATHS_HLSLI

static const float kRefReach = 4000000.0f;
// The most bounces a path takes: Russian roulette ends it, unbiased (its weight 1 / keep); this is the loop's bound.
// The loop's gain per bounce is at most kBounceAlbedo kAlbedoMost = 0.45 under the relight and kAlbedoMost = 0.9 under
// the art's form, so what lies past 64 bounces is under 0.9^64 = 0.1 % of the whole series even
// there (the 4 bounces before left out up to 0.45^4 = 4 % under the relight and 66 % under the art's form).
static const uint kRefBounces = 64u;

// What the indirect ratio is a ratio to: O's unit, as the real-time pass divides (rt_o_unit).
float3 ref_ambient() {
    return rt_o_unit();
}

// The light a pixel's surface does not take from itself (rt_lights.hlsl's rule): a light adds nothing to its own body,
// the parts of its model that glow this frame (item word 14 bit 25); the model's other parts take its light. A pixel
// whose item is not known keeps its slot's rule.
uint ref_self(int2 pixel, uint slot) {
    const uint here = g_item_map.Load(int3(pixel, 0)).x;
    return here == 0u || (g_items.Load(g_at.x + (here - 1u) * kItemBytes + 56u) >> 25u & 1u) != 0u ? slot : 0xFFFFFFFFu;
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
    const float3 through = reach > 1.0f ? rt_through(from, toward, reach, slot, place) :float3(1.0f, 1.0f, 1.0f);
    return light * through * float(count);
}

// A hit as a point rays leave from, as the surface pass leaves a pixel's (rt_surface): its geometric normal lifts it
// by the skin, its smooth normal shades (rt_ray_origin, rt_room_near take it).
RtSurface ref_point(RtHit h) {
    RtSurface p;
    p.away = length(h.at);
    p.at = h.at;
    p.smooth_ = h.ns;
    p.normal = h.ng;
    p.lift = 0.0f;
    p.slot = h.slot;
    p.skin = max(0.003f * p.away, 0.5f);  // rt_off's
    p.origin = h.at + h.ng * p.skin;
    return p;
}

// What a surface shows to a ray that lights something, by path tracing, as an affine function of its own sky ratio O
// (which the caller traces on): out = c + M * O. A thing that shows itself has M = 0 (the emitter list stands for a
// listed glow: an emissive-class surface shows itself unless it is listed; any other class is shaded, whatever its
// glow row). `e`: an area glow's emission over its body (rt_hit_area_glow: as it is, not against the
// scene-lit mean). The share is rt_share_terms' affine form: where the relight's tint bound (rt_relit_share) binds
// the reference keeps the unbounded hue (the bound is not affine in O; see INDIRECT.md).
void ref_surface(RtHit h, inout uint seed, out float3 c, out float3x3 m, out float3 e) {
    e = rt_hit_area_glow(h, true);
    if (h.glows && !h.listed) {
        c = rt_shown(h.emission) * g_shade.gains.x;  // linear, as the real-time hit's
        m = (float3x3)0;
        return;
    }
    float3 base = rt_return_albedo(h, true);  // the model's linear colour at kBounceAlbedo
    // Glass's body gives up what its mirror takes, (1 - F0) under the model (rt_hit_radiance_with).
    if (h.klass == kRtGlass) {
        const float r = (h.ior - 1.0f) / (h.ior + 1.0f);
        base *= 1.0f - rt_form() * r * r;
    }
    c = float3(0.0f, 0.0f, 0.0f);
    m = (float3x3)0;
    if (any(base > 0.0f)) {  // a metal's body is gone under the model: no ray for a light it cannot take
        const bool lit = (h.item_flags & kItemLit) != 0u;
        const float nl = rt_hit_facing(h);
        const float3 v = nl > 0.0f ? rt_hit_sun(h, true, rt_random2(seed)) : float3(1.0f, 1.0f, 1.0f);
        const float3 d = ref_light(h.at, rt_off(h.at, h.ng), h.ns, h.glow_body ? h.slot : 0xFFFFFFFFu, seed);
        float3 s0;
        float3x3 mo;
        rt_share_terms(lit, nl, v, s0, mo);
        c = base * (s0 + d);
        m = float3x3(base.x * mo[0], base.y * mo[1], base.z * mo[2]);
    }
    // Mirror-like hits a gathered ray meets stand in for their reflection, as the real-time gather's.
    if (h.klass == kRtMetal || h.klass == kRtGlass) {
        c += rt_hit_mirror_stand_in(h);
    }
}

// The indirect ratio I at a point, by path tracing: the cosine-weighted mean of what it sees about `n`, the dome
// where a ray meets it (rt_sky_ratio), kBounce * L / unit where it meets a surface, L depending on that surface's own
// ratio, traced on: the whole series. Each gathered ray as the real-time gather's: from the side
// it leaves by (rt_ray_origin), through the water on its way, a near hit in a room at
// kRoomNearShare of its light (rt_room_near). `count`: the first surface met goes into the
// counters of the gathered hits' mean, as the real-time pass counts its own (rt_count_hit).
float3 ref_ratio(RtSurface p, float3 n, bool count, inout uint seed) {
    const float3 k = g_shade.gains.z / ref_ambient();  // kBounce * L / unit, per channel
    float3 result = float3(0.0f, 0.0f, 0.0f);
    float3x3 through = float3x3(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    RtSurface point_ = p;
    float3 normal = n;
    for (uint bounce = 0u; bounce < kRefBounces; ++bounce) {
        const float3 dir = rt_cosine(normal, rt_random2(seed));
        const float3 from = rt_ray_origin(point_, dir);
        uint item, triangle_;
        float2 where_;
        float far_;
        const uint met = rt_gather(from, dir, item, triangle_, where_, far_);
        const float3 water = rt_water_way(from, dir, met == 2u ? far_ : kIndirectReach);
        through = float3x3(through[0] * water, through[1] * water, through[2] * water);
        if (met == 0u) {
            result += mul(through, float3(1.0f, 1.0f, 1.0f));  // nothing at all: the stage's ambient itself
            break;
        }
        RtHit h;
        rt_hit_read(item, triangle_, where_, far_, from, dir, 0.05f, h);
        if (met == 1u || (h.item_flags & kItemSky) != 0u) {
            result += mul(through, rt_sky_ratio(rt_dome_light(h)));  // linear, as the mean it is a ratio to
            break;
        }
        float3 c, e;
        float3x3 m;
        ref_surface(h, seed, c, m, e);
        const bool shines = h.glows && !h.listed;
        if (!shines && bounce == 0u && count) {
            rt_count_hit(k * (c + mul(m, float3(g_shade.gains.y, g_shade.gains.y, g_shade.gains.y))));
        }
        const float near_ = shines ? 1.0f : rt_room_near(point_, normal, far_);
        const float3 kh = shines ? k : k * (g_shade.authored.w * near_);
        result += mul(through, kh * c + k * e);  // an area glow's emission as it is
        if (shines) {
            break;
        }
        through = mul(through, float3x3(kh.x * m[0], kh.y * m[1], kh.z * m[2]));
        const float3 rows = abs(through[0]) + abs(through[1]) + abs(through[2]);
        const float most = max(rows.x, max(rows.y, rows.z));
        if (!(most > 0.0f)) {
            break;  // nothing more reaches the point
        }
        if (bounce >= 1u) {
            const float keep = clamp(most, 0.05f, 0.95f);
            if (rt_random(seed) >= keep) {
                break;
            }
            through /= keep;
        }
        point_ = ref_point(h);
        normal = h.ns;
    }
    return result;
}

// What a ray that only looks sees at a solid hit by path tracing, as rt_hit_radiance_with shows it to such a ray (the
// sun on its disc, one light of all, its own O traced on rather than kHitSky): a lit program within its set's level
// (rt_lit_seen_bound), a glowing body with its share of its own colour. Both bounds take the one O
// traced: a bound of a sample is not the bound of the mean where it binds (INDIRECT.md). A metal's mirror is the caller's.
float3 ref_seen(RtHit h, inout uint seed) {
    if (h.glows) {
        return rt_shown(h.emission) * g_shade.gains.x;
    }
    float3 albedo = rt_hit_albedo(h);
    if (h.klass == kRtGlass) {
        const float r = (h.ior - 1.0f) / (h.ior + 1.0f);
        albedo *= 1.0f - rt_form() * r * r;
    }
    float3 out_ = float3(0.0f, 0.0f, 0.0f);
    if (any(albedo > 0.0f)) {
        const bool lit = (h.item_flags & kItemLit) != 0u;
        const float nl = rt_hit_facing(h);
        const float3 v = nl > 0.0f ? rt_hit_sun(h, true, rt_random2(seed)) : float3(1.0f, 1.0f, 1.0f);
        // A glowing body seen takes no lamp light (its own light would flood it), as rt_hit_radiance_with's body_seen.
        const float3 d = h.glow_body ? float3(0.0f, 0.0f, 0.0f) : ref_light(h.at, rt_off(h.at, h.ng), h.ns, 0xFFFFFFFFu, seed);
        const float3 o = ref_ratio(ref_point(h), h.ns, false, seed);
        float3 share_ = rt_share(lit, nl, v, o, rt_tint_bound(h.item_flags));
        if (lit) {
            share_ = rt_lit_seen_bound(nl, v, o, share_);
        }
        out_ = albedo * (share_ + d);
    }
    // A glass that glows as a body (not as an emitter: ref_mirror sees through any other glass) mirrors by its stand-in,
    // under the body's own-colour rule, as rt_hit_radiance_with orders them.
    if (h.klass == kRtGlass) {
        out_ += rt_hit_mirror_stand_in(h);
    }
    if (h.glow_body) {  // a hot body at 2 to 2.5 of it
        const float3 lit_value = rt_lin(h.base);
        out_ = (h.item_flags & kRtItemHot) != 0u ? clamp(out_, 2.0f * lit_value, 2.5f * lit_value)
                                                 : min(out_ + (h.being ? 0.35f : 0.8f) * lit_value, 1.2f * lit_value);
    }
    return out_;
}

// What a mirror ray sees from `from` along `dir`: where it meets no solid or cut-out thing the sky along its direction
// read from the eye (rt_look: the one miss rule), a dome met as a solid thing its light, each with the sun's glint
// where the ray runs at it (a ray of the reference's T only once it has turned at a mirror: the transmit pass's own
// rays show none); else the surface by path tracing, traced on through what mirrors (up to kRefBounces, Russian
// roulette on the weight):
// - a metal by Schlick's F from its F0 (rt_hit_mirror_stand_in's: lerp(0.04, lin(base), metal) under the model, its
//   painted share under the art's) at the half vector of its GGX sample;
// - a clear thing (glass-class, not glowing) seen through as the real-time mirror's ray sees it
//   (rt_seen_past_clear: straight on, half its body's through-colour a face), its face mirroring: Schlick's F at the
//   face chooses which, so each way carries its own share unweighed (F / F, (1 - F) / (1 - F)).
// A glowing glass shows itself as the real-time ray's stop does (ref_seen).
#if defined(RT_TRANSMIT_REFERENCE) && RT_TRANSMIT_REFERENCE
static const bool kRefGlintStraight = false;  // rt_transmit.hlsl's rays: rt_seen_past_clear(.., glint false, ..)
#else
static const bool kRefGlintStraight = true;   // the reflections pass's rays: rt_mirror_seen
#endif
float3 ref_mirror(float3 from, float3 dir, float roughness, inout uint seed) {
    float3 result = float3(0.0f, 0.0f, 0.0f), weight = float3(1.0f, 1.0f, 1.0f);
    bool turned = false;
    for (uint bounce = 0u; bounce < kRefBounces; ++bounce) {
        const float cone = abs(g_ray.x) + 0.35f * roughness;
        // The thing met first, whatever its class (no clear face passed: this walk chooses at each face itself), or the
        // sky: rt_look's one ray query a leg, the dome along `dir` from the eye after a miss, each with the glint.
        const RtLook l = rt_look(from, dir, kRefReach, cone, kRefGlintStraight || turned, 0u, true);
        if (!l.hit) {
            result += weight * l.shown;  // the dome or g_sky, or a dome item met; the model's sun
            break;
        }
        const RtHit h = l.h;
        const uint item = h.item;
        if (h.klass == kRtGlass && !h.glows && !h.glow_body) {
            const float r = (h.ior - 1.0f) / (h.ior + 1.0f);
            const float f0 = r * r;
            const float slant = 1.0f - saturate(abs(dot(dir, h.ng)));
            const float fresnel = lerp(f0, 1.0f, slant * slant * slant * slant * slant) * rt_form();
            if (rt_random(seed) < fresnel) {
                const float3 n = dot(h.ns, dir) < 0.0f ? h.ns : -h.ns;
                const float3 ng = dot(h.ng, dir) < 0.0f ? h.ng : -h.ng;
                dir = h.roughness < 0.03f ? reflect(dir, n) : rt_ggx_reflect(n, -dir, h.roughness * h.roughness, rt_random2(seed));
                if (dot(dir, ng) <= 0.0f) {
                    dir = reflect(dir, ng);
                }
                from = rt_off(h.at, ng);
                turned = true;
            } else {
                weight *= sqrt(rt_clear_through(g_at.x + item * kItemBytes));
                from = h.at + dir * max(0.003f * length(h.at), 0.5f);
            }
            roughness = max(roughness, h.roughness);
        } else {
            result += weight * ref_seen(h, seed);
            if (h.glows || h.klass != kRtMetal) {
                break;
            }
            const float t = rt_form();
            const float3 f0 = lerp(float3(h.share, h.share, h.share), lerp(float3(0.04f, 0.04f, 0.04f), rt_lin(h.base), h.metal), t);
            const float3 v_in = -dir;
            dir = h.roughness < 0.03f ? reflect(dir, h.ns) : rt_ggx_reflect(h.ns, v_in, h.roughness * h.roughness, rt_random2(seed));
            if (dot(dir, h.ng) <= 0.0f) {
                dir = reflect(dir, h.ng);
            }
            const float3 half_ = v_in + dir;
            const float c = dot(half_, half_) > 1e-12f ? saturate(dot(normalize(half_), v_in)) : 0.0f;
            const float slant = 1.0f - c;
            weight *= f0 + (1.0f - f0) * (slant * slant * slant * slant * slant);
            from = rt_off(h.at, h.ng);
            roughness = max(roughness, h.roughness);
            turned = true;
        }
        const float most = max(weight.r, max(weight.g, weight.b));
        if (!(most > 0.0f)) {
            break;
        }
        if (bounce >= 1u) {
            const float keep = clamp(most, 0.05f, 0.95f);
            if (rt_random(seed) >= keep) {
                break;
            }
            weight /= keep;
        }
    }
    return result;
}

#endif  // NEBULA_RT_REFERENCE_PATHS_HLSLI
