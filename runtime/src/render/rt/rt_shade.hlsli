// SPDX-License-Identifier: GPL-3.0-only
// Hit shading: what every ray that meets a surface knows of it and what light it sends back (the master plan's
// sections 3 and 10). One model for the real-time passes (the sky and bounce light, reflections, the light volume)
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
    float4 ambient;   // rgb A: the stage's ambient colour times kAmbientGain; w free
    float4 key;       // rgb K: the stage key's colour; w kShadowTint
    float4 fill;      // rgb Fl: the stage's fill colour; w kWrap
    float4 sky;       // rgb what a ray that meets nothing at all shows; w kSkyGain
    float4 gains;     // x kAreaEmission, y kHitSky, z kBounce, w kBounceMax
    float4 axis[3];   // the world light grid: cell k along axis i = floor(dot(at, axis[i].xyz) + axis[i].w)
    float4 look;      // x kSkyMin, y kHitDark, z 1 / the dome's mean luminance (0: not known yet), w kSunGlint
    float4 authored;  // rgb the stage's ambient as authored (A without kAmbientGain): what the indirect ratio is a ratio to;
                      // w the gain of a surface met (1 / the running mean of such hits, at most 1: rt_count_hit; FB b46)
    uint4 grid;       // x the grid's cells' first byte in t4, y its entries', z 1 when it holds any, w the frame's number
    float4 dome;      // rgb 1 / the dome's mean colour as the bounce rays see it, w 1 when known (rt_sky_ratio; FB b48)
    float4 form;      // the painted composite's look (FC c36 rt_look_; DIRECTOR.md C3): x kForm, y the stage's m,
                      // z kDebake, w kAmbientLook (rt_share_terms; FB b53)
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
// Item word 1: an area light (FC's c07: a glowing mesh too big to be a point of the emitter list).
static const uint kRtItemArea = 32u;
static const float kRtPi = 3.14159265f;
static const float kRtFarAway = 200000.0f;

// How many frames a history holds, as a share of what it holds at 60 a second (FA's rt_history_scale, rt_frame.hlsli,
// 16.5: every history lasts the same time at any frame rate); 1 before that is in the frame's constants.
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

// A normal of the GGX distribution as the view sees it (Heitz 2018, "Sampling the GGX Distribution of Visible
// Normals"), about `n`, for the view `v` (toward the eye), alpha = roughness squared; the direction it reflects v to.
float3 rt_ggx_reflect(float3 n, float3 v, float alpha, float2 u) {
    float3 t, b;
    rt_basis(n, t, b);
    const float3 vl = float3(dot(v, t), dot(v, b), dot(v, n));
    const float3 vh = normalize(float3(alpha * vl.x, alpha * vl.y, max(vl.z, 1e-4f)));
    const float lensq = vh.x * vh.x + vh.y * vh.y;
    const float3 t1 = lensq > 0.0f ? float3(-vh.y, vh.x, 0.0f) * rsqrt(lensq) : float3(1.0f, 0.0f, 0.0f);
    const float3 t2 = cross(vh, t1);
    const float r = sqrt(u.x), phi = 2.0f * kRtPi * u.y;
    const float p1 = r * cos(phi);
    const float s = 0.5f * (1.0f + vh.z);
    const float p2 = (1.0f - s) * sqrt(max(1.0f - p1 * p1, 0.0f)) + s * r * sin(phi);
    const float3 nh = p1 * t1 + p2 * t2 + sqrt(max(1.0f - p1 * p1 - p2 * p2, 0.0f)) * vh;
    const float3 m = normalize(float3(alpha * nh.x, alpha * nh.y, max(nh.z, 1e-6f)));
    const float3 mw = t * m.x + b * m.y + n * m.z;
    return reflect(-v, mw);
}

// ---- Rays.
// The nearest thing of `mask` on the ray (cutouts where their maps say they are): its item, triangle, the weights of
// its second and third corners, its distance. False where nothing is.
bool rt_closest(float3 origin, float3 direction, float reach, uint mask, out uint item, out uint triangle_, out float2 where_,
                out float far_) {
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0f;
    ray.TMax = reach;
    RayQuery<RAY_FLAG_NONE> query;
    query.TraceRayInline(g_scene, (mask & kMaskCutout) != 0u ? RAY_FLAG_NONE : RAY_FLAG_FORCE_OPAQUE, mask, ray);
    while (query.Proceed()) {
        if (query.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
            cutout_there(query.CandidateInstanceID(), query.CandidatePrimitiveIndex(), query.CandidateTriangleBarycentrics(),
                         ray.Origin + ray.Direction * query.CandidateTriangleRayT())) {
            query.CommitNonOpaqueTriangleHit();
        }
    }
    item = 0u;
    triangle_ = 0u;
    where_ = float2(0.0f, 0.0f);
    far_ = reach;
    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT) {
        return false;
    }
    item = query.CommittedInstanceID();
    triangle_ = query.CommittedPrimitiveIndex();
    where_ = query.CommittedTriangleBarycentrics();
    far_ = query.CommittedRayT();
    return true;
}

// Whether the ray reaches `reach` with nothing solid (or a cutout where it is there) on it, the thing of slot `self`
// aside (a light's own body). 0 or 1 today; translucent tints (13.4) make it rgb.
float3 rt_through_of(float3 origin, float3 direction, float reach, uint self, uint mask) {
    uint item, triangle_;
    float2 where_;
    float far_;
    if (!rt_closest(origin, direction, reach, mask, item, triangle_, where_, far_)) {
        return float3(1.0f, 1.0f, 1.0f);
    }
    return g_items.Load(g_at.x + item * kItemBytes) == self && self != 0xFFFFFFFFu ? float3(1.0f, 1.0f, 1.0f) : float3(0.0f, 0.0f, 0.0f);
}

float3 rt_through(float3 origin, float3 direction, float reach, uint self) {
    return rt_through_of(origin, direction, reach, self, kMaskSolid | kMaskCutout);
}

// What stands in the sun's way of a surface (as the sun pass): everything solid for a lit one; for a painted one
// lit things only when the painted style asks (g_look.y), what the painting could not know.
uint rt_sun_mask(bool lit) {
    return (lit || g_look.y == 0.0f ? kMaskSolid | kMaskCutout : kMaskLit) | kMaskShadowOnly;
}

bool rt_sun_traced() {
    return (g_more.z & 1u) != 0u;
}

// A ray that gathers light (11.1, 11.2): the nearest solid or cut-out thing within kIndirectReach; past that the sky's
// dome (a ray of its own over the dome alone); 2 a thing, 1 the dome, 0 nothing at all (the stage's ambient itself).
// Blended things (smoke, glows, glass) do not stop it: what light passes them is section 13.4's.
static const float kIndirectReach = 20000.0f, kDomeReach = 4000000.0f;
uint rt_gather(float3 origin, float3 direction, out uint item, out uint triangle_, out float2 where_, out float far_) {
    if (rt_closest(origin, direction, kIndirectReach, kMaskSolid | kMaskCutout, item, triangle_, where_, far_)) {
        return 2u;
    }
    return rt_closest(origin, direction, kDomeReach, kMaskSky, item, triangle_, where_, far_) ? 1u : 0u;
}

// ---- What a hit knows (10.1).
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
    bool glows;            // it shows itself and takes no light: an emissive surface, an area light, a glowing mesh
    bool listed;           // and its light is one of the frame's emitter list: a ray that lights something shades it instead
};

float3 rt_bytes3(uint w) {
    return float3(float(w & 255u), float(w >> 8u & 255u), float(w >> 16u & 255u)) / 255.0f;
}

// Everything a hit knows, from its item's sixteen words and its triangle; the first map at a level of detail by the
// ray cone (Akenine-Moller et al. 2021, "Improved Shader and Texture Level of Detail Using Ray Cones"): `cone` is the
// spread of the ray (radians a unit of way), so its width at the hit is far * cone.
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
        uint colour[3];
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
                vc += float3(float(colour[k] & 255u), float(colour[k] >> 8u & 255u), float(colour[k] >> 16u & 255u)) / 255.0f *
                      (k == 0u ? u : k == 1u ? v : w);
            }
            // The cooker read the material with the vertex's colour mid grey (hit_record.rs): twice it is the colour.
            h.vertex = vc * 2.0f;
        }
        if (w2.x != 0xFFFFFFFFu && area > 1e-9f) {
            uint map_width, map_height;
            g_textures[NonUniformResourceIndex(w2.x)].GetDimensions(map_width, map_height);
            const float2 e1 = coordinate[1] - coordinate[0], e2 = coordinate[2] - coordinate[0];
            const float texels = abs(e1.x * e2.y - e1.y * e2.x) * float(map_width) * float(map_height);
            const float wide = max(far_ * cone, 1e-4f) / max(abs(dot(h.ng, direction)), 0.05f);
            const float level = max(0.5f * log2(max(texels / area, 1e-12f)) + log2(wide), 0.0f);
            const float4 m = g_textures[NonUniformResourceIndex(w2.x)].SampleLevel(g_wrap, h.uv, level);
            map = m.rgb;
            h.alpha = m.a;
        }
    }
    h.base = h.base * map * h.vertex;
    // One light, one way: a glowing mesh of the emitter list (word 14 bit 24) lights the others through the list alone;
    // an area light (word 1: 32) and an emissive surface that is no light of the list show their own emission to every
    // ray; a listed glow shows it to a ray that only looks at it (a mirror's).
    const bool area = (w0.y & kRtItemArea) != 0u;
    h.listed = (w3.z >> 24u & 1u) != 0u && !area;
    h.glows = h.klass == kRtEmissive || area || h.listed;
    if (h.glows) {
        // The record's emission was read with its maps mid grey (hit_record.rs): twice it under the map is what it shows.
        const float3 own = rt_bytes3(w3.z);
        h.emission = (max(own.r, max(own.g, own.b)) > 0.02f ? min(own * 2.0f, 1.0f) * map : h.base);
    }
}

// The sky's dome where a ray meets it: its first map, nothing else (10.2 item 1).
float3 rt_dome(RtHit h) {
    return h.base * g_shade.sky.w;
}

// What the dome shows along a direction, as a ratio to the stage's authored ambient (11.1: S).
// Against the dome's mean colour, a channel at a time, once it is known (FB b48): open sky averages 1, its colour
// included (the art's ambient already is the sky's colour); before that against the mean luminance as at first.
float3 rt_sky_ratio(float3 dome) {
    if (g_shade.dome.w > 0.0f) {
        return clamp(dome * g_shade.dome.rgb, 0.25f, 3.0f);
    }
    const float scale = g_shade.look.z > 0.0f ? g_shade.look.z : 1.0f / max(rt_lum(dome), 1e-3f);
    return clamp(dome * scale, 0.25f, 3.0f);
}

// The dome's luminance into the counters (fixed point, 256 a unit) for the host's running mean (11.1). The caller
// counts only some pixels so a frame's sum stays far inside 32 bits.
void rt_count_dome(float3 dome) {
    // Rounded: a dark dome's linear values are small (b65).
    InterlockedAdd(g_counters[44], uint(clamp(rt_lum(dome), 0.0f, 64.0f) * 256.0f + 0.5f));
    InterlockedAdd(g_counters[45], 1u);
    // Its colour, a channel at a time (rt_sky_ratio; FB b48).
    const uint3 c = uint3(clamp(dome, 0.0f, 64.0f) * 256.0f + 0.5f);
    InterlockedAdd(g_counters[48], c.r);
    InterlockedAdd(g_counters[49], c.g);
    InterlockedAdd(g_counters[50], c.b);
}

// The dome's mean as a property of the stage's sky (FB b51, C4): kDomeDirections fixed directions, cosine-weighted
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
    if (!rt_closest(float3(0.0f, 0.0f, 0.0f), dir, kDomeReach, kMaskSky, item, triangle_, where_, far_)) {
        return;
    }
    RtHit h;
    rt_hit_read(item, triangle_, where_, far_, float3(0.0f, 0.0f, 0.0f), dir, 0.05f, h);
    rt_count_dome(rt_lin(rt_dome(h)));  // the mean in linear light, as every ratio to it (b65)
}

// A surface met (not shining of itself) as a ratio before the gain, kBounce * L / A, into the counters 46 and 47 for the
// host's running mean of such hits (FB b46): a surface met counts against it, as the sky against the dome's mean. Held
// to 16 so a 4K frame's sum stays inside 32 bits with the reference counting too.
void rt_count_hit(float3 ratio) {
    InterlockedAdd(g_counters[46], uint(clamp(rt_lum(ratio), 0.0f, 16.0f) * 256.0f));
    InterlockedAdd(g_counters[47], 1u);
}

// ---- Light.
// Emitter `index` of the frame's list at a surface, unshadowed, as a share of the surface's own colour
// (rt_common.hlsli emitter_light: kWrap 0.25 its facing).
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

// ---- October's light model (DIRECTOR.md 12, M1..M3, M6; FB b59): linear light in the stage's own units.
static const float kSunStrength = 1.25f;  // the sun at a third of a stop over the stage's key (October, 25 October)
static const float kSkyChroma = 0.4f;     // the sky's colour in shade at this share of its chroma (M8; c39's sky_chroma)
// The relight's share of the model: 0 the art's share (Painted), 1 the model.
float rt_form() {
    return saturate(g_shade.form.x);
}
// What is drawn and seen as it is (the dome, the empty sky, a glow's colour) in the unit hits are in: linear (M1, b59b).
float3 rt_shown(float3 c) {
    return rt_lin(c);
}
// The stage's light in the picture's linear units (a white surface facing up under the full light shows 1):
// sky = lin(A) E / full, sun = (full - sky) kSunStrength E / full, E the exposure that pins that white surface to the
// game's own full light, the sun at no less than 30 degrees up (October's physical_exposure).
struct RtStageLight {
    float3 sky, sun, unit;
};
RtStageLight rt_stage_light() {
    const float3 a = g_shade.ambient.rgb, k = g_shade.key.rgb;
    const float3 sky = rt_lin(a);
    const float3 full = rt_lin(saturate(a + k));
    const float3 rest = max(full - sky, 0.0f);
    // up.s as the programs take it: view_look.y (the shading frame's form.y, the same rt_look_; FC c39/c40).
    const float up = g_shade.form.y;
    const float e = rt_lum(full) / max(rt_lum(sky) + rt_lum(rest) * up, 1e-4f);
    // The sky in shade by kAmbientLook, at kSkyChroma of its colour; a channel with no full light: share 1.
    const float3 sky_look = g_shade.form.w * lerp(rt_lum(sky).xxx, sky, kSkyChroma);
    const float3 ok = float3(full >= 1e-4f);
    RtStageLight s;
    s.sky = ok * (sky_look * e / max(full, 1e-4f)) + (1.0f - ok);
    s.sun = ok * (rest * kSunStrength * e / max(full, 1e-4f));
    // O's unit: the plain sky's light (no kAmbientLook, full chroma), so the dials move the shade and not O.
    s.unit = ok * max(sky * e / max(full, 1e-4f), 1e-3f) + (1.0f - ok);
    return s;
}
// O's unit (C4: the one divisor of every gather return): what O = 1 gives a white surface. The art's: the authored
// ambient; the model's: the sky's light.
float3 rt_o_unit() {
    const float t = rt_form();
    const float3 art = max(rt_lin(g_shade.authored.rgb), 0.002f);  // linear: O = 1 is "as authored" (M1; b59b)
    return t > 0.0f ? lerp(art, rt_stage_light().unit, t) : art;
}
// A hit's colour as it takes light: the art's, or (the model) its linear colour, a metal's diffuse gone (M6).
float3 rt_hit_albedo(RtHit h) {
    const float t = rt_form();
    const float3 base = rt_lin(h.base);  // linear always (M1, b59b)
    return t > 0.0f ? base * (1.0f - t * h.metal) : base;
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
    return lerp(art, rt_stage_light().unit * f0, t);  // what a mirror sees is the plain sky, no shade dial
}

// A surface's share of light as an affine function of what the sky and the bounce give it (O, rgb): share = s0 +
// Mo * O. Painted surfaces (3.4): P_c = (A_c O_c + K_c nl V_c) / (A_c + K_c nl), its luminance form likewise with
// the channel means, P = lerp(P_lum, P_c, kShadowTint). Lit surfaces (3.4, 10.2): A O + K nl V + Fl 0.5 O. `nl`
// from the smooth normal. Linear in O and V, so a mean over samples of the share is the share of the means.
void rt_share_terms_art(bool lit, float nl, float3 v, out float3 s0, out float3x3 mo) {
    const float3 a = g_shade.ambient.rgb, k = g_shade.key.rgb;
    if (lit) {
        s0 = k * nl * v;
        const float3 d = a + 0.5f * g_shade.fill.rgb;
        mo = float3x3(d.x, 0.0f, 0.0f, 0.0f, d.y, 0.0f, 0.0f, 0.0f, d.z);
        return;
    }
    // The painted composite's share as the programs give it (FC c39 traced_painted's Painted; FB b53, b67): the art is
    // taken to hold A + K f, f the sun's facing as the light pass gives a painted surface; share = lerp(mean, a channel
    // at a time, kShadowTint), a ratio of display values (rt_share takes it to linear). Linear in O: s0 + M O.
    const float t = g_shade.key.w;
    const float f = rt_sun_traced() ? saturate(nl / max(g_look.z, 1e-3f)) : 1.0f;
    const float m = f;
    const float ga = 1.0f;
    const float la = rt_lum(a), lk = rt_lum(k);
    const float lum_held = la + lk * m;
    const float3 held = a + k * m;
    const float3 ok = float3(held > 1e-4f);  // a channel the art holds nothing of: 1
    const float3 own_s0 = ok * (k * f * v / max(held, 1e-4f)) + (1.0f - ok);
    const float3 own = ok * (ga * a / max(held, 1e-4f));
    const bool whole = lum_held > 1e-4f;
    const float mean_s0 = whole ? lk * f * rt_lum(v) / lum_held : 1.0f;
    const float3 shared_ = whole ? ga * float3(0.2126f, 0.7152f, 0.0722f) * a / lum_held : float3(0.0f, 0.0f, 0.0f);
    s0 = (1.0f - t) * mean_s0 + t * own_s0;
    const float3 row = (1.0f - t) * shared_;  // every channel takes lum(A O) alike: the same row of columns
    mo = float3x3(row.x + t * own.x, row.y, row.z, row.x, row.y + t * own.y, row.z, row.x, row.y, row.z + t * own.z);
}

// The share in linear light, affine in O for the reference (ref_surface: c + M O): the art's display ratio x to the
// power 2.2 (c39's `painted`; a lit surface's sums likewise) by its tangent at O = 1 (exact there), then for a painted
// surface the model mixed in by kForm (FB b59, M3: s0 = sun max(n.s, 0) V, M = the sky's light). A lit surface keeps its
// own sums at every kForm, as the programs' lit share 1 (b67).
void rt_share_terms(bool lit, float nl, float3 v, out float3 s0, out float3x3 mo) {
    rt_share_terms_art(lit, nl, v, s0, mo);
    const float3 x1 = max(s0 + mul(mo, float3(1.0f, 1.0f, 1.0f)), 0.0f);
    const float3 slope = 2.2f * pow(x1, 1.2f);
    mo = float3x3(mo[0] * slope.x, mo[1] * slope.y, mo[2] * slope.z);
    s0 = pow(x1, 2.2f) - mul(mo, float3(1.0f, 1.0f, 1.0f));
    const float t = rt_form();
    if (!lit && t > 0.0f) {
        const RtStageLight st = rt_stage_light();
        s0 = lerp(s0, st.sun * max(nl, 0.0f) * v, t);
        mo = mo * (1.0f - t) + float3x3(st.sky.x, 0.0f, 0.0f, 0.0f, st.sky.y, 0.0f, 0.0f, 0.0f, st.sky.z) * t;
    }
}

// The air's share of the stage's light (FB b64): the sun's part a unit of V gives (rt_air_sun) and the whole with the
// sky's part by s (rt_air_share). The art's a channel at a time in linear light (M1), the model's under the relight:
// the sun as the open ground facing up takes it (up.s = form.y).
float3 rt_air_sun() {
    const float3 sky = rt_lin(g_shade.authored.rgb);
    const float3 full = rt_lin(saturate(g_shade.authored.rgb + g_shade.key.rgb));
    const float3 ok = float3(full >= 1e-4f);
    const float3 art = ok * max(full - sky, 0.0f) / max(full, 1e-4f);
    const float t = rt_form();
    return t > 0.0f ? lerp(art, rt_stage_light().sun * g_shade.form.y, t) : art;
}
float3 rt_air_share(float s, float3 v) {
    const float3 sky = rt_lin(g_shade.authored.rgb);
    const float3 full = rt_lin(saturate(g_shade.authored.rgb + g_shade.key.rgb));
    const float3 ok = float3(full >= 1e-4f);
    float3 sky_part = ok * sky / max(full, 1e-4f);
    const float t = rt_form();
    if (t > 0.0f) {
        sky_part = lerp(sky_part, rt_stage_light().sky, t);
    }
    return sky_part * s + rt_air_sun() * v;
}

// The share a hit takes, exactly (b67): the art's display ratio to the power 2.2, a painted surface's lerped to the model by
// kForm.
float3 rt_share(bool lit, float nl, float3 v, float3 o) {
    float3 s0;
    float3x3 mo;
    rt_share_terms_art(lit, nl, v, s0, mo);
    const float3 art = pow(max(s0 + mul(mo, o), 0.0f), 2.2f);
    const float t = rt_form();
    if (lit || !(t > 0.0f)) {
        return art;
    }
    const RtStageLight st = rt_stage_light();
    return lerp(art, st.sky * o + st.sun * max(nl, 0.0f) * v, t);
}

// From just off a surface: along its geometric normal, more the farther from the eye (depth's steps grow).
float3 rt_off(float3 at, float3 ng) {
    return at + ng * max(0.003f * length(at), 0.5f);
}

// One sun shadow ray from a hit (10.2 item 4): toward the sun itself, or a point of its disc (`u`, the reference).
float3 rt_hit_sun(RtHit h, bool disc, float2 u) {
    if (!rt_sun_traced()) {
        return float3(1.0f, 1.0f, 1.0f);  // no sun traced: the key is the authored light
    }
    const float3 toward = disc ? rt_within(g_sun.xyz, g_sun.w, u) : g_sun.xyz;
    const bool thin = (h.item_flags & kItemThin) != 0u;
    const float3 n = thin && dot(h.ng, g_sun.xyz) < 0.0f ? -h.ng : h.ng;
    return rt_through_of(rt_off(h.at, n), toward, kRtFarAway, 0xFFFFFFFFu, rt_sun_mask((h.item_flags & kItemLit) != 0u));
}

float rt_hit_facing(RtHit h) {
    const float turned = dot(h.ns, g_sun.xyz);
    return max((h.item_flags & kItemThin) != 0u ? abs(turned) : turned, 0.0f);
}

// ---- The world light grid (10.3): for hits off the screen, the lights whose reach touches the hit's cell.
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
            estimate *= rt_through(from, toward, reach, kept_slot);
        }
    }
    return estimate;
}

// ---- What a hit shows (10.2), for the real-time passes. Options:
static const uint kRtShadeSunRay = 1u;      // trace the sun from the hit (Medium, High); else Vh = 1 where it faces it
static const uint kRtShadeLightRay = 2u;    // the grid's light shadowed (High)
static const uint kRtShadeSkyOnly = 8u;     // Low: a hit is kHitDark, nothing read
static const uint kRtShadeLighting = 16u;   // a ray that lights something: a listed glow lights through the list, not by its hit

// Where the hit is on the screen and the screen shows that very surface (10.2 item 5): its place projects inside the
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

// The local lights' share at a hit the screen shows (10.2 item 5, FB b49): the walk before's D out there (`added`,
// RtImage::Added: every light, shadowed, through the lights' filters), unless the walk before's blended layer put its
// own light at that pixel (`blended`, its guide: a surface there). False: the world light grid stands in.
bool rt_screen_lights(Texture2D<float4> guide, Texture2D<float4> added, Texture2D<float4> blended, float3 at, out float3 d) {
    d = float3(0.0f, 0.0f, 0.0f);
    int3 p;
    if (!rt_screen_pixel(guide, at, p) || blended.Load(p).a != 0.0f) {
        return false;
    }
    d = added.Load(p).rgb;
    return true;
}

// What a hit sends back toward the ray (10.2), in the picture's units (a share of white). `o`: the hit's own indirect
// ratio (kHitSky, or what the screen knows of it).
float3 rt_hit_radiance_with(RtHit h, uint options, float3 o, bool d_known, float3 d_seen, inout uint seed);
float3 rt_hit_radiance(RtHit h, uint options, float3 o, inout uint seed) {
    return rt_hit_radiance_with(h, options, o, false, float3(0.0f, 0.0f, 0.0f), seed);
}
// The same with the hit's local lights known from the screen (`d_known`: `d_seen`, rt_screen_lights), else from the
// world light grid (10.3).
float3 rt_hit_radiance_with(RtHit h, uint options, float3 o, bool d_known, float3 d_seen, inout uint seed) {
    if ((h.item_flags & kItemSky) != 0u) {
        return rt_shown(rt_dome(h));
    }
    if (h.glows && !(h.listed && (options & kRtShadeLighting) != 0u)) {
        return rt_shown(h.emission) * g_shade.gains.x;
    }
    if ((options & kRtShadeSkyOnly) != 0u) {
        const float t = rt_form();
        const float3 art = rt_hit_albedo(h) * g_shade.look.y * rt_lin(g_shade.ambient.rgb);  // linear (M1; b59b)
        return t > 0.0f ? lerp(art, rt_hit_albedo(h) * g_shade.look.y * rt_stage_light().sky, t) : art;
    }
    const bool lit = (h.item_flags & kItemLit) != 0u;
    const float nl = rt_hit_facing(h);
    float3 v = float3(1.0f, 1.0f, 1.0f);
    if (nl > 0.0f && (options & kRtShadeSunRay) != 0u) {
        v = rt_hit_sun(h, false, float2(0.0f, 0.0f));
    }
    const float3 d = d_known ? d_seen : rt_grid_light(h.at, h.ns, h.slot, (options & kRtShadeLightRay) != 0u, seed);
    float3 out_ = rt_hit_albedo(h) * (rt_share(lit, nl, v, o) + d);
    // Mirror-like hits do not trace again: the stage's ambient stands for their reflection (10.2 item 6; the model:
    // the sky's light by F0, b59).
    if (h.klass == kRtMetal || h.klass == kRtGlass) {
        out_ += rt_hit_mirror_stand_in(h);
    }
    return out_;
}

// ---- A pixel's surface as the surface pass left it (rt_common.hlsli's images), with what hit shading wants of it.
struct RtPixel {
    RtSurface s;
    uint klass, flags;  // its class; kSurface* (lit, emissive, environment, two-sided)
    float roughness, metal, share;
    float3 base;
};

bool rt_pixel_read(int2 pixel, float4 guide, float4 normals, float4 motion, float4 material, float4 base, out RtPixel p) {
    p.s = rt_surface(pixel, guide, normals, motion.w);
    const uint bits = uint(material.b * 255.0f + 0.5f);
    p.klass = bits & 15u;
    p.flags = bits & 240u;
    p.roughness = material.r;
    p.metal = material.g;
    p.share = material.a;
    p.base = base.rgb;
    return guide.a != 0.0f;
}

#endif
