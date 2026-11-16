// SPDX-License-Identifier: GPL-3.0-only
// What is seen through glass, crystal, ice and water, bent:
// one pass over the second layer's pixels (the nearest blended surface a pixel) of class glass or water, High only.
//   Water, one medium: the eye's ray refracted at the surface (index 1.33) on to the bed (the nearest solid or cut-out
//     thing) or the body's own far face (an exit: refracted out there, then on into the scene), what that sends back by
//     the shared model (rt_hit_radiance: its sun traced), times T = exp(-a d) over the way d under water (pure water's
//     absorption at the stage's clear way, rt_water_absorb); plus the light the water itself scatters back over that
//     way in its own colour (rt_water_scattered: the sky and the sun by its visibility at the surface). Deep water (the ray leaves by the far face onto nothing solid, or meets neither bed nor far face: an
//     open sheet over the void) is the same at d infinite: nothing seen, the water's colour whole. Where even the
//     surface's own face is not met again, no T: the programs composite the surface over the frame drawn behind it.
//   Glass, crystal, ice that is a body (its thickness, item word 17, at least kThinGlass: crystal and ice as measured;
//     clear glass only by a rt_materials.tsv row, the cook making it a shell): refracted in, traced inside against
//     the same instance's faces (blended, no culling) to its exit, refracted out (total internal reflection: turned back
//     inside, at most two inside segments), then on into the scene; exp(-sigma d) over the way inside, sigma =
//     -ln(colour) / thickness * kGlassTint. A crystal ball shows the scene upside down (row R3). Crystal (index 1.7 and
//     over) traces each colour's own index (ior - 0.02, ior, ior + 0.02 for r, g, b): rainbow edges, three
//     paths, no noise, no filter (crystals are small on the screen).
//   A shell (thinner than kThinGlass: capsules, domes, bubbles, spheres, panes: hollow things are two
//     thin walls, what is inside is seen): walls that bend nothing that shows. Traced straight on; each wall crossed
//     takes the square root of the through-colour (half the body a face, as rt_seen_past_clear and let_through) to the
//     power 1 / cos_t, cos_t the refracted angle's cosine inside the wall (Snell: a slab's way is its thickness over
//     cos_t); the far wall (the instance's next face on the ray, before which a solid inside is seen) also 1 - F
//     (Schlick).
// Writes T: rgb the radiance that comes out through the surface toward the eye (what is seen, through the medium's
// absorption, and a water's own scattered light; no Fresnel of the first face: the programs weigh it, program.hlsli
// traced_colour: F R + (1 - F) T), a 1 where traced, 0 elsewhere (the programs blend as on Medium there).
// Pass constants: g_pass.x 1 (the second layer), g_pass.w the shading frame's first byte in t4.
#include "rt_shade.hlsli"
#if RT_TRANSMIT_REFERENCE
#include "rt_reference_paths.hlsli"
#endif

Texture2D<float4> g_guide : register(t9);     // the second layer's guide (r its eye distance, a 0 where none)
Texture2D<float4> g_normals : register(t10);  // the second layer's normals
Texture2D<float4> g_motion : register(t11);   // the second layer's motion (w: the smooth surface's lift)
Texture2D<float4> g_sun_seen : register(t13); // the second layer's sun visibility V (its filter's), where traced
RT_FORMAT(rgba16f) RWTexture2D<float4> g_out : register(u0);
#if RT_TRANSMIT_REFERENCE
Texture2D<float4> g_t_before : register(t12);               // the reference's T sums, the walk before's
RT_FORMAT(rgba32f) RWTexture2D<float4> g_t_mean : register(u1);  // this walk's: rgb the mean, a the samples
#endif

static const float kThinGlass = 4.0f;      // units: thinner is a shell (a soap film, a dome's pane)
static const float kGlassTint = 1.0f;      // 13.2: the art's whole tint at the thickness (was 0.5)
// Falling water's sheet (rt_clear_fall), units at normal incidence: about a metre of water (100 units a metre at
// the game's scale), over which it takes water's absorption and gives back water's colour as the sea does.
static const float kFallSheet = 100.0f;
static const float kSeenReach = 4000000.0f;
static const uint kInsideSegments = 2u;

// Schlick's F of a face of index `ior` crossed at cosine `c`.
float transmit_fresnel(float ior, float c) {
    const float r = (ior - 1.0f) / (ior + 1.0f);
    const float g = 1.0f - saturate(c);
    return r * r + (1.0f - r * r) * g * g * g * g * g;
}

// The cosine of the refracted angle inside a face of index `ior` met at cosine `c` from air (Snell): never 0 for an
// index over 1, so a slab's way thickness / cos_t stays finite at any slant.
float transmit_cos_inside(float ior, float c) {
    const float s = saturate(1.0f - c * c) / (ior * ior);
    return sqrt(1.0f - s);
}

// One look of the walk below: what a ray from `from` along `dir` meets. `solid`: the nearest solid or cut-out thing
// before `reach` (a body's far wall), whatever its class, met or not (met false: nothing there); else the ray seen on
// to kSeenReach as the reflections pass's rays see (a clear thing on the way seen through, not shaded as a wall:
// rt_shade.hlsli rt_look), `far_` its way to the first thing met (kSeenReach where none). Its light is
// transmit_shade's, the walk's one shading call.
struct TransmitLook {
    float3 from, dir;  // the ray (the reference traces its paths along it)
    bool met;          // `solid`: a thing within reach
    float far_;
#if !RT_TRANSMIT_REFERENCE
    RtLook l;
#endif
};
TransmitLook transmit_look(float3 from, float3 dir, float reach, float cone, bool solid) {
    TransmitLook k;
    k.from = from;
    k.dir = dir;
#if RT_TRANSMIT_REFERENCE
    // The reference: the distance as the pass takes it (a looking ray's measured to the dome as well);
    // what it meets by path tracing (transmit_shade).
    uint met_item, met_triangle;
    float2 met_where;
    k.met = rt_closest(from, dir, solid ? reach : kSeenReach, solid ? kMaskSolid | kMaskCutout : kMaskSolid | kMaskCutout | kMaskSky,
                       met_item, met_triangle, met_where, k.far_);
    if (!k.met && !solid) {
        k.far_ = kSeenReach;
    }
#else
    k.l = rt_look(from, dir, solid ? reach : kSeenReach, cone, solid ? 0u : kClearFaces, !solid);
    k.met = k.l.met;
    k.far_ = k.l.far_;
#endif
    return k;
}
float3 transmit_shade(TransmitLook k, inout uint seed) {
#if RT_TRANSMIT_REFERENCE
    // The mean of the walk's paths (its light traced on, mirrors traced on).
    const uint paths = max(uint(g_pass_value[0].x), 1u);
    float3 sum = float3(0.0f, 0.0f, 0.0f);
    for (uint p = 0u; p < paths; ++p) {
        sum += ref_mirror(k.from, k.dir, 0.0f, seed);
    }
    return sum / float(paths);
#else
    return rt_look_shaded(k.l, kRtShadeSunRay | kRtShadeLightRay, rt_sky_mean(), seed);
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
        if (rt_hit_item(query.CandidateInstanceID()) == self) {
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

// The walks (one a pixel; crystal one a colour), as steps: each step at most one query of the pixel's own instance
// (own_face) and one look (transmit_look), then the walk's one shading of the look it ends on (transmit_shade). One
// textual call of each, so the pass holds one copy of each walk's ray queries (each call of a function that traces is
// inlined whole). kWalkSteps: the longest walk, a body's (kInsideSegments inside, then out).
static const uint kWayWater = 0u, kWayWalls = 1u, kWayBody = 2u;
static const uint kWalkSteps = kInsideSegments + 1u;
// A step's phases: the first (an own face and a look), the water's fallback (an own face alone), the last look alone.
static const uint kStepFirst = 0u, kStepSheet = 1u, kStepLast = 2u;

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
    if (water && (g_more.z & 4096u) != 0u) {
        // The camera under water: the eye's way is the game's fog and the surface is seen from inside;
        // no T (the programs take the mirror alone, clear_surface's total internal reflection).
        g_out[id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
    rt_shade_begin(g_pass.w);
    const RtSurface s = rt_surface(int2(id.xy), guide, g_normals.Load(here), g_motion.Load(here).w);
    const float3 look = normalize(s.at);
    const float3 n = dot(s.smooth_, look) < 0.0f ? s.smooth_ : s.normal;
    // The filter's colour in linear light at its own thickness: its class's through-colour
    // (lerp(white, art, k), rt_common.hlsli), not the art's whole colour.
    const float3 colour = rt_clear_through(at);
    // The index (item word 15: bits 12-19 as (ior - 1) x 100; 0: the class's own).
    const uint ior_bits = g_items.Load(at + 60u) >> 12u & 255u;
    const float ior = ior_bits != 0u ? 1.0f + float(ior_bits) / 100.0f : (water ? 1.33f : 1.5f);
    const float cone = abs(g_ray.x);
    uint seed = rt_seed(id.xy, g_more.y, 47u);
    float3 t = float3(0.0f, 0.0f, 0.0f);
    // The sun's visibility at the surface for the water's own light (g_more.z bit 0: the sun traced; else all of it,
    // as the light pass takes a stage without one).
    const float3 sun_v = (g_more.z & 1u) != 0u ? saturate(g_sun_seen.Load(here).rgb) : float3(1.0f, 1.0f, 1.0f);
    // rt_dev transmit_why (g_more.z bit 16): water's way shown in place of T, by how its walk ended: green the bed met
    // (darker by the way under water, e^(-way / the stage's water clear)), blue out through the body's own far face onto
    // something, red out through it onto nothing (deep), yellow no face but the open sheet's own (deep), magenta no T.
    const bool why = water && (g_more.z & 65536u) != 0u;
    float3 why_colour = float3(1.0f, 0.0f, 1.0f);
    bool traced = true;
    const uint self = item - 1u;
    // Water: under the surface (the eye's ray refracted, at cosine cos_t to the normal), from `above` (a ray
    // from under the surface starts inside shallow ground) and `below` (the skin under it, for the body's own faces,
    // which a solid ray does not meet: the water is blended): the bed (the nearest solid or cut-out thing) before the
    // body's own far face, else that face refracted out (turned back inside past the critical angle, then on as
    // reflected; the way inside after it is not counted) and on into the scene: onto nothing solid, deep water.
    // Neither met (the water's body is not in the traced scene): an open sheet with nothing solid under it (a
    // code-drawn sea over the void: OceanBowl is one grid, no floor) is deep water too, where its own face is met along
    // the eye's ray; not even that: no T. `way`: the way under water, from the surface (`deep`: no end).
    // Walls (clear glass, which the cook makes a shell; thin crystal or ice):
    // straight on from the lit side; each wall crossed takes sqrt(colour) ^ (1 / cos_t) (its own thickness over the
    // refracted slant); the far wall (the instance's next face; none on an open pane) also 1 - F; a solid inside before
    // it is seen (the planet in a bubble, the building in a capsule: W1).
    // A body: refracted in at `ior`, the instance's next face met inside (none: open, as if it left where it is); a
    // solid thing inside before it is what is seen (a planet in a glass ball, the building in Good Egg's capsule: W1);
    // else refracted out there, or turned back inside by total internal reflection (off the face, inside: the next
    // segment does not meet it again), at most kInsideSegments; then on into the scene. Crystal (index
    // 1.7 and over): a walk a colour at ior - 0.02, ior, ior + 0.02.
    const uint way_kind = water ? kWayWater : thickness < kThinGlass ? kWayWalls : kWayBody;
    const bool fall = way_kind == kWayWalls && rt_clear_fall(at);  // the art's tint none: water's medium instead
    const uint colours = way_kind == kWayBody && ior >= 1.7f ? 3u : 1u;
    [loop] for (uint c = 0u; c < colours; ++c) {
        const float c_ior = colours == 3u ? ior + 0.02f * (float(c) - 1.0f) : ior;
        float3 from = rt_ray_origin(s, look), dir = look;  // walls: straight on
        float3 out_normal = n;  // a body: the last face's normal toward the outside
        float inside = 0.0f;    // a body: the way inside
        uint segment = 0u;
        float3 above = float3(0.0f, 0.0f, 0.0f), below = above;
        float to_surface = 0.0f;
        float way = 0.0f;  // water: the way under it
        float3 face = float3(1.0f, 1.0f, 1.0f), factor = face;  // walls: half the body a face, what the walls take
        if (way_kind == kWayWater) {
            // Into water from air: refract never turns back, and the refracted ray's cosine is at least
            // sqrt(1 - 1/ior^2).
            dir = refract(look, n, 1.0f / c_ior);
            above = s.at + n * s.skin;
            below = s.at - n * s.skin;
            to_surface = s.skin / -dot(dir, n);
        } else if (way_kind == kWayWalls) {
            face = fall ? float3(1.0f, 1.0f, 1.0f) : pow(colour, 0.5f * kGlassTint);
            factor = pow(face, 1.0f / transmit_cos_inside(c_ior, saturate(-dot(n, look))));
        } else {
            dir = refract(look, n, 1.0f / c_ior);
            from = s.at - n * s.skin;
        }
        float3 seen = float3(0.0f, 0.0f, 0.0f);
        bool deep = false;       // water: no bed, the way endless
        bool deep_test = false;  // water: the last look meeting nothing is deep water
        bool shade = false;      // the walk ends on `last`
        TransmitLook last;
        uint phase = kStepFirst;
        [loop] for (uint step = 0u; step < kWalkSteps; ++step) {
            // The step's own face: the first step's along the walk's ray (water's from under the surface), the
            // sheet's along the eye's ray through the surface; the last step has none.
            const bool sheet = phase == kStepSheet;
            const float3 own_from = sheet ? s.at - look * (2.0f * s.skin) : way_kind == kWayWater ? below : from;
            const float own_reach = sheet ? 4.0f * s.skin : kSeenReach;
            uint own_triangle = 0u;
            float2 own_where = float2(0.0f, 0.0f);
            float own_far = own_reach;
            bool own_met = false;
            if (phase != kStepLast) {
                own_met = own_face(self, own_from, sheet ? look : dir, own_reach, own_triangle, own_where, own_far);
            }
            if (sheet) {
                if (own_met) {
                    deep = true;
                    why_colour = float3(1.0f, 1.0f, 0.0f);
                } else {
                    traced = false;
                }
                break;
            }
            // The step's look: the first step's for a solid thing before the face met (water's from above the
            // surface, to the face and back up to it; with no face, the bed anywhere along the ray), a body's out of
            // an open one from where it is; the last step's on into the scene.
            const bool first = phase == kStepFirst;
            const bool solid = first && (way_kind == kWayWater || own_met);
            const float3 look_from = first && way_kind == kWayWater ? above
                                     : way_kind == kWayBody && !solid ? from + out_normal * s.skin
                                                                      : from;
            const float look_reach = !solid || !own_met ? kSeenReach
                                     : way_kind == kWayWater ? own_far + 2.0f * to_surface
                                                             : own_far;
            const TransmitLook k = transmit_look(look_from, dir, look_reach, cone, solid);
            if (!solid || k.met) {
                // The end: a solid thing met, or the look on into the scene.
                last = k;
                shade = true;
                if (way_kind == kWayWater && first) {
                    way = max(k.far_ - to_surface, 0.0f);
                    why_colour = float3(0.0f, exp(-way / rt_water_clear()), 0.0f);
                } else if (way_kind == kWayBody && solid) {
                    inside += k.far_;
                }
                break;
            }
            if (!own_met) {
                phase = kStepSheet;  // water, neither met
                continue;
            }
            // On through the face met.
            RtHit h;
            rt_hit_read(self, own_triangle, own_where, own_far, own_from, dir, cone, h);
            if (way_kind == kWayWater) {
                way = own_far + to_surface;
                const float3 bent = refract(dir, h.ns, c_ior);  // h.ns faces the ray: inside; water to air
                const bool leaves = dot(bent, bent) > 0.0f;
                const float exit_skin = max(0.003f * length(h.at), 0.5f);
                from = h.at + (leaves ? -h.ns : h.ns) * exit_skin;
                dir = leaves ? bent : reflect(dir, h.ns);
                deep_test = true;
                phase = kStepLast;
            } else if (way_kind == kWayWalls) {
                const float c2 = abs(dot(h.ns, look));
                factor *= pow(face, 1.0f / transmit_cos_inside(c_ior, c2)) * (1.0f - transmit_fresnel(c_ior, c2));
                from = h.at + look * max(0.003f * length(h.at), 0.5f);
                phase = kStepLast;
            } else {
                inside += own_far;
                from = h.at;
                out_normal = -h.ns;  // h.ns faces the ray, inside
                const float3 bent = refract(dir, h.ns, c_ior);
                if (dot(bent, bent) > 0.0f) {
                    dir = bent;
                    phase = kStepLast;
                } else {
                    dir = reflect(dir, h.ns);  // total internal reflection: on inside
                    from = h.at + h.ns * s.skin;
                    out_normal = h.ns;
                    ++segment;
                    phase = segment < kInsideSegments ? kStepFirst : kStepLast;
                }
            }
        }
        if (shade) {
            seen = transmit_shade(last, seed);
        }
        if (way_kind == kWayWater) {
            // Out through its own far face onto no solid or cut-out thing: deep water, as an open sheet.
            deep = deep || (deep_test && !(last.far_ < kSeenReach));
            if (deep_test) {
                why_colour = deep ? float3(1.0f, 0.0f, 0.0f) : float3(0.0f, 0.0f, 1.0f);
            }
            // What the way under it lets through of what is seen (Pope and Fry's ratios; rt_water_absorb), and what
            // the water sends back in its own colour over that way.
            t = deep ? rt_water_scattered(1e30f, sun_v) : seen * rt_water_absorb(way) + rt_water_scattered(way, sun_v);
        } else if (way_kind == kWayWalls) {
            t = factor * seen;
            if (fall) {
                // Falling water: the sheet's way through it (its thickness over the refracted slant) is water, one
                // medium with the sea.
                const float sheet = kFallSheet / transmit_cos_inside(c_ior, saturate(-dot(n, look)));
                t = t * rt_water_absorb(sheet) + rt_water_scattered(sheet, sun_v);
            }
        } else {
            const float3 sigma = -log(colour) / thickness * kGlassTint;
            if (colours == 3u) {
                t[c] = seen[c] * exp(-sigma[c] * inside);
            } else {
                t = seen * exp(-sigma * inside);
            }
        }
    }
    if (why) {
        // Cyan: a water face stands on the eye's ray nearer than this layer's surface (another sheet is the one seen).
        RayDesc eye;
        eye.Origin = look * 1.0f;
        eye.Direction = look;
        eye.TMin = 0.0f;
        eye.TMax = max(length(s.at) - 2.0f * s.skin - 1.0f, 0.0f);
        RayQuery<RAY_FLAG_FORCE_NON_OPAQUE> nearer;
        nearer.TraceRayInline(g_scene, RAY_FLAG_FORCE_NON_OPAQUE, kMaskBlended, eye);
        bool sheet_in_front = false;
        while (nearer.Proceed()) {
            const uint other = rt_hit_item(nearer.CandidateInstanceID());
            if ((g_items.Load(g_at.x + other * kItemBytes + 44u) & 255u) == kClassWater) {
                sheet_in_front = true;
                nearer.Abort();
            }
        }
        g_out[id.xy] = float4((sheet_in_front ? float3(0.0f, 1.0f, 1.0f) : traced ? why_colour : float3(1.0f, 0.0f, 1.0f)) * 4.0f, 1.0f);
        return;
    }
    if (!traced) {
        g_out[id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
    rt_count(24u);  // refracted pixels (rt-inputs)
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
