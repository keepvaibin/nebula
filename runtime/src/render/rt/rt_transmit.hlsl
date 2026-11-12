// SPDX-License-Identifier: GPL-3.0-only
// What is seen through glass, crystal, ice and water, bent (the master plan's 13.2 High, 13.3 High; work list 22.14):
// one pass over the second layer's pixels (the nearest blended surface a pixel) of class glass or water, High only.
//   Water: the eye's ray refracted at the surface (index 1.33) on to the floor (the nearest solid or cut-out thing),
//     what the floor sends back by the shared model (rt_hit_radiance: its sun traced), times exp(-sigma d), sigma from
//     the water's own colour over kWaterClear: shallow water clear, deep water its colour and dark.
//   Glass, crystal, ice that is a body (its thickness, item word 17, at least kThinGlass): refracted in, traced inside
//     against the same instance's faces (blended, no culling) to its exit, refracted out (total internal reflection:
//     turned back inside, at most two inside segments), then on into the scene; exp(-sigma d) over the way inside,
//     sigma = -ln(colour) / thickness * kGlassTint. A glass ball shows the scene upside down (row R3). Crystal (index
//     1.7 and over) traces each colour's own index (ior - 0.02, ior, ior + 0.02 for r, g, b): rainbow edges (row R4),
//     three paths, no noise, no filter (crystals are small on the screen).
//   A shell (thinner than kThinGlass: bubbles, domes, capsules, windows, and glass with no thickness cooked) bends
//     nothing that shows: traced straight on, T = colour ^ (1 / cos), its own thickness over the slant (R1, R3; FB b61).
// Writes T: rgb what comes through (the absorption in it, no Fresnel: the programs weigh it, program.hlsli
// traced_mirror), a 1 where traced, 0 elsewhere (the programs blend as on Medium there).
// Pass constants: g_pass.x 1 (the second layer), g_pass.w the shading frame's first byte in t4.
#include "rt_shade.hlsli"
#if RT_TRANSMIT_REFERENCE
#include "rt_reference_paths.hlsli"
#endif

Texture2D<float4> g_guide : register(t9);     // the second layer's guide (r its eye distance, a 0 where none)
Texture2D<float4> g_normals : register(t10);  // the second layer's normals
Texture2D<float4> g_motion : register(t11);   // the second layer's motion (w: the smooth surface's lift)
RT_FORMAT(rgba16f) RWTexture2D<float4> g_out : register(u0);
#if RT_TRANSMIT_REFERENCE
Texture2D<float4> g_t_before : register(t12);               // the reference's T sums, the walk before's (FB)
RT_FORMAT(rgba32f) RWTexture2D<float4> g_t_mean : register(u1);  // this walk's: rgb the mean, a the samples
#endif

static const float kThinGlass = 4.0f;      // units: thinner is a shell (a soap film, a dome's pane)
static const float kGlassTint = 1.0f;      // 13.2: the art's whole tint at the thickness (R1; FB b61: was 0.5)
// 13.3: the way to mostly the water's colour is the stage's water clear (g_look.w; 600 units by default; FB b58)
static const float kSeenReach = 4000000.0f;
static const uint kInsideSegments = 2u;

// What a ray from `from` along `dir` meets and what that sends back (as the reflections pass's rays).
float3 seen_along(float3 from, float3 dir, float cone, inout uint seed, out float far_) {
#if RT_TRANSMIT_REFERENCE
    // The reference (section 18; FB): what the ray meets by path tracing (its light traced on, mirrors traced on), the
    // mean of the walk's paths; the distance as the pass takes it.
    uint met_item, met_triangle;
    float2 met_where;
    if (!rt_closest(from, dir, kSeenReach, kMaskSolid | kMaskCutout | kMaskSky, met_item, met_triangle, met_where, far_)) {
        far_ = kSeenReach;
    }
    const uint paths = max(uint(g_pass_value[0].x), 1u);
    float3 sum = float3(0.0f, 0.0f, 0.0f);
    for (uint p = 0u; p < paths; ++p) {
        sum += ref_mirror(from, dir, 0.0f, seed);
    }
    return sum / float(paths);
#else
    uint item, triangle_;
    float2 where_;
    if (!rt_closest(from, dir, kSeenReach, kMaskSolid | kMaskCutout | kMaskSky, item, triangle_, where_, far_)) {
        far_ = kSeenReach;
        return rt_lin(g_sky.rgb);  // linear (M1; FB b59b)
    }
    RtHit h;
    rt_hit_read(item, triangle_, where_, far_, from, dir, cone, h);
    if ((h.item_flags & kItemSky) != 0u) {
        return rt_shown(rt_dome(h));  // linear (M1; FB b65)
    }
    const float3 o = float3(g_shade.gains.y, g_shade.gains.y, g_shade.gains.y);
    return rt_hit_radiance(h, kRtShadeSunRay | kRtShadeLightRay, o, seed);
#endif
}

// The nearest face of instance `self` on the ray (blended, both sides), its distance; false where none (an open mesh).
bool own_face(uint self, float3 from, float3 dir, float reach, out uint triangle_, out float2 where_, out float far_) {
    RayDesc ray;
    ray.Origin = from;
    ray.Direction = dir;
    ray.TMin = 0.0f;
    ray.TMax = reach;
    RayQuery<RAY_FLAG_FORCE_NON_OPAQUE> query;
    query.TraceRayInline(g_scene, RAY_FLAG_FORCE_NON_OPAQUE, kMaskBlended, ray);
    while (query.Proceed()) {
        if (query.CandidateInstanceID() == self) {
            query.CommitNonOpaqueTriangleHit();
        }
    }
    triangle_ = 0u;
    where_ = float2(0.0f, 0.0f);
    far_ = reach;
    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT) {
        return false;
    }
    triangle_ = query.CommittedPrimitiveIndex();
    where_ = query.CommittedTriangleBarycentrics();
    far_ = query.CommittedRayT();
    return true;
}

// Through a body of instance `self` at index `ior`, entering at `at` (normal `n` toward the eye) along `look`: what
// comes out (before absorption) and the way inside.
float3 through_body(uint self, float ior, float3 at, float3 n, float3 look, float skin, float cone, inout uint seed,
                    out float inside) {
    inside = 0.0f;
    float3 dir = refract(look, n, 1.0f / ior);
    float3 from = at - n * skin;
    float3 out_normal = n;  // the last face's normal toward the outside
    for (uint segment = 0u; segment < kInsideSegments; ++segment) {
        uint triangle_;
        float2 where_;
        float far_;
        if (!own_face(self, from, dir, kSeenReach, triangle_, where_, far_)) {
            break;  // open: as if it left where it is
        }
        RtHit h;
        rt_hit_read(self, triangle_, where_, far_, from, dir, cone, h);
        inside += far_;
        from = h.at;
        out_normal = -h.ns;  // h.ns faces the ray, inside
        const float3 bent = refract(dir, h.ns, ior);
        if (dot(bent, bent) > 0.0f) {
            dir = bent;
            break;
        }
        dir = reflect(dir, h.ns);  // total internal reflection: on inside
        out_normal = h.ns;
    }
    float far_;
    return seen_along(from + out_normal * skin, dir, cone, seed, far_);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint width, height;
    g_out.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) {
        return;
    }
    const int3 here = int3(id.xy, 0);
#if RT_TRANSMIT_REFERENCE
    g_t_mean[id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);  // no T here: no sums (a traced pixel writes its own below)
#endif
    const float4 guide = g_guide.Load(here);
    const uint item = g_item_map.Load(here).x;
    if (g_pass.x == 0u || guide.a == 0.0f || item == 0u) {
        g_out[id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
    const uint at = g_at.x + (item - 1u) * kItemBytes;
    const uint look_word = g_items.Load(at + 44u);
    const uint klass = look_word & 255u;
    const bool water = klass == kClassWater;
    const float thickness = asfloat(g_items.Load(at + 68u));
    if (!(water || klass == kClassGlass)) {
        g_out[id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
    rt_shade_begin(g_pass.w);
    const RtSurface s = rt_surface(int2(id.xy), guide, g_normals.Load(here), g_motion.Load(here).w);
    const float3 look = normalize(s.at);
    const float3 n = dot(s.smooth_, look) < 0.0f ? s.smooth_ : s.normal;
    // The filter's colour in linear light: white seen through it turns the art's colour (M1; FB b59b).
    const float3 colour = max(rt_lin(rt_bytes3(g_items.Load(at + 48u))), 0.002f);
    // The index (item word 15, FC's c15: bits 12-19 as (ior - 1) x 100; 0: the class's own).
    const uint ior_bits = g_items.Load(at + 60u) >> 12u & 255u;
    const float ior = ior_bits != 0u ? 1.0f + float(ior_bits) / 100.0f : (water ? 1.33f : 1.5f);
    const float cone = abs(g_ray.x);
    uint seed = rt_seed(id.xy, g_more.y, 47u);
    float3 t;
    if (water) {
        const float3 dir = refract(look, n, 1.0f / ior);
        float far_;
        const float3 floor_ = seen_along(s.at - n * s.skin, dot(dir, dir) > 0.0f ? dir : look, cone, seed, far_);
        const float clear = rt_water_clear();
        t = floor_ * exp(log(colour) * (min(far_, 8.0f * clear) / clear));
    } else if (thickness < kThinGlass) {
        // A shell: straight on, its colour by the slant (R1, R3; FB b61).
        float far_;
        const float cos_in = max(-dot(n, look), 0.2f);
        t = seen_along(s.at - n * s.skin, look, cone, seed, far_) * pow(colour, kGlassTint / cos_in);
    } else {
        const float3 sigma = -log(colour) / thickness * kGlassTint;
        if (ior >= 1.7f) {
            for (uint c = 0u; c < 3u; ++c) {
                float inside;
                const float3 seen = through_body(item - 1u, ior + 0.02f * (float(c) - 1.0f), s.at, n, look, s.skin, cone, seed, inside);
                t[c] = seen[c] * exp(-sigma[c] * inside);
            }
        } else {
            float inside;
            t = through_body(item - 1u, ior, s.at, n, look, s.skin, cone, seed, inside) * exp(-sigma * inside);
        }
    }
    InterlockedAdd(g_counters[24], 1u);  // refracted pixels (rt-inputs)
#if RT_TRANSMIT_REFERENCE
    {
        // Summed while nothing moves (the reference's own restarts start it again), held at the most samples.
        const float4 before = g_t_before.Load(here);
        const float count = (g_pass.z & 1u) != 0u ? 0.0f : before.a;
        const float paths = float(max(uint(g_pass_value[0].x), 1u));
        if (count >= g_pass_value[0].y) {
            t = before.rgb;
            g_t_mean[id.xy] = before;
        } else {
            t = (before.rgb * count + t * paths) / (count + paths);
            g_t_mean[id.xy] = float4(t, count + paths);
        }
    }
#endif
    g_out[id.xy] = float4(min(t, 64.0f), 1.0f);
}
