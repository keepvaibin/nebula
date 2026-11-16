// SPDX-License-Identifier: GPL-3.0-only
// The reference path tracer's paths: every bounce traced, no cache, on the shared model the real-time
// passes shade with (shade_core.hlsli through rt_shade.hlsli: rt_hit_material, shade_lobes, shade_highlight, the sun's
// light and the two skies), so a preset differs from it by its estimates alone. Shared by the reference
// (rt_reference.hlsl) and its T (rt_transmit.hlsl under RT_TRANSMIT_REFERENCE). Included after rt_shade.hlsli.
#ifndef NEBULA_RT_REFERENCE_PATHS_HLSLI
#define NEBULA_RT_REFERENCE_PATHS_HLSLI

static const float kRefReach = 4000000.0f;
// The most bounces a path takes: Russian roulette ends it, unbiased (its weight 1 / keep); this is the loop's bound.
// A bounce keeps at most kShadeBaseMost (0.9) of what it takes, so what lies past 64 bounces is under 0.9^64 = 0.1 %
// of the whole series.
static const uint kRefBounces = 64u;

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

// The radiance arriving at `from` along `dir`, by path tracing (the shared model at every vertex). `lighting`: the path
// lights something (the sky and bounce light I), so a miss shows the lighting sky and a listed glow lights through
// the emitter list alone (next-event estimation, ref_light), never again by being met; a path that looks (a mirror's)
// shows the drawn dome and every glow as it shines, until its first opaque bounce, after which it lights that
// surface. At each surface: its emission, the sun by a ray to a uniform point of its disc and the sun's highlight,
// one local light of all, and the next direction drawn from its lobes by their albedos (the diffuse lobe by the
// cosine, the specular by GGX), weighed by the lobe's albedo over its chance: the split sum's own weights, the
// real-time hit's. A clear thing (glass-class, not glowing) chooses by its exact Fresnel to mirror or to let the ray
// through (half its body's through-colour a face), each way unweighed. A path that lights takes the water on its way
// (rt_water_way), as the gather's rays.
float3 ref_path(float3 from, float3 dir, float roughness, bool lighting, inout uint seed) {
    float3 result = float3(0.0f, 0.0f, 0.0f), weight = float3(1.0f, 1.0f, 1.0f);
    for (uint bounce = 0u; bounce < kRefBounces; ++bounce) {
        const float cone = abs(g_ray.x) + 0.35f * roughness;
        const RtLook l = rt_look(from, dir, kRefReach, cone, 0u, true);
        if (lighting) {
            weight *= rt_water_way(from, dir, l.far_);
        }
        if (!l.hit) {
            result += weight * (lighting ? rt_sky_lighting(l.shown) : l.shown);
            break;
        }
        const RtHit h = l.h;
        if (h.klass == kRtGlass && !h.glows && !h.glow_body) {
            const float f = shade_fresnel_exact(abs(dot(dir, h.ns)), h.ior);
            if (rt_random(seed) < f) {
                const float3 n = dot(h.ns, dir) < 0.0f ? h.ns : -h.ns;
                const float3 ng = dot(h.ng, dir) < 0.0f ? h.ng : -h.ng;
                dir = h.roughness < 0.03f ? reflect(dir, n) : rt_ggx_reflect(n, -dir, h.roughness * h.roughness, rt_random2(seed));
                if (dot(dir, ng) <= 0.0f) {
                    dir = reflect(dir, ng);
                }
                from = rt_off(h.at, ng);
            } else {
                weight *= sqrt(rt_clear_through(g_at.x + h.item * kItemBytes));
                from = h.at + dir * max(0.003f * length(h.at), 0.5f);
            }
            roughness = max(roughness, h.roughness);
            continue;
        }
        const ShadeMaterial m = rt_hit_material(h, lighting);
        if (h.glows || h.klass == kRtPainted) {
            result += weight * m.emit;
            break;
        }
        const float facing = rt_hit_facing(h);
        const float3 v = facing > 0.0f ? rt_hit_sun(h, true, rt_random2(seed)) : float3(0.0f, 0.0f, 0.0f);
        const float3 sun = rt_sun_light() * v;
        const float3 d = ref_light(h.at, rt_off(h.at, h.ng), h.ns, h.glow_body ? h.slot : 0xFFFFFFFFu, seed);
        const ShadeLobes lobes = shade_lobes(m, dot(h.ns, h.view));
        const float3 highlight = facing > 0.0f ? shade_highlight(m, lobes, h.ns, h.view, g_sun.xyz, g_sun.w) * sun : float3(0.0f, 0.0f, 0.0f);
        result += weight * (lobes.diffuse * (sun * facing + d) + highlight + m.emit);
        // The next direction, by the lobes' albedos.
        const float pd = rt_lum(lobes.diffuse), ps = rt_lum(lobes.mirror);
        if (!(pd + ps > 0.0f)) {
            break;
        }
        const float p_mirror = ps / (pd + ps);
        if (rt_random(seed) < p_mirror) {
            dir = h.roughness < 0.03f ? reflect(-h.view, h.ns) : rt_ggx_reflect(h.ns, h.view, h.roughness * h.roughness, rt_random2(seed));
            if (dot(dir, h.ng) <= 0.0f) {
                dir = reflect(dir, h.ng);
            }
            weight *= lobes.mirror / p_mirror;
            roughness = max(roughness, h.roughness);
        } else {
            dir = rt_cosine(dot(h.ns, h.ng) > 0.0f ? h.ns : h.ng, rt_random2(seed));
            weight *= lobes.diffuse / (1.0f - p_mirror);
            roughness = 1.0f;
        }
        from = rt_off(h.at, h.ng);
        lighting = true;  // past its first opaque bounce a path lights that surface
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

// I at a point, by path tracing: the radiance arriving along a cosine-drawn direction about `n` (its mean is the
// irradiance over pi, light units), each ray from the side it leaves by (rt_ray_origin), as the gather's.
float3 ref_irradiance(RtSurface p, float3 n, inout uint seed) {
    const float3 dir = rt_cosine(n, rt_random2(seed));
    return ref_path(rt_ray_origin(p, dir), dir, 1.0f, true, seed);
}

// What a mirror ray sees from `from` along `dir`: the path that looks first.
float3 ref_mirror(float3 from, float3 dir, float roughness, inout uint seed) {
    return ref_path(from, dir, roughness, false, seed);
}

#endif  // NEBULA_RT_REFERENCE_PATHS_HLSLI
