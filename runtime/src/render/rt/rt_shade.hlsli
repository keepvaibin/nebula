// SPDX-License-Identifier: GPL-3.0-only
// Hit shading: what every ray that meets a surface knows of it and what light it sends back.
// One model for the real-time passes (the sky and bounce light, reflections, the light volume)
// and for the reference path tracer, so a difference between them is a difference of sampling, never of the model.
//
// Built on rt_common.hlsli (the frame's bindings, b0, the mesh readers, the surface images). A pass that shades hits
// binds the shading frame in t4 (rt_bounce.cpp rt_shade_binds): sixteen float4 of the stage and the model's tunables,
// then the world light grid; it calls rt_shade_begin() first. Everything is in the scene camera's eye space.
#ifndef NEBULA_RT_SHADE_HLSLI
#define NEBULA_RT_SHADE_HLSLI

#include "rt_common.hlsli"

// The shading frame (rt_bounce.cpp RtShadeFrame, field for field), at the head of t4.
struct RtShadeFrame {
    float4 ambient;   // rgb A: the stage's ambient colour as the programs take it (rt_stage_a_); w free
    float4 key;       // rgb K: the stage key's colour; w kShadowTint
    float4 fill;      // rgb Fl: the stage's fill colour; w free
    float4 sky;       // rgb what a ray that meets nothing at all shows; w kSkyGain
    float4 gains;     // x kAreaEmission, y kHitSky, z kBounce, w kBounceMax
    float4 axis[3];   // the world light grid: cell k along axis i = floor(dot(at, axis[i].xyz) + axis[i].w)
    float4 look;      // x kSkyMin, y kHitDark, z 1 / the dome's mean luminance (0: not known yet), w kSunGlint
    float4 authored;  // rgb the stage's ambient as the programs take it (rt_stage_a_): the indirect ratio's base;
                      // w the gain of a surface met (1 / the running mean of such hits, at most 1: rt_count_hit)
    uint4 grid;       // x the grid's cells' first byte in t4, y its entries', z 1 when it holds any, w the frame's number
    float4 dome;      // rgb 1 / the dome's mean colour as the bounce rays see it, w 1 when known (rt_sky_ratio)
    float4 form;      // the painted composite's look: x kForm, y the stage's m,
                      // z kDebake, w kAmbientLook (rt_share_terms)
    uint4 spare[3];
};
static RtShadeFrame g_shade;

// `at`: the frame's first byte in t4 (the pass constants carry it); the grid's offsets are from it.
void rt_shade_begin(uint at) {
    g_shade.ambient = asfloat(g_tiles.Load4(at));
    g_shade.key = asfloat(g_tiles.Load4(at + 16u));
    g_shade.fill = asfloat(g_tiles.Load4(at + 32u));
    g_shade.sky = asfloat(g_tiles.Load4(at + 48u));
    g_shade.gains = asfloat(g_tiles.Load4(at + 64u));
    g_shade.axis[0] = asfloat(g_tiles.Load4(at + 80u));
    g_shade.axis[1] = asfloat(g_tiles.Load4(at + 96u));
    g_shade.axis[2] = asfloat(g_tiles.Load4(at + 112u));
    g_shade.look = asfloat(g_tiles.Load4(at + 128u));
    g_shade.authored = asfloat(g_tiles.Load4(at + 144u));
    g_shade.grid = g_tiles.Load4(at + 160u);
    g_shade.grid.xy += uint2(at, at);
    g_shade.dome = asfloat(g_tiles.Load4(at + 176u));
    g_shade.form = asfloat(g_tiles.Load4(at + 192u));
    for (uint k = 0u; k < 3u; ++k) {
        g_shade.spare[k] = uint4(0u, 0u, 0u, 0u);
    }
}

// nebula_model.h NEBULA_HIT_* (the item table's word 11, low byte) and its flags (the next byte).
static const uint kRtDielectric = 0u, kRtGloss = 1u, kRtMetal = 2u, kRtGlass = 3u, kRtCutoutClass = 4u, kRtRamp = 5u,
                  kRtEmissive = 6u, kRtPainted = 7u, kRtTranslucent = 8u;
static const uint kRtHitVertexColour = 8u, kRtHitEnvironment = 16u;
// Item word 1: an area light (a glowing mesh too big to be a point of the emitter list).
static const uint kRtItemArea = 32u;
static const uint kRtItemBeing = 1u << 30u;  // a being's glowing body (rt_walk.cpp)
static const uint kRtItemHot = 1u << 31u;    // a hot body, lava or a sun (rt_walk.cpp; the raster's draw flag 8192)
static const float kRtPi = 3.14159265f;
static const float kRtFarAway = 200000.0f;

// How many frames a history holds, as a share of what it holds at 60 a second (rt_history_scale, rt_frame.hlsli:
// every history lasts the same time at any frame rate); 1 before that is in the frame's constants.
float fb_history_scale() {
#ifdef NEBULA_RT_HISTORY_SCALE
    return rt_history_scale();
#else
    return 1.0f;
#endif
}

float rt_lum(float3 c) {
    return dot(c, kLuminance);
}

// ---- Numbers: a pixel's own stream of independent ones (path tracing wants white noise: every sample its own).
uint rt_seed(uint2 pixel, uint frame, uint use) {
    return rt_hash(pixel.x * 1973u + pixel.y * 9277u + frame * 26699u + use * 7919u + 1u);
}

float rt_random(inout uint seed) {
    seed = rt_hash(seed + 0x9E3779B9u);
    return float(seed >> 8u) * (1.0f / 16777216.0f);
}

float2 rt_random2(inout uint seed) {
    const float a = rt_random(seed);
    return float2(a, rt_random(seed));
}

// ---- Directions.
// Two directions at right angles to n (Duff et al. 2017, "Building an Orthonormal Basis, Revisited").
void rt_basis(float3 n, out float3 t, out float3 b) {
    const float s = n.z >= 0.0f ? 1.0f : -1.0f;
    const float a = -1.0f / (s + n.z);
    const float k = n.x * n.y * a;
    t = float3(1.0f + s * n.x * n.x * a, s * k, -s * n.x);
    b = float3(k, s + n.y * n.y * a, -n.y);
}

// Over the half-space n faces, by the cosine (the chance of a direction is cos / pi).
float3 rt_cosine(float3 n, float2 u) {
    float3 t, b;
    rt_basis(n, t, b);
    const float r = sqrt(u.x), phi = 2.0f * kRtPi * u.y;
    return normalize(t * (r * cos(phi)) + b * (r * sin(phi)) + n * sqrt(max(1.0f - u.x, 0.0f)));
}

// Uniformly over the disc within `spread` (a tangent) of `toward`.
float3 rt_within(float3 toward, float spread, float2 u) {
    float3 t, b;
    rt_basis(toward, t, b);
    const float r = sqrt(u.x) * spread, phi = 2.0f * kRtPi * u.y;
    return normalize(toward + (t * cos(phi) + b * sin(phi)) * r);
}

// A normal of the GGX distribution as the view sees it (the distribution of Heitz 2018, "Sampling the GGX
// Distribution of Visible Normals", D_V(m) = G1(v) max(v.m, 0) D(m) / v.n), drawn by Dupuy and Benyoub 2023,
// "Sound and Robust Visible-Normal Sampling" (the visible hemisphere as the half vectors of the view and a point of a
// spherical cap): the same distribution with no basis about the view, about `n`, for the view `v` (toward the eye),
// alpha = roughness squared; the direction it reflects v to. A view under the shading horizon is taken at its horizon.
float3 rt_ggx_reflect(float3 n, float3 v, float alpha, float2 u) {
    float3 t, b;
    rt_basis(n, t, b);
    const float3 vl = float3(dot(v, t), dot(v, b), max(dot(v, n), 1e-4f));
    // Into the hemisphere's configuration (alpha 1), a point of the cap z in (-v.z, 1] there, their sum, and back.
    const float3 vs = normalize(float3(alpha * vl.x, alpha * vl.y, vl.z));
    const float phi = 2.0f * kRtPi * u.x;
    const float z = (1.0f - u.y) * (1.0f + vs.z) - vs.z;
    const float sin_theta = sqrt(saturate(1.0f - z * z));
    const float3 h = float3(sin_theta * cos(phi), sin_theta * sin(phi), z) + vs;
    const float3 m = normalize(float3(alpha * h.x, alpha * h.y, max(h.z, 1e-6f)));
    return reflect(-v, t * m.x + b * m.y + n * m.z);
}

// ---- Rays.
// The nearest thing of `mask` on the ray (cutouts where their maps say they are; the walk the sun pass takes,
// nearest_on_ray): its item, triangle, the weights of its second and third corners, its distance. False where nothing is.
bool rt_closest(float3 origin, float3 direction, float reach, uint mask, out uint item, out uint triangle_, out float2 where_,
                out float far_) {
    return nearest_on_ray(origin, direction, reach, mask, false, 0xFFFFFFFFu, item, triangle_, where_, far_);
}

// Whether the ray reaches `reach` with nothing solid (or a cutout where it is there) on it, the thing of slot `self`
// aside (a clump's own triangles). 0 or 1 today; translucent tints make it rgb.
// The first hit ends it (a shadow ray wants no nearest); with a `self`, that thing's triangles are passed over and the
// walk goes on past them (blocked_but), so what stands behind it still shadows. Both by one walk (shadow_walk's
// `plain` without a `self`).
float3 rt_through_of(float3 origin, float3 direction, float reach, uint self, uint mask) {
    const bool shut = shadow_walk(origin, direction, reach, mask, self, false, float4(0.0f, 0.0f, 0.0f, 0.0f), self == 0xFFFFFFFFu);
    return shut ? float3(0.0f, 0.0f, 0.0f) : float3(1.0f, 1.0f, 1.0f);
}

// A light's shadow ray (`self`: the light's slot, rt_emitter_light's; `place`: its place, centre and radius): its own
// body (the glowing parts of its model) passed over, the model's other parts shadow it beyond the light's radius
// (blocked_but's `body` and `sphere`); without a `self`, blocked's walk.
float3 rt_through(float3 origin, float3 direction, float reach, uint self, float4 place) {
    const bool shut = shadow_walk(origin, direction, reach, kMaskSolid | kMaskCutout, self, true, place, self == 0xFFFFFFFFu);
    return shut ? float3(0.0f, 0.0f, 0.0f) : float3(1.0f, 1.0f, 1.0f);
}

// What stands in the sun's way of a surface (as the sun pass): everything solid for a lit one; for a painted one
// lit things only when the painted style asks (g_look.y), what the painting could not know.
uint rt_sun_mask(bool lit) {
    return (lit || g_look.y == 0.0f ? kMaskSolid | kMaskCutout : kMaskLit) | kMaskShadowOnly;
}

bool rt_sun_traced() {
    return (g_more.z & 1u) != 0u;
}

// Where a ray from a pixel's surface starts: on the lit side s.origin, the smooth surface's point
// (Hanika 2021: rt_surface.hlsl's lift along the geometric normal) off it by the skin; into the far side (through a
// sheet, a body) the skin under the geometric surface, as rt_sun where n.s < 0.
float3 rt_ray_origin(RtSurface s, float3 dir) {
    return dot(dir, s.normal) >= 0.0f ? s.origin : s.at - s.normal * s.skin;
}

// The near field's reach (COMPLETENESS 1): 1 % of the eye distance, 20 to 60 units. rt_near.hlsl sees 0..r at
// full size, the half-size gather (High) from r on.
float rt_near_reach(float away) {
    return clamp(0.01f * away, 20.0f, 60.0f);
}

// A ray that gathers light: the nearest solid or cut-out thing within kIndirectReach; past that the sky's
// dome (a ray of its own over the dome alone); 2 a thing, 1 the dome, 0 nothing at all (the stage's ambient itself).
// Blended things (smoke, glows, glass) do not stop it: what light passes them is section 13.4's.
// The dome is drawn around the eye (rt_walk.cpp: its rows as drawn) and stands for what is at no distance: it is read
// along the ray's direction FROM THE EYE (rt_dome_ray), as its mean is measured (rt_count_dome_directions). From the
// ray's own start a dome smaller than the scene, or a start beyond it, would be missed or met at the wrong place.
static const float kIndirectReach = 20000.0f, kDomeReach = 4000000.0f;
bool rt_dome_ray(float3 direction, out uint item, out uint triangle_, out float2 where_, out float far_) {
    return rt_closest(float3(0.0f, 0.0f, 0.0f), direction, kDomeReach, kMaskSky, item, triangle_, where_, far_);
}
// The two rays as two legs of one loop (one ray query in the pass, not two): the thing, then the dome (rt_dome_ray's).
uint rt_gather(float3 origin, float3 direction, out uint item, out uint triangle_, out float2 where_, out float far_) {
    [loop] for (uint leg = 0u; leg < 2u; ++leg) {
        const bool dome = leg != 0u;
        if (rt_closest(dome ? float3(0.0f, 0.0f, 0.0f) : origin, direction, dome ? kDomeReach : kIndirectReach,
                       dome ? kMaskSky : kMaskSolid | kMaskCutout, item, triangle_, where_, far_)) {
            return dome ? 1u : 2u;
        }
    }
    return 0u;
}

// Rooms: a place the sun and the sky do not reach, decided by the point. No sun traced
// this frame: every point. Else an enclosed one (not merely something overhead; a crown, an arch, a bridge stay
// today's): three rays from kRoomNear out (what stands nearer, a body, a pot, a chair, is the near hits' and not a
// wall) each meet a solid or a cutout within kIndirectReach: along `n`, along the player's up (where the sky is) and
// toward the sun.
static const float kRoomNear = 100.0f;       // a gathered hit nearer than this stands over the point (a metre; 250 dimmed whole rooms)
static const float kRoomProbe = 250.0f;      // where a sunned frame's room probes start: past a body
static const float kRoomNearShare = 0.25f;   // what such a hit sends of its light in a room; full again by 2 kRoomNear
bool rt_room_point(RtSurface s, float3 n) {
    if (!rt_sun_traced()) {
        return true;
    }
    const float3 way[3] = {n, dot(g_up.xyz, g_up.xyz) > 0.25f ? normalize(g_up.xyz) : n, g_sun.xyz};
    [unroll] for (uint k = 0u; k < 3u; ++k) {
        uint met;
        if (!blocked(rt_ray_origin(s, way[k]) + way[k] * kRoomProbe, way[k], kIndirectReach - kRoomProbe, kMaskSolid | kMaskCutout,
                     true, met)) {
            return false;  // the sky that way: not a room
        }
    }
    return true;
}
// The share a gathered hit at `away` from the point sends in a room (1 past 2 kRoomNear or under open sky).
float rt_room_near(RtSurface s, float3 n, float away) {
    if (!(away < 2.0f * kRoomNear) || !rt_room_point(s, n)) {
        return 1.0f;
    }
    return lerp(kRoomNearShare, 1.0f, smoothstep(kRoomNear, 2.0f * kRoomNear, away));
}

// The water a gathered ray crosses within `reach`: sheets within kWaterLayer are one layer,
// absorbed by the way under water (kWaterAbsorb, the stage's clear), 1 - F head on once. Two layers or more:
// the ray crosses a body, the way between the outer two; one: it starts under it going up (the player's up: out, the
// way to it) or ends under it going down (from the bank to the bed, the way past it). 1 where it meets no water.
float3 rt_water_way(float3 origin, float3 dir, float reach) {
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = dir;
    ray.TMin = 0.0f;
    ray.TMax = reach;
    RayQuery<RAY_FLAG_FORCE_NON_OPAQUE> query;
    query.TraceRayInline(g_scene, RAY_FLAG_FORCE_NON_OPAQUE, kMaskBlended, ray);
    uint waters = 0u;
    float nearest = 3.0e38f, farthest = 0.0f;
    while (query.Proceed()) {
        if ((g_items.Load(g_at.x + rt_hit_item(query.CandidateInstanceID()) * kItemBytes + 44u) & 255u) != kClassWater) {
            continue;
        }
        const float t = query.CandidateTriangleRayT();
        if (waters == 0u || (abs(t - nearest) > kWaterLayer && abs(t - farthest) > kWaterLayer)) {
            ++waters;
            nearest = min(nearest, t);
            farthest = max(farthest, t);
        }
    }
    if (waters == 0u) {
        return float3(1.0f, 1.0f, 1.0f);
    }
    const float way = waters >= 2u ? farthest - nearest : (dot(dir, g_up.xyz) > 0.0f ? nearest : max(reach - nearest, 0.0f));
    return 0.98f * exp(-way / rt_water_clear() * kWaterAbsorb);
}

// ---- What a hit knows.
struct RtHit {
    float3 at, ng, ns;     // place, geometric normal, smooth normal (facing the ray)
    float2 uv;             // its first coordinate
    float3 vertex;         // its vertex colour (1 if none)
    uint item, slot;       // the frame's item, its object's slot
    uint klass, flags;     // NEBULA_HIT_* class and flags
    uint item_flags;       // the item table's word 1 (lit, thin, sky, area ...)
    float3 base;           // albedo estimate: record base * first map * vertex colour where flagged
    float alpha;           // first map's alpha
    float3 emission;       // what it shows with no light
    float roughness, metal, share, ior, thickness;
    float far;             // the ray's length to it
    bool glows;            // it shows itself and takes no light: an emissive-class surface alone (a ray sees what the
                           // screen shows)
    bool listed;           // its light is one of the frame's emitter list: a ray that lights something shades it instead
    bool glow_body;        // a glowing mesh that is a light this frame (word 14 bit 25): never duller than
                           // its emission to a ray that only looks
    bool being;            // a glowing body that is a being (item word 1 bit 30): less of its own colour
    float through;         // the sun's share on its far side (a crown, a cloud: word 14 bits 26-31; a card's
                           // kCardThrough); its sun ray passes its own item
};

float3 rt_bytes3(uint w) {
    return float3(float(w & 255u), float(w >> 8u & 255u), float(w >> 16u & 255u)) / 255.0f;
}

// Everything a hit knows, from its item's words (kItemBytes) and its triangle; the first map at a level of detail by the
// ray cone (Akenine-Moller et al. 2021, "Improved Shader and Texture Level of Detail Using Ray Cones"): `cone` is the
// spread of the ray (radians a unit of way), so its width at the hit is far * cone.
// A vertex colour de-lit by its cooked gain (traced_vertex_gain): r g b signed bytes of log2 x 42,
// by kDebake (the shading frame's form.z), bounded by its largest channel as the raster's vertex stage.
float3 rt_vertex_gain(float3 c, uint gain) {
    const float debake = g_shade.form.z;
    if (!(debake > 0.0f) || gain == 0u) {
        return c;
    }
    const int3 steps = int3(gain << 24u, gain << 16u, gain << 8u) >> 24;
    const float3 g = max(c, 0.0f) * lerp(float3(1.0f, 1.0f, 1.0f), exp2(float3(steps) / 42.0f), debake);
    return g / max(max(max(g.r, g.g), g.b), 1.0f);
}

// The same gain as a multiplier, for a mesh painted by its map alone (no vertex colour; programs.rs
// map_gained): 1 without it.
float3 rt_map_gain(uint gain) {
    const float debake = g_shade.form.z;
    if (!(debake > 0.0f) || gain == 0u) {
        return float3(1.0f, 1.0f, 1.0f);
    }
    const int3 steps = int3(gain << 24u, gain << 16u, gain << 8u) >> 24;
    return lerp(float3(1.0f, 1.0f, 1.0f), exp2(float3(steps) / 42.0f), debake);
}

void rt_hit_read(uint item, uint triangle_, float2 where_, float far_, float3 origin, float3 direction, float cone, out RtHit h) {
    const uint at = g_at.x + item * kItemBytes;
    const uint4 w0 = g_items.Load4(at), w1 = g_items.Load4(at + 16u), w2 = g_items.Load4(at + 32u), w3 = g_items.Load4(at + 48u);
    h.at = origin + direction * far_;
    h.ng = -direction;
    h.ns = -direction;
    h.uv = float2(0.0f, 0.0f);
    h.vertex = float3(1.0f, 1.0f, 1.0f);
    h.item = item;
    h.slot = w0.x;
    h.item_flags = w0.y;
    h.klass = w2.w & 255u;
    h.flags = w2.w >> 8u & 255u;
    h.roughness = float(w2.w >> 16u & 255u) / 255.0f;
    h.metal = float(w2.w >> 24u) / 255.0f;
    h.base = rt_bytes3(w3.x);
    h.share = float(w3.x >> 24u) / 255.0f;
    h.alpha = 1.0f;
    h.emission = float3(0.0f, 0.0f, 0.0f);
    // Its index (item word 15 bits 12-19, (ior - 1) x 100; 0: the class's own) and thickness (word 17, units).
    const uint ior_bits = w3.w >> 12u & 255u;
    h.ior = ior_bits != 0u ? 1.0f + float(ior_bits) / 100.0f : (h.klass == kRtGlass ? 1.5f : 1.33f);
    h.thickness = asfloat(g_items.Load(at + 68u));
    h.far = far_;
    float3 map = float3(1.0f, 1.0f, 1.0f);
    const uint arena = w0.z;
    if (arena < 8u) {
        const uint base = mesh_at(arena);
        const uint3 corner = triangle_corners(w0, w1, triangle_);
        float3 place[3], normal[3];
        float2 coordinate[3];
        uint colour[3], gain[3];
        const uint gains = g_items.Load(at + 76u);  // item word 19: the vertices' de-light gains
        for (uint c = 0u; c < 3u; ++c) {
            const uint row = vertex_rows(arena, base, w1.w, w2.z, corner[c]);
            const float4 r0 = asfloat(g_rows.Load4(row)), r1 = asfloat(g_rows.Load4(row + 16u)), r2 = asfloat(g_rows.Load4(row + 32u));
            const float3 n0 = asfloat(g_rows.Load3(row + 48u)), n1 = asfloat(g_rows.Load3(row + 64u)), n2 = asfloat(g_rows.Load3(row + 80u));
            const float4 p = float4(asfloat(arena_words(arena, base + w1.z + corner[c] * 12u)), 1.0f);
            place[c] = float3(dot(r0, p), dot(r1, p), dot(r2, p));
            // A vertex's attributes, 24 bytes: its normal (three floats), its colour (four bytes), its first coordinate.
            const float3 n = asfloat(arena_words(arena, base + w0.w + corner[c] * 24u));
            normal[c] = float3(dot(n0, n), dot(n1, n), dot(n2, n));
            const uint3 b = arena_words(arena, base + w0.w + corner[c] * 24u + 12u);
            colour[c] = b.x;
            gain[c] = gains != 0u ? arena_word(arena, base + (gains & 0x0FFFFFFFu) * 4u + corner[c] * ((gains >> 28u) * 4u)) : 0u;
            coordinate[c] = asfloat(b.yz);
        }
        const float u = 1.0f - where_.x - where_.y, v = where_.x, w = where_.y;
        const float3 across = cross(place[1] - place[0], place[2] - place[0]);
        const float area = length(across);
        if (area > 1e-9f) {
            const float3 n = across / area;
            h.ng = dot(n, direction) < 0.0f ? n : -n;
            h.ns = h.ng;
            float3 smooth_ = normal[0] * u + normal[1] * v + normal[2] * w;
            const float size = dot(smooth_, smooth_);
            if (size > 1e-12f) {
                smooth_ *= rsqrt(size);
                // Vertex normals that do not go with the surface they are on are not its shape (as the surface pass).
                if (abs(dot(smooth_, h.ng)) >= 0.5f) {
                    h.ns = dot(smooth_, h.ng) < 0.0f ? -smooth_ : smooth_;
                }
            }
        }
        h.uv = coordinate[0] * u + coordinate[1] * v + coordinate[2] * w;
        if ((h.flags & kRtHitVertexColour) != 0u) {
            float3 vc = float3(0.0f, 0.0f, 0.0f);
            for (uint k = 0u; k < 3u; ++k) {
                vc += rt_vertex_gain(float3(float(colour[k] & 255u), float(colour[k] >> 8u & 255u), float(colour[k] >> 16u & 255u)) / 255.0f,
                                     gain[k]) *
                      (k == 0u ? u : k == 1u ? v : w);
            }
            h.vertex = vc * kVertexGrey;
        }
        if (w2.x != 0xFFFFFFFFu && area > 1e-9f) {
            uint map_width, map_height;
            g_textures[NonUniformResourceIndex(w2.x)].GetDimensions(map_width, map_height);
            const float2 e1 = coordinate[1] - coordinate[0], e2 = coordinate[2] - coordinate[0];
            const float texels = abs(e1.x * e2.y - e1.y * e2.x) * float(map_width) * float(map_height);
            const float wide = rt_cone_width(origin, far_, cone) / max(abs(dot(h.ng, direction)), 0.05f);
            const float level = max(0.5f * log2(max(texels / area, 1e-12f)) + log2(wide), 0.0f);
            const float4 m = g_textures[NonUniformResourceIndex(w2.x)].SampleLevel(g_wrap, h.uv, level);
            map = m.rgb;
            h.alpha = m.a;
        }
        // A mesh painted by its map alone (gains, no vertex colour): its map de-lit as the raster's traced form,
        // bounded by its largest channel (traced_map_shown).
        if ((h.flags & kRtHitVertexColour) == 0u && gains != 0u) {
            const float3 g = max(map, 0.0f) * (rt_map_gain(gain[0]) * u + rt_map_gain(gain[1]) * v + rt_map_gain(gain[2]) * w);
            map = g / max(max(max(g.r, g.g), g.b), 1.0f);
        }
    }
    h.base = h.base * map * h.vertex;
    // A ray sees what the screen shows: an emissive-class surface its emission (a ray that lights something
    // shades it where it is a light of the list: word 14 bit 24), any other class its material, shaded, whatever glow
    // row it has. One light, one way: a listed glow lights the others through the list alone; an area glow (word 1: 32,
    // never listed) by the rays that light something and meet it (rt_hit_area_glow).
    const bool area = (w0.y & kRtItemArea) != 0u;
    h.listed = (w3.z >> 24u & 1u) != 0u && !area;
    h.glows = h.klass == kRtEmissive;
    h.glow_body = (w3.z >> 25u & 1u) != 0u;
    h.being = h.glow_body && (w0.y & kRtItemBeing) != 0u;
    const float cooked_through = float(w3.z >> 26u) / 63.0f;
    h.through = cooked_through > 0.0f ? cooked_through
                                      : (w0.y & kItemThin) != 0u && (w0.y & kItemAlphaTested) != 0u ? kCardThrough : 0.0f;
    if (h.glows || area || h.glow_body) {
        // The record's emission is in the base's convention (hit_record.rs read(): its colour map's grey undone at the
        // cook, the vertex's grey kept): what it shows is it by the map and by the vertex's colour (h.vertex: kVertexGrey
        // x the colour where flagged), as h.base. No doubling here (it doubled again what the cook had undone; the
        // hue-keeping bound went with it).
        const float3 own = rt_bytes3(w3.z);
        const float own_most = max(own.r, max(own.g, own.b));
        h.emission = (own_most > 0.02f ? own * map * h.vertex : h.base);
    }
}

// The sky's dome where a ray meets it: its first map, nothing else, in linear light, the gain on the light.
float3 rt_dome_light(RtHit h) {
    return rt_lin(h.base) * g_shade.sky.w;
}

// What the dome shows along a direction, as a ratio to the stage's authored ambient (11.1: S).
// Against the dome's mean colour, a channel at a time, once it is known: open sky averages 1, its colour
// included (the art's ambient already is the sky's colour); before that against the mean luminance as at first.
// Whole: a reservoir's light and p_hat take the sample as it is; the guard is the mean's clamp after the filters
// A floor so p_hat stays positive.
// Under the relight (kForm > 0): against the dome's mean LUMINANCE, at kSkyChroma of its
// chroma, lerp(luma(L), L, kSkyChroma) / mean: the sky's hue enters O, once (sky' takes luminance only); a uniform dome
// gives luminance 1 (the furnace). Painted (kForm 0) as before, to the bit.
static const float kSkyChroma = 0.4f;     // the sky's colour in shade at this share of its chroma (October's)
float rt_sky_chroma() {
    return rt_dev_or(kDevDomeChroma, kSkyChroma);
}
// The dome's mean luminance as rt_sky_ratio divides by it (the mean colour's luminance once known: luminance is linear).
float rt_dome_mean_lum(float3 dome) {
    if (g_shade.dome.w > 0.0f) {
        return max(rt_lum(1.0f / max(g_shade.dome.rgb, 1e-6f)), 1e-3f);
    }
    return g_shade.look.z > 0.0f ? 1.0f / g_shade.look.z : max(rt_lum(dome), 1e-3f);
}
float3 rt_sky_ratio(float3 dome) {
    float3 art;
    if (g_shade.dome.w > 0.0f) {
        art = max(dome * g_shade.dome.rgb, 1e-3f);
    } else {
        const float scale = g_shade.look.z > 0.0f ? g_shade.look.z : 1.0f / max(rt_lum(dome), 1e-3f);
        art = max(dome * scale, 1e-3f);
    }
    const float t = saturate(g_shade.form.x);
    if (!(t > 0.0f)) {
        return art;
    }
    const float3 model = max(lerp(rt_lum(dome).xxx, dome, rt_sky_chroma()) / rt_dome_mean_lum(dome), 1e-3f);
    return lerp(art, model, t);
}
// The open sky's hue, its mean colour over its luminance (1 until the mean is known).
float3 rt_sky_hue() {
    if (!(g_shade.dome.w > 0.0f)) {
        return float3(1.0f, 1.0f, 1.0f);
    }
    const float3 mean = 1.0f / max(g_shade.dome.rgb, 1e-6f);
    return mean / max(rt_lum(mean), 1e-3f);
}
// The O an open, unshadowed surface takes under the relight: the cosine mean of rt_sky_ratio's model; 1
// Painted.
float3 rt_open_o() {
    const float t = saturate(g_shade.form.x);
    return t > 0.0f ? lerp(float3(1.0f, 1.0f, 1.0f), lerp(float3(1.0f, 1.0f, 1.0f), rt_sky_hue(), rt_sky_chroma()), t)
                    : float3(1.0f, 1.0f, 1.0f);
}

// The dome's luminance into the counters (fixed point, kDomeUnit a unit, each direction held to 8: the 4096 directions
// of rt_count_dome_directions sum inside 32 bits) for the host's running mean (11.1; rt_bounce.cpp kDomeUnit).
static const float kDomeUnit = 65536.0f;  // a dark dome (linear 0.01) to 1/655
void rt_count_dome(float3 dome) {
    // Rounded: a dark dome's linear values are small.
    rt_count_sum(44u, uint(clamp(rt_lum(dome), 0.0f, 8.0f) * kDomeUnit + 0.5f));
    rt_count(45u);
    // Its colour, a channel at a time (rt_sky_ratio).
    const uint3 c = uint3(clamp(dome, 0.0f, 8.0f) * kDomeUnit + 0.5f);
    rt_count_sum(48u, c.r);
    rt_count_sum(49u, c.g);
    rt_count_sum(50u, c.b);
}

// The dome's mean as a property of the stage's sky: kDomeDirections fixed directions, cosine-weighted
// about the player's up, traced from the eye against the sky alone; the indirect pass and the reference each count
// the same set (threads 0 to 63 by 0 to 63), so both divide by the same number and open ground at the player reads 1.
// A direction that meets no dome is not counted.
static const uint kDomeDirections = 4096u;
void rt_count_dome_directions(uint2 id) {
    if (any(id >= uint2(64u, 64u)) || !(dot(g_up.xyz, g_up.xyz) > 0.25f)) {
        return;
    }
    const uint k = id.y * 64u + id.x;
    const float2 u = float2((float(k) + 0.5f) / float(kDomeDirections), frac(float(k) * 0.6180339887f));
    const float3 dir = rt_cosine(normalize(g_up.xyz), u);
    uint item, triangle_;
    float2 where_;
    float far_;
    if (!rt_dome_ray(dir, item, triangle_, where_, far_)) {
        return;
    }
    RtHit h;
    rt_hit_read(item, triangle_, where_, far_, float3(0.0f, 0.0f, 0.0f), dir, 0.05f, h);
    rt_count_dome(rt_dome_light(h));  // the mean in linear light, as every ratio to it
}

// A surface met (not shining of itself) as a ratio before the gain, kBounce * L / A, into the counters 46 and 47 for the
// host's running mean of such hits: a surface met counts against it, as the sky against the dome's mean. Held
// to 16 so a 4K frame's sum stays inside 32 bits with the reference counting too.
void rt_count_hit(float3 ratio) {
    rt_count_sum(46u, uint(clamp(rt_lum(ratio), 0.0f, 16.0f) * 256.0f));
    rt_count(47u);
}

// ---- Light.
// Emitter `index` of the frame's list at a surface, unshadowed, as a share of the surface's own colour
// (rt_common.hlsli emitter_light: its facing, n.l, no wrap).
float3 rt_emitter_light(uint index, float3 at, float3 n, uint self, out float4 place, out uint slot) {
    return emitter_light(g_glow.x + index * 48u, at, n, self, place, slot);
}

// Where a light's shadow ray goes: to a point of its disc facing the surface (8.5 R-3), stopping short of its body.
float3 rt_toward_light(float4 place, float3 from, float2 u, out float reach) {
    const float3 to = place.xyz - from;
    const float far_ = length(to);
    const float3 dir = to / max(far_, 1e-3f);
    const float3 aim = place.w > 50.0f ? rt_within(dir, place.w / max(far_, place.w), u) : dir;
    reach = far_ - min(max(place.w, 10.0f), 60.0f);
    return aim;
}

// ---- October's light model: linear light in the stage's own units.
static const float kSunStrength = 1.25f;  // October's 1.25 (was 1)
// O as the model takes it: its luminance and kShadowTint of its hue,
// (1 - t) lum(O) + t O; linear in O (rt_o_prime_m is its matrix).
float3 rt_o_prime(float3 o) {
    const float t = g_shade.key.w;
    return (1.0f - t) * rt_lum(o) + t * o;
}
float3x3 rt_o_prime_m() {
    const float t = g_shade.key.w;
    const float3 l = (1.0f - t) * float3(0.2126f, 0.7152f, 0.0722f);
    return float3x3(l.x + t, l.y, l.z, l.x, l.y + t, l.z, l.x, l.y, l.z + t);
}
// The relight's share of the model: 0 the art's share (Painted), 1 the model.
float rt_form() {
    return saturate(g_shade.form.x);
}
// What is drawn and seen as it is (the dome, the empty sky, a glow's colour) in the unit hits are in: linear.
float3 rt_shown(float3 c) {
    return rt_lin(c);
}
// The stage's light in the picture's linear units (a white surface facing up under the full light shows 1):
// sky = lin(A) E / full, sun = (full - sky) kSunStrength E / full, E the exposure that pins that white surface to the
// game's own full light, the sun at no less than 30 degrees up (October's physical_exposure).
// `lit_sky`, `lit_sun`, `held`: the same before the division by the held light, for the bounded share
// (rt_relit_share).
struct RtStageLight {
    float3 sky, sun, unit;
    float3 lit_sky, lit_sun, held;
};
RtStageLight rt_stage_light() {
    const float3 a = g_shade.ambient.rgb, k = g_shade.key.rgb;
    const float3 sky = rt_lin(a);
    const float3 full = rt_lin(saturate(a + k));
    const float3 rest = max(full - sky, 0.0f);
    // up.s as the programs take it: view_look.y (the shading frame's form.y, the same rt_look_).
    const float up = g_shade.form.y;
    const float e = rt_lum(full) / max(rt_lum(sky) + rt_lum(rest) * up, 1e-4f);
    // The sky in shade by kAmbientLook, a channel at a time at kSkyChroma of its chroma (the
    // shade's hue is the authored ambient's; O enters as O', rt_o_prime); a channel with no full
    // light: share 1. The run's keys where it gives them, as the programs': sun_strength, sky_chroma,
    // stage_light (1: the stage's light as it is, 0: divided by the full light).
    const float sun_strength = rt_dev_or(kDevSunStrength, kSunStrength);
    const float3 held = lerp(full, float3(1.0f, 1.0f, 1.0f), rt_dev_or(kDevStageLight, 0.0f));
    const float3 sky_look = g_shade.form.w * lerp(rt_lum(sky).xxx, sky, rt_dev_or(kDevSkyChroma, 0.4f));
    const float3 ok = float3(held >= 1e-4f);
    RtStageLight s;
    s.sky = ok * (sky_look * e / max(held, 1e-4f)) + (1.0f - ok);
    s.sun = ok * (rest * sun_strength * e / max(held, 1e-4f));
    // O's unit: the plain sky's light (no kAmbientLook), its luminance (a white hit under open sky returns its
    // O, hue and all), so the dials move the shade and not O; at least the shade's sky a channel at a time, so the
    // loop's gain stays under the albedo.
    s.unit = ok * max(max(rt_lum(sky) * e / max(held, 1e-4f), s.sky), 1e-3f) + (1.0f - ok);
    s.lit_sky = sky_look * e;
    s.lit_sun = rest * sun_strength * e;
    s.held = held;
    return s;
}
// The relit share as the programs take it (program.hlsli traced_bounded,
// view_relight): lit = sky' O' + kSun key max(n.s, 0) V (times E), its luminance over the held light's, its colour
// over the held light's colour within [1 / bound, bound] a channel (a channel of the stage's light near 0 does
// not blow up). `bound`: the draw's (rt_tint_bound). 1 where the held light is nothing.
static const float kTintBoundEnvironment = 2.0f, kTintBoundCharacter = 1.25f;
// A hit's bound by the kind of its light set (item word 1 bits 28, 29: the raster's draw flags 2048, 4096; program.hlsli
// traced_tint_bound), and with neither the model's rule: a lit program a character's, a painted surface the
// environment's.
float rt_tint_bound(uint item_flags) {
    if ((item_flags & kItemSetCharacter) != 0u) {
        return kTintBoundCharacter;
    }
    if ((item_flags & kItemSetPlanet) != 0u) {
        return kTintBoundEnvironment;
    }
    return (item_flags & kItemLit) != 0u ? kTintBoundCharacter : kTintBoundEnvironment;
}
float3 rt_relit_share(RtStageLight st, float3 o, float nl, float3 v, float bound) {
    const float held_l = rt_lum(st.held);
    if (!(held_l > 1e-4f)) {
        return float3(1.0f, 1.0f, 1.0f);
    }
    const float3 lit = st.lit_sky * rt_o_prime(o) + st.lit_sun * (max(nl, 0.0f) * v);
    const float lit_l = rt_lum(lit);
    const float3 tint = lit_l > 1e-6f ? (lit / lit_l) / max(st.held / held_l, 1e-4f) : float3(1.0f, 1.0f, 1.0f);
    return (lit_l / held_l) * clamp(tint, 1.0f / bound, bound);
}
// O's unit (C4: the one divisor of every gather return): what O = 1 gives a white surface. The art's: the authored
// ambient; the model's: the sky's light.
float3 rt_o_unit() {
    const float t = rt_form();
    // The art's: 1, a painted hit's O-part is in the art's own units (O = 1: as painted), so one trip round the bounce's
    // loop multiplies O by lin(base), the albedo.
    const float3 art = float3(1.0f, 1.0f, 1.0f);
    return t > 0.0f ? lerp(art, rt_stage_light().unit, t) : art;
}
// A hit's colour as it takes light: the art's, or (the model) its linear colour, a metal's diffuse gone. It is
// rho * full, the reflectance against the stage's full light, at most kAlbedoMost a channel: the
// bounce's loop gain per trip is under it.
static const float kAlbedoMost = 0.9f;  // CHOSEN: fresh snow's albedo
float3 rt_hit_albedo(RtHit h) {
    const float t = rt_form();
    float3 base = min(rt_lin(h.base), kAlbedoMost);  // linear always
    // A non-metal's purity bound, as program.hlsli traced_pure.
    if (h.klass != kRtMetal) {
        base = max(base, 0.04f * max(max(base.r, base.g), base.b));
    }
    return t > 0.0f ? base * (1.0f - t * h.metal) : base;
}
// What a hit RETURNS to a ray that lights something (the gather): under the model
// its colour at kBounceAlbedo, real ground's range (sand 0.35-0.45, grass 0.2-0.25, white paint 0.8: the art's 0.9
// white at 0.45); on screen, in a mirror, through water: rt_hit_albedo, the display's anchor. The art's form keeps
// its own (by kForm).
static const float kBounceAlbedo = 0.5f;  // CHOSEN: the art's white at 0.45, its grass at 0.3
float3 rt_return_albedo(RtHit h, bool lighting) {
    return lighting ? rt_hit_albedo(h) * lerp(1.0f, kBounceAlbedo, rt_form()) : rt_hit_albedo(h);
}
// A metal or glass hit's reflection, which hits do not trace again: the stage's ambient by its painted share (the
// art's), or the sky's light by its F0 (the model: lerp(0.04, lin(base), metallic); glass by its index).
float3 rt_hit_mirror_stand_in(RtHit h) {
    const float t = rt_form();
    const float3 art = rt_lin(g_shade.ambient.rgb) * h.share;
    if (!(t > 0.0f)) {
        return art;
    }
    const float r = (h.ior - 1.0f) / (h.ior + 1.0f);
    const float3 f0 = h.klass == kRtMetal ? lerp(float3(0.04f, 0.04f, 0.04f), rt_lin(h.base), h.metal) : float3(r * r, r * r, r * r);
    // What a mirror sees is the plain sky, no shade dial, at its whole hue.
    return lerp(art, rt_stage_light().unit * rt_sky_hue() * f0, t);
}

// A surface's share of light as an affine function of what the sky and the bounce give it (O, rgb): share = s0 +
// Mo * O. Painted surfaces: P_c = (A_c O_c + K_c nl V_c) / (A_c + K_c nl), its luminance form likewise with
// the channel means, P = lerp(P_lum, P_c, kShadowTint). Lit surfaces: A O + K nl V + Fl 0.5 O. `nl`
// from the smooth normal. Linear in O and V, so a mean over samples of the share is the share of the means. All in
// linear light: one trip round the bounce's loop multiplies O by at most the hit's albedo.
void rt_share_terms_art(bool lit, float nl, float3 v, out float3 s0, out float3x3 mo) {
    const float3 a = g_shade.ambient.rgb, k = g_shade.key.rgb;
    if (lit) {
        // Linear light: lin(D) O + (lin(D + K nl) - lin(D)) V, D = A + Fl / 2; the loop's gain lin(D) lin(base).
        const float3 d = rt_lin(saturate(a + 0.5f * g_shade.fill.rgb));
        s0 = max(rt_lin(saturate(a + 0.5f * g_shade.fill.rgb + k * max(nl, 0.0f))) - d, 0.0f) * v;
        mo = float3x3(d.x, 0.0f, 0.0f, 0.0f, d.y, 0.0f, 0.0f, 0.0f, d.z);
        return;
    }
    // The painted composite's share: the art is taken to hold
    // A + K f, f the sun's facing as the light pass gives a painted surface, in LINEAR light (O and V are ratios of
    // linear light): share = (lin(A) O + (lin(A + K f) - lin(A)) V) / lin(A + K f), lerp(mean, a channel at a time,
    // kShadowTint). Linear in O: s0 + M O; O in shade, 1 at O = V = 1.
    const float t = g_shade.key.w;
    const float f = rt_sun_traced() ? saturate(nl / max(g_look.z, 1e-3f)) : 1.0f;
    const float ga = 1.0f;
    const float3 a_lin = rt_lin(a);
    const float3 held = rt_lin(saturate(a + k * f));
    const float3 key_lin = max(held - a_lin, 0.0f);
    const float lum_held = rt_lum(held);
    const float3 ok = float3(held > 1e-4f);  // a channel the art holds nothing of: 1
    const float3 own_s0 = ok * (key_lin * v / max(held, 1e-4f)) + (1.0f - ok);
    const float3 own = ok * (ga * a_lin / max(held, 1e-4f));
    const bool whole = lum_held > 1e-4f;
    const float mean_s0 = whole ? rt_lum(key_lin * v) / lum_held : 1.0f;
    const float3 shared_ = whole ? ga * float3(0.2126f, 0.7152f, 0.0722f) * a_lin / lum_held : float3(0.0f, 0.0f, 0.0f);
    s0 = (1.0f - t) * mean_s0 + t * own_s0;
    const float3 row = (1.0f - t) * shared_;  // every channel takes lum(A O) alike: the same row of columns
    mo = float3x3(row.x + t * own.x, row.y, row.z, row.x, row.y + t * own.y, row.z, row.x, row.y, row.z + t * own.z);
}

// The share in linear light, affine in O for the reference (ref_surface: c + M O; the art's share is linear light
// already), then the model mixed in by kForm, a lit surface's as a painted one's (s0 = sun
// max(n.s, 0) V, M = the sky's light). The model's part here is UNBOUNDED: rt_share bounds its tint (rt_relit_share),
// which is not affine in O, so the two agree only where the bound does not bind (the
// reference keeps this affine form for gathered hits).
void rt_share_terms(bool lit, float nl, float3 v, out float3 s0, out float3x3 mo) {
    rt_share_terms_art(lit, nl, v, s0, mo);
    const float t = rt_form();
    if (t > 0.0f) {
        const RtStageLight st = rt_stage_light();
        s0 = lerp(s0, st.sun * max(nl, 0.0f) * v, t);
        mo = mo * (1.0f - t) + mul(float3x3(st.sky.x, 0.0f, 0.0f, 0.0f, st.sky.y, 0.0f, 0.0f, 0.0f, st.sky.z), rt_o_prime_m()) * t;
    }
}

// The air's share of the stage's light: the sun's part a unit of V gives (rt_air_sun) and the whole with
// the sky's part by s (rt_air_share). The art's a channel at a time in linear light, the model's under the
// relight: the sun on a white surface square to it (E_perp / pi; no n.s, no u).
float3 rt_air_sun() {
    const float3 sky = rt_lin(g_shade.authored.rgb);
    const float3 full = rt_lin(saturate(g_shade.authored.rgb + g_shade.key.rgb));
    const float3 ok = float3(full >= 1e-4f);
    const float3 art = ok * max(full - sky, 0.0f) / max(full, 1e-4f);
    const float t = rt_form();
    return t > 0.0f ? lerp(art, rt_stage_light().sun, t) : art;
}
float3 rt_air_share(float s, float3 v) {
    const float3 sky = rt_lin(g_shade.authored.rgb);
    const float3 full = rt_lin(saturate(g_shade.authored.rgb + g_shade.key.rgb));
    const float3 ok = float3(full >= 1e-4f);
    float3 sky_part = ok * sky / max(full, 1e-4f);
    const float t = rt_form();
    if (t > 0.0f) {
        sky_part = lerp(sky_part, rt_stage_light().sky * rt_o_prime(rt_open_o()), t);  // the open sky's O
    }
    return sky_part * s + rt_air_sun() * v;
}

// The share a hit takes: the art's in linear light, lerped to the model by kForm (lit or painted
// alike); the model's bounded as the programs bound it (rt_relit_share, `bound` the draw's).
float3 rt_share(bool lit, float nl, float3 v, float3 o, float bound) {
    float3 s0;
    float3x3 mo;
    rt_share_terms_art(lit, nl, v, s0, mo);
    const float t = rt_form();
    const float3 art = max(s0 + mul(mo, o), 0.0f);
    if (!(t > 0.0f)) {
        return art;
    }
    return lerp(art, rt_relit_share(rt_stage_light(), o, nl, v, bound), t);
}

// (Without it, Freezeflame's ice showed Mario 6.81 times as bright as
// the screen does). A ray that only looks at a lit program's surface sees it within kLitSeenLow..kLitSeenHigh of its
// light set's own level, in luminance: the art's share (the shading frame's set: lin(A + Fl/2) O + (lin(A + Fl/2 + K nl)
// - lin(A + Fl/2)) V), as the screen draws it by its set (program_lit.hlsli traced_set_level, program.hlsli's
// character bound 0.8 .. 1.25); its hue the model's. Painted (kForm 0): the art's share already, as it was.
static const float kLitSeenLow = 0.8f, kLitSeenHigh = 1.25f;
float3 rt_lit_seen_bound(float nl, float3 v, float3 o, float3 share_) {
    if (!(rt_form() > 0.0f)) {
        return share_;
    }
    float3 s0;
    float3x3 mo;
    rt_share_terms_art(true, nl, v, s0, mo);
    const float art_l = rt_lum(max(s0 + mul(mo, o), 0.0f));
    const float l = rt_lum(share_);
    if (!(art_l > 1e-4f) || !(l > 1e-6f)) {
        return share_;
    }
    return share_ * (clamp(l, kLitSeenLow * art_l, kLitSeenHigh * art_l) / l);
}

// From just off a surface: along its geometric normal, more the farther from the eye (depth's steps grow).
float3 rt_off(float3 at, float3 ng) {
    return at + ng * max(0.003f * length(at), 0.5f);
}

// One sun shadow ray from a hit: toward the sun itself, or a point of its disc (`u`, the reference).
float3 rt_hit_sun(RtHit h, bool disc, float2 u) {
    if (!rt_sun_traced()) {
        return float3(1.0f, 1.0f, 1.0f);  // no sun traced: the key is the authored light
    }
    const float3 toward = disc ? rt_within(g_sun.xyz, g_sun.w, u) : g_sun.xyz;
    const bool thin = (h.item_flags & kItemThin) != 0u;
    const bool clump = h.through > 0.0f;  // a crown's, a cloud's, a card's ray passes its own item
    const float3 n = (thin || clump) && dot(h.ng, g_sun.xyz) < 0.0f ? -h.ng : h.ng;
    const float3 from = rt_off(h.at, n);
    float3 v = rt_through_of(from, toward, kRtFarAway, clump ? h.slot : 0xFFFFFFFFu, rt_sun_mask((h.item_flags & kItemLit) != 0u));
    // What the blended things let through, at High as the sun pass (a hit under water takes the sun through it).
    if (rt_part(kPartSun) >= 3u && any(v > 0.0f)) {
        float first;
        v *= let_through(from, toward, kRtFarAway, first);
    }
    return v;
}

float rt_hit_facing(RtHit h) {
    const float turned = dot(h.ns, g_sun.xyz);
    if (turned < 0.0f && h.through > 0.0f) {
        return -h.through * turned;  // the far side of a crown, a cloud, a card by its share
    }
    return max((h.item_flags & kItemThin) != 0u ? abs(turned) : turned, 0.0f);
}

// ---- The world light grid: for hits off the screen, the lights whose reach touches the hit's cell.
// Two of the cell's lights by resampled importance sampling; one shadow ray when `shadowed`. Lights outside the grid
// (over 60 m away) are not seen by bounce rays.
float3 rt_grid_light(float3 at, float3 n, uint self, bool shadowed, inout uint seed) {
    if (g_shade.grid.z == 0u) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    const int3 cell = int3(floor(float3(dot(at, g_shade.axis[0].xyz) + g_shade.axis[0].w, dot(at, g_shade.axis[1].xyz) + g_shade.axis[1].w,
                                        dot(at, g_shade.axis[2].xyz) + g_shade.axis[2].w)));
    if (any(cell < int3(0, 0, 0)) || any(cell >= int3(32, 16, 32))) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    const uint2 run = g_tiles.Load2(g_shade.grid.x + uint((cell.z * 16 + cell.y) * 32 + cell.x) * 8u);
    if (run.y == 0u) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    float sum = 0.0f, kept_target = 0.0f;
    float3 kept = float3(0.0f, 0.0f, 0.0f);
    float4 kept_place = float4(0.0f, 0.0f, 0.0f, 0.0f);
    uint kept_slot = 0u;
    for (uint k = 0u; k < 2u; ++k) {
        const uint entry = g_tiles.Load(g_shade.grid.y + (run.x + min(uint(rt_random(seed) * float(run.y)), run.y - 1u)) * 4u);
        float4 place;
        uint slot;
        const float3 light = rt_emitter_light(entry & 0xFFFFu, at, n, self, place, slot) * float(max(entry >> 16u, 1u));
        const float target = rt_lum(light);
        if (!(target > 0.0f)) {
            continue;
        }
        const float weight = target * float(run.y);
        sum += weight;
        if (rt_random(seed) * sum < weight) {
            kept = light;
            kept_target = target;
            kept_place = place;
            kept_slot = slot;
        }
    }
    if (!(kept_target > 0.0f)) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    float3 estimate = kept * (sum / (2.0f * kept_target));
    if (shadowed) {
        const float3 from = rt_off(at, n);
        float reach;
        const float3 toward = rt_toward_light(kept_place, from, rt_random2(seed), reach);
        if (reach > 1.0f) {
            estimate *= rt_through(from, toward, reach, kept_slot, kept_place);
        }
    }
    return estimate;
}

// ---- What a hit shows, for the real-time passes. Options:
static const uint kRtShadeSunRay = 1u;      // trace the sun from the hit (Medium, High); else Vh = 1 where it faces it
static const uint kRtShadeLightRay = 2u;    // the grid's light shadowed (High)
static const uint kRtShadeSkyOnly = 8u;     // Low: a hit is kHitDark, nothing read
static const uint kRtShadeLighting = 16u;   // a ray that lights something: a listed glow lights through the list, not by its hit

// Where the hit is on the screen and the screen shows that very surface: its place projects inside the
// picture at the depth `guide` (this frame's surface guide) holds there, within 2 % + 2 units. `p` its pixel.
bool rt_screen_pixel(Texture2D<float4> guide, float3 at, out int3 p) {
    p = int3(0, 0, 0);
    const float away = -at.z;
    if (!(away > 1.0f)) {
        return false;
    }
    const float2 pixel = float2((at.x / away - g_ray.y) / g_ray.x, (at.y / away - g_ray.w) / g_ray.z);
    if (!in_viewport(pixel)) {
        return false;
    }
    p = int3(int2(pixel), 0);
    const float4 seen = guide.Load(p);
    return seen.a != 0.0f && abs(seen.r - away) <= 0.02f * away + 2.0f;
}
// The same, and the pixel shows the hit's own thing on the hit's side: the guide's slot is the hit's
// (rt_surface.hlsl: slot & 1023, slot >> 10) and its smooth normal within 25 degrees of the hit's (`n`: h.ns, which
// faces the ray; the guide's faces the eye: for a surface both see, the same side). Depth alone took another object's,
// or a sheet's other side's, light at the same depth.
static const float kScreenSameFacing = 0.906f;  // cos 25 degrees
bool rt_screen_pixel(Texture2D<float4> guide, float3 at, uint slot, float3 n, out int3 p) {
    if (!rt_screen_pixel(guide, at, p)) {
        return false;
    }
    const float4 seen = guide.Load(p);
    return uint(seen.g) + uint(seen.b) * 1024u == slot && dot(rt_unpack_normal(seen.a), n) >= kScreenSameFacing;
}

// Last frame's indirect ratio where the hit is on the screen and the screen shows that very surface (more than one
// bounce over time, 10.2 item 5): `guide` this frame's surface guide, `sky` last frame's I out.
bool rt_screen_cache(Texture2D<float4> guide, Texture2D<float4> sky, float3 at, out float3 o) {
    o = float3(1.0f, 1.0f, 1.0f);
    int3 p;
    if (!rt_screen_pixel(guide, at, p)) {
        return false;
    }
    o = sky.Load(p).rgb;
    return true;
}
// The same for a hit, matched by its thing and side as well (rt_screen_pixel with the slot): the form to call.
bool rt_screen_cache(Texture2D<float4> guide, Texture2D<float4> sky, RtHit h, out float3 o) {
    o = float3(1.0f, 1.0f, 1.0f);
    int3 p;
    if (!rt_screen_pixel(guide, h.at, h.slot, h.ns, p)) {
        return false;
    }
    o = sky.Load(p).rgb;
    return true;
}

// The local lights' share at a hit the screen shows: the walk before's D out there (`added`,
// RtImage::Added: every light, shadowed, through the lights' filters), unless the walk before's blended layer put its
// own light at that pixel (`blended`, its guide: a surface there; read only when `blended_bound`, the pass's bit that the
// walk before ran that layer and its guide is bound: without it Added holds the opaque layer's lights alone). False:
// the world light grid stands in.
bool rt_screen_lights(Texture2D<float4> guide, Texture2D<float4> added, Texture2D<float4> blended, bool blended_bound, float3 at,
                      out float3 d) {
    d = float3(0.0f, 0.0f, 0.0f);
    int3 p;
    if (!rt_screen_pixel(guide, at, p) || (blended_bound && blended.Load(p).a != 0.0f)) {
        return false;
    }
    d = added.Load(p).rgb;
    return true;
}
bool rt_screen_lights(Texture2D<float4> guide, Texture2D<float4> added, Texture2D<float4> blended, bool blended_bound, RtHit h,
                      out float3 d) {
    d = float3(0.0f, 0.0f, 0.0f);
    int3 p;
    if (!rt_screen_pixel(guide, h.at, h.slot, h.ns, p) || (blended_bound && blended.Load(p).a != 0.0f)) {
        return false;
    }
    d = added.Load(p).rgb;
    return true;
}

// What an area glow of a class other than emissive (item word 1: 32; never a light of the list) gives a ray that
// lights something, over its shaded body: its emission as it is, in the picture's units. 0 for any other hit
// and for a ray that only looks (a mirror's sees the body, as the screen shows it).
float3 rt_hit_area_glow(RtHit h, bool lighting) {
    return lighting && !h.glows && (h.item_flags & kRtItemArea) != 0u ? rt_shown(h.emission) * g_shade.gains.x
                                                                        : float3(0.0f, 0.0f, 0.0f);
}

// What a hit sends back toward the ray, in the picture's units (a share of white). `o`: the hit's own indirect
// ratio (kHitSky, or what the screen knows of it). An area glow's emission is not in it (rt_hit_area_glow).
float3 rt_hit_radiance_with(RtHit h, uint options, float3 o, bool d_known, float3 d_seen, inout uint seed);
float3 rt_hit_radiance(RtHit h, uint options, float3 o, inout uint seed) {
    return rt_hit_radiance_with(h, options, o, false, float3(0.0f, 0.0f, 0.0f), seed);
}
// The same with the hit's local lights known from the screen (`d_known`: `d_seen`, rt_screen_lights), else from the
// world light grid.
float3 rt_hit_radiance_with(RtHit h, uint options, float3 o, bool d_known, float3 d_seen, inout uint seed) {
    if ((h.item_flags & kItemSky) != 0u) {
        return rt_dome_light(h);
    }
    if (h.glows && !(h.listed && (options & kRtShadeLighting) != 0u)) {
        return rt_shown(h.emission) * g_shade.gains.x;
    }
    const bool lighting = (options & kRtShadeLighting) != 0u;  // what it returns at kBounceAlbedo
    if ((options & kRtShadeSkyOnly) != 0u) {
        const float t = rt_form();
        // What the sky gives it: the art's, a painted hit 1 (O = 1 of its own look), a lit one its share's
        // lin(D); the model's sky' by kForm, both alike.
        const float3 art = rt_return_albedo(h, lighting) * g_shade.look.y *
                           ((h.item_flags & kItemLit) != 0u ? rt_lin(saturate(g_shade.ambient.rgb + 0.5f * g_shade.fill.rgb))
                                                            : float3(1.0f, 1.0f, 1.0f));
        if (!(t > 0.0f)) {
            return art;
        }
        const float3 sky = rt_relit_share(rt_stage_light(), rt_open_o(), 0.0f, float3(0.0f, 0.0f, 0.0f), rt_tint_bound(h.item_flags));
        return lerp(art, rt_return_albedo(h, lighting) * g_shade.look.y * sky, t);
    }
    const bool lit = (h.item_flags & kItemLit) != 0u;
    const float nl = rt_hit_facing(h);
    float3 v = float3(1.0f, 1.0f, 1.0f);
    if (nl > 0.0f && (options & kRtShadeSunRay) != 0u) {
        v = rt_hit_sun(h, false, float2(0.0f, 0.0f));
    }
    // A glowing body seen takes no lamp light (program.hlsli traced_colour: its lamps are 0 for draw flag 16384, its own
    // light would flood it); below it is its shade plus its own colour's share.
    const bool body_seen = h.glow_body && !lighting;
    const float3 d = body_seen ? float3(0.0f, 0.0f, 0.0f)
                     : d_known ? d_seen
                               : rt_grid_light(h.at, h.ns, h.glow_body ? h.slot : 0xFFFFFFFFu, (options & kRtShadeLightRay) != 0u, seed);
    float3 share_ = rt_share(lit, nl, v, o, rt_tint_bound(h.item_flags));
    if (lit && !lighting) {
        share_ = rt_lit_seen_bound(nl, v, o, share_);  // a ray that looks sees a lit program as the screen does
    }
    float3 out_ = rt_return_albedo(h, lighting) * (share_ + d);
    // Mirror-like hits do not trace again: the stage's ambient stands for their reflection (the model:
    // the sky's light by F0). The exchange: glass's body gives up what its mirror takes,
    // (1 - F0) under the model (a metal's body is gone already: rt_hit_albedo).
    if (h.klass == kRtGlass) {
        const float r = (h.ior - 1.0f) / (h.ior + 1.0f);
        out_ *= 1.0f - rt_form() * r * r;
    }
    if (h.klass == kRtMetal || h.klass == kRtGlass) {
        out_ += rt_hit_mirror_stand_in(h);
    }
    // A glowing body seen (a mirror's ray, not one that lights: that light
    // comes through the list once) is shaded as any surface and adds a share of its own colour, a being 0.35 and an
    // object 0.8 of its lit value, at most 1.2 of it a channel: program.hlsli traced_colour's rule, on the same
    // linear value (traced_lin(base)). A hot body (item word 1 bit 31, the
    // raster's draw flag 8192: lava, a sun) at 2 to 2.5 of it, traced_colour's clamp.
    if (body_seen) {
        const float3 lit_value = rt_lin(h.base);
        out_ = (h.item_flags & kRtItemHot) != 0u ? clamp(out_, 2.0f * lit_value, 2.5f * lit_value)
                                                  : min(out_ + (h.being ? 0.35f : 0.8f) * lit_value, 1.2f * lit_value);
    }
    return out_;
}

// ---- What a mirrored ray sees: one function for every mirror the passes trace.
// Whether the sun's own disc lies along `dir`: within the bloom's reach, tan kSunSeenTan (three radii
// of the 0.8 degree disc it was set for), or the disc itself where a stage's is wider.
static const float kSunSeenTan = 0.042f;
bool rt_sees_sun(float3 dir) {
    return rt_sun_traced() && dot(dir, g_sun.xyz) > cos(atan(max(kSunSeenTan, g_sun.w)));
}
// The sun's glint, linear: the stage's key decoded, the model's sun by kForm, times kSunGlint (far over white).
float3 rt_sun_glint() {
    return lerp(rt_lin(g_shade.key.rgb), rt_stage_light().sun, rt_form()) * g_shade.look.w;
}
// The miss rule (rt_look's last leg): what a ray shows where it meets nothing within its reach is the sky's dome along
// its direction, read from the eye (rt_dome_ray) as the screen draws it and decoded once; where no dome stands on that
// line, the stage's ambient (g_sky). Never the dome as met from the ray's own start (it is drawn around the eye: a
// mirror far from the eye, or one past the dome's shell, met it nowhere and showed g_sky, black on a space stage).
// Along a mirrored ray from `from`: the solid thing met by the shared hit shading (`options`: kRtShade*), else what a
// miss shows (the miss rule); the sun's glint where the ray runs at it unhindered.
// What a ray that only looks meets, a clear thing seen THROUGH: a glass-class hit that does not
// glow passes the ray on (a shell is one face, a body two; at most kClearFaces), each face giving its stand-in mirror
// and taking the square root of its body's through-colour (rt_clear_through; half a body a face, as let_through) times
// 1 - F head on. `far_`: the way to the first thing met (`reach` where none). `glint`: the sun's own glint where the
// ray runs at it unhindered (a mirror's ray).
static const uint kClearFaces = 3u;
// The walk alone, its end unshaded, so that a pass shades the one hit a walk ends on by one call of the hit shading
// (each textual call of a function that traces is another copy of its ray queries in the compiled pass). One ray
// query a leg: the faces (`clear_faces` passed at most, the last met shaded whatever it is) and, after a miss, the dome
// along `dir` from the eye (the miss rule above, rt_dome_ray's ray). What it shows is added + tint x (hit ? the hit's light :
// shown). `sky` false: a miss ends it unmet (no dome; met false), a walk that only asks what solid thing stands within
// `reach` (clear_faces 0: the first thing met is the end, whatever its class).
struct RtLook {
    float3 added;  // the stand-in mirrors of the clear faces passed, each by the tint before it
    float3 tint;   // what the clear faces passed let through
    float3 shown;  // where it ended on the sky (a dome item met, the dome along it, or g_sky): that light and the glint
    bool hit;      // it ended on `h`, to be shaded
    bool met;      // the solid walk met something (a dome item, a face, the hit)
    uint faces;    // clear faces passed
    float far_;    // the way to the first thing met (`reach` where none)
    RtHit h;
};
// The sun's glint along a looking ray where it asks for one and runs at the sun (rt_sees_sun; 0 else).
float3 rt_look_glint(bool glint, float3 dir) {
    return glint && rt_sees_sun(dir) ? rt_sun_glint() : float3(0.0f, 0.0f, 0.0f);
}
RtLook rt_look(float3 from, float3 dir, float reach, float cone, bool glint, uint clear_faces, bool sky) {
    RtLook l;
    l.added = float3(0.0f, 0.0f, 0.0f);
    l.tint = float3(1.0f, 1.0f, 1.0f);
    l.shown = float3(0.0f, 0.0f, 0.0f);
    l.hit = false;
    l.met = false;
    l.faces = 0u;
    l.far_ = -1.0f;
    float3 at_ = from;
    float run = 0.0f;
    bool dome = false;
    [loop] for (uint leg = 0u; leg < clear_faces + 2u; ++leg) {
        const float3 origin = dome ? float3(0.0f, 0.0f, 0.0f) : at_;
        // The first leg's reach as given (a wall's distance may be under 1); a later face's what is left of it.
        const float leg_reach = dome ? kDomeReach : leg == 0u ? reach : max(reach - run, 1.0f);
        uint item, triangle_;
        float2 where_;
        float t;
        const bool met = rt_closest(origin, dir, leg_reach, dome ? kMaskSky : kMaskSolid | kMaskCutout, item, triangle_, where_, t);
        if (!met && !dome) {
            l.far_ = l.far_ < 0.0f ? reach : l.far_;
            if (!sky) {
                break;
            }
            dome = true;
            continue;
        }
        if (!met) {
            l.shown = rt_lin(g_sky.rgb) + rt_look_glint(glint, dir);  // no dome on that line
            break;
        }
        RtHit h;
        rt_hit_read(item, triangle_, where_, t, origin, dir, cone, h);
        if (dome) {
            l.shown = rt_dome_light(h) + rt_look_glint(glint, dir);
            break;
        }
        l.met = true;
        l.far_ = l.far_ < 0.0f ? run + t : l.far_;
        if ((h.item_flags & kItemSky) != 0u) {
            l.shown = rt_dome_light(h) + rt_look_glint(glint, dir);
            break;
        }
        if (h.klass != kRtGlass || h.glows || h.glow_body || leg == clear_faces) {
            l.hit = true;
            l.h = h;
            break;
        }
        l.added += l.tint * rt_hit_mirror_stand_in(h);
        l.tint *= rt_clear_face(g_at.x + item * kItemBytes);
        const float skin = max(0.003f * (run + t), 0.5f);
        at_ = h.at + dir * skin;
        run += t + skin;
        ++l.faces;
    }
    return l;
}
// What a looking ray shows at the end of its walk, `o` the hit's indirect ratio.
float3 rt_look_shaded(RtLook l, uint options, float3 o, inout uint seed) {
    float3 end = l.shown;
    if (l.hit) {
        end = rt_hit_radiance(l.h, options, o, seed);
    }
    return l.added + l.tint * end;
}
float3 rt_seen_past_clear(float3 from, float3 dir, float reach, float cone, uint options, bool glint, inout uint seed,
                          out float far_) {
    const RtLook l = rt_look(from, dir, reach, cone, glint, kClearFaces, true);
    far_ = l.far_;
    return rt_look_shaded(l, options, float3(g_shade.gains.y, g_shade.gains.y, g_shade.gains.y), seed);
}

float3 rt_mirror_seen(float3 from, float3 dir, float cone, uint options, inout uint seed) {
    float far_;
    return rt_seen_past_clear(from, dir, kRtFarAway, cone, options, true, seed, far_);
}

#endif
