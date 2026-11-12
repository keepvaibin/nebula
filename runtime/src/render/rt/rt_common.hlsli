// SPDX-License-Identifier: GPL-3.0-only
// What the passes of a frame's traced light share (rt_surface, rt_sun, rt_lights, rt_light, and the passes of the
// sky, reflections, the light volume and the reference): the frame's bindings by register, the mesh memory and how a
// pixel or a ray finds its triangle there, rays, and the surface images' encodings. A pass declares its own images
// (t9-t20 read, u0-u7 written); what it does not use the compiler leaves out of it.
#ifndef NEBULA_RT_COMMON_HLSLI
#define NEBULA_RT_COMMON_HLSLI

#include "rt_bindings.hlsli"
#include "rt_frame.hlsli"
#include "rt_sample.hlsli"

RaytracingAccelerationStructure g_scene : register(t0);
// Sixteen uints an item (rt_walk.cpp rt_tables): its slot; its flags and light set; its mesh for a pixel that wants its
// triangle: the arena bound (8 up: none), its attributes' and indices' first bytes there, an index's bytes, its
// positions' first byte, its matrix's first row; for an alpha-tested material its first map among the textures (all
// ones: none) and the alpha from which it is there; its matrix stream's first byte from 1 (0: the one row places every
// vertex; else a uint a vertex names the matrix, six rows each from the item's first row); what a ray that meets it
// shows: its material's class, flags, roughness and metal, a byte each; its colour under full light, a byte a channel,
// and the share of its look a painted reflection carries; the first row its matrix had the frame before; two spare.
ByteAddressBuffer g_items : register(t1);
ByteAddressBuffer g_sets : register(t2);   // 16 bytes a set: its lamp's place and radius (0: none)
ByteAddressBuffer g_drops : register(t3);  // the emitters (48 bytes each, from g_glow.x) and the reservoirs' remap (g_kept.x)
ByteAddressBuffer g_tiles : register(t4);  // the emitters' tiles (two uints a tile: first entry, how many) and entries
Texture2D<float> g_depth_map : register(t5);
Texture2D<uint2> g_item_map : register(t6);  // the pixel's item, from 1, and its triangle
// The mesh memory: the arenas the frame's meshes lie in, and the frame's rows (six a matrix: three of its place, three
// of its normal).
ByteAddressBuffer g_arena0 : register(t21);
ByteAddressBuffer g_arena1 : register(t22);
ByteAddressBuffer g_arena2 : register(t23);
ByteAddressBuffer g_arena3 : register(t24);
ByteAddressBuffer g_arena4 : register(t25);
ByteAddressBuffer g_arena5 : register(t26);
ByteAddressBuffer g_arena6 : register(t27);
ByteAddressBuffer g_arena7 : register(t28);
ByteAddressBuffer g_rows : register(t29);
// Every texture of the renderer's, by the numbers the draws' programs use, and one sampler (linear, repeating).
Texture2D<float4> g_textures[] : register(t0, space1);
SamplerState g_wrap : register(s0);
RWStructuredBuffer<uint> g_counters : register(u8);

static const float3 kLuminance = float3(0.2126f, 0.7152f, 0.0722f);
// sRGB's decode (M1: the pictures the programs read are linear; FB b59b). Unclamped above 1, as c39's traced_lin.
float3 rt_lin(float3 c) {
    c = max(c, 0.0f);
    return lerp(pow((c + 0.055f) / 1.055f, 2.4f), c / 12.92f, step(c, 0.04045f));
}

float rt_unit(uint seed) {
    return float(seed & 0xFFFFFFu) / 16777216.0f;
}

// The pixel lies in the scene's viewport.
bool in_viewport(float2 centre) {
    return centre.x >= g_viewport.x && centre.y >= g_viewport.y && centre.x < g_viewport.x + g_viewport.z &&
           centre.y < g_viewport.y + g_viewport.w;
}

// The eye-space place of a pixel of the depth map, or z = 0 where there is none.
float3 place_at(int2 pixel) {
    const float depth = g_depth_map.Load(int3(pixel, 0));
    if (rt_far(g_depth, depth)) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    return rt_eye_ray(g_ray, float2(pixel) + 0.5f) * -rt_eye_z(g_depth, depth);
}

uint3 arena_words(uint arena, uint at) {
    switch (arena) {
    case 0u: return g_arena0.Load3(at);
    case 1u: return g_arena1.Load3(at);
    case 2u: return g_arena2.Load3(at);
    case 3u: return g_arena3.Load3(at);
    case 4u: return g_arena4.Load3(at);
    case 5u: return g_arena5.Load3(at);
    case 6u: return g_arena6.Load3(at);
    default: return g_arena7.Load3(at);
    }
}

uint arena_word(uint arena, uint at) {
    switch (arena) {
    case 0u: return g_arena0.Load(at);
    case 1u: return g_arena1.Load(at);
    case 2u: return g_arena2.Load(at);
    case 3u: return g_arena3.Load(at);
    case 4u: return g_arena4.Load(at);
    case 5u: return g_arena5.Load(at);
    case 6u: return g_arena6.Load(at);
    default: return g_arena7.Load(at);
    }
}

uint mesh_at(uint n) {
    return n < 4u ? g_mesh_at[0][n] : n < 8u ? g_mesh_at[1][n - 4u] : g_mesh_at[2][n - 8u];
}

// The first byte of the six rows that place vertex `vertex` of a mesh: the item's row, or the matrix the vertex names
// in the mesh's matrix stream (`stream`: its first byte from 1, 0 when one row places the mesh).
uint vertex_rows(uint arena, uint base, uint first_row, uint stream, uint vertex) {
    const uint row = mesh_at(8u) + first_row * 16u;
    return stream == 0u ? row : row + arena_word(arena, base + (stream - 1u) + vertex * 4u) * 96u;
}

// Corner `n`'s vertex of a triangle list with 16-bit indices starting at byte `at` (a load starts on a 4-byte word).
uint index16(uint arena, uint at, uint n) {
    const uint byte_ = at + n * 2u;
    const uint word = arena_words(arena, byte_ & ~3u).x;
    return (byte_ & 2u) != 0u ? word >> 16u : word & 0xFFFFu;
}

// The three vertices of triangle `triangle_` of the item whose words `first`, `second` are.
uint3 triangle_corners(uint4 first, uint4 second, uint triangle_) {
    const uint arena = first.z;
    const uint indices = mesh_at(arena) + second.x;
    if (second.y == 2u) {
        return uint3(index16(arena, indices, triangle_ * 3u), index16(arena, indices, triangle_ * 3u + 1u), index16(arena, indices, triangle_ * 3u + 2u));
    }
    return arena_words(arena, indices + triangle_ * 12u);
}

// A direction within `spread` (a tangent) of `toward`, by two numbers in 0-1.
float3 within(float3 toward, float spread, float2 noise) {
    const float turn = 6.2831853f * noise.x;
    const float out_ = sqrt(noise.y) * spread;
    const float3 side = normalize(abs(toward.y) < 0.9f ? cross(toward, float3(0.0f, 1.0f, 0.0f)) : cross(toward, float3(1.0f, 0.0f, 0.0f)));
    const float3 up = cross(side, toward);
    return normalize(toward + (side * cos(turn) + up * sin(turn)) * out_);
}

// The alpha of item `item`'s first map where a ray meets its triangle `triangle_` at `where_` (1: no map to read).
float alpha_there(uint item, uint triangle_, float2 where_) {
    const uint at = g_at.x + item * kItemBytes;
    const uint4 first = g_items.Load4(at), second = g_items.Load4(at + 16u);
    const uint map = g_items.Load(at + 32u);
    if (first.z >= 8u || map == 0xFFFFFFFFu) {
        return 1.0f;
    }
    const uint base = mesh_at(first.z);
    const uint3 corner = triangle_corners(first, second, triangle_);
    const float2 c0 = asfloat(arena_words(first.z, base + first.w + corner.x * 24u + 12u).yz);
    const float2 c1 = asfloat(arena_words(first.z, base + first.w + corner.y * 24u + 12u).yz);
    const float2 c2 = asfloat(arena_words(first.z, base + first.w + corner.z * 24u + 12u).yz);
    const float2 coordinate = c0 * (1.0f - where_.x - where_.y) + c1 * where_.x + c2 * where_.y;
    return g_textures[NonUniformResourceIndex(map)].SampleLevel(g_wrap, coordinate, 0.0f).a;
}

// Whether the clip areas show a clipped map part's point `at` (the eye's space; plan 26 M10): the game draws the
// part whole and paints it over where its volumes say (FallOutFieldDraw), so what shows is its points inside some
// volume (g_clip.z 1) or outside every one (2). The volumes' unit shapes: nebula_frame_list.h NEBULA_CLIP_*.
bool clip_shows(float3 at) {
    const float4 p = float4(at, 1.0f);
    bool inside = false;
    for (uint v = 0u; v < g_clip.y && !inside; ++v) {
        const uint base = g_at.x + g_clip.x + v * 64u;
        const float3 u = float3(dot(asfloat(g_items.Load4(base)), p), dot(asfloat(g_items.Load4(base + 16u)), p),
                                dot(asfloat(g_items.Load4(base + 32u)), p));
        const uint shape = g_items.Load(base + 48u);
        if (shape == 0u) {
            inside = dot(u, u) < 1.0f;
        } else if (shape == 1u) {
            inside = all(abs(u) <= 1.0f);
        } else if (shape == 2u) {
            inside = abs(u.x) <= 1.0f && abs(u.z) <= 1.0f && u.y >= 0.0f && u.y <= 2.0f;
        } else {
            const float rate = shape == 3u ? u.y : 1.0f - u.y;
            inside = u.y >= 0.0f && u.y <= 1.0f && u.x * u.x + u.z * u.z < rate * rate;
        }
    }
    return g_clip.z == 2u ? !inside : inside;
}

// Whether an alpha-tested surface is there where a ray meets it: item `item`'s triangle `triangle_`, at `where_`
// among its corners (the second and third's weights), `at_eye` the place met. Its first map's alpha at the vertices'
// first coordinates, against the material's threshold: what the cutout of a leaf, a fence or a flower is drawn by.
// Where the item gives no mesh or no map (a skinned or frame-made mesh), it is not there: a sheet whose holes the
// pass cannot read casts no shadow rather than a solid one (counted). A clipped map part (kItemClipped) is there
// where its clip areas show it (and its map, if it has one and its program alpha-tests, says so).
bool cutout_there(uint item, uint triangle_, float2 where_, float3 at_eye) {
    const uint at = g_at.x + item * kItemBytes;
    const uint4 first = g_items.Load4(at), second = g_items.Load4(at + 16u);
    const uint2 map = g_items.Load2(at + 32u);
    const uint arena = first.z;
    const bool clipped = (first.y & kItemClipped) != 0u;
    if (clipped && !clip_shows(at_eye)) {
        return false;
    }
    if (clipped && (first.y & kItemAlphaTested) == 0u) {
        return true;  // its program discards nothing: its map's alpha is no test of it
    }
    if (arena >= 8u || map.x == 0xFFFFFFFFu) {
        if (clipped) {
            return true;
        }
        InterlockedAdd(g_counters[22], 1u);
        return false;
    }
    const uint base = mesh_at(arena);
    const uint3 corner = triangle_corners(first, second, triangle_);
    // A vertex's colour word and first coordinate: the 12 bytes from its 12th.
    const float2 c0 = asfloat(arena_words(arena, base + first.w + corner.x * 24u + 12u).yz);
    const float2 c1 = asfloat(arena_words(arena, base + first.w + corner.y * 24u + 12u).yz);
    const float2 c2 = asfloat(arena_words(arena, base + first.w + corner.z * 24u + 12u).yz);
    const float2 coordinate = c0 * (1.0f - where_.x - where_.y) + c1 * where_.x + c2 * where_.y;
    InterlockedAdd(g_counters[21], 1u);
    return g_textures[NonUniformResourceIndex(map.x)].SampleLevel(g_wrap, coordinate, 0.0f).a >= asfloat(map.y);
}

// How far along the ray something of `mask` stands, or -1 where nothing does; `met`: the item of what does (the
// nearest, unless `any` takes the first the walk meets). With kMaskCutout in the mask, alpha-tested surfaces stand
// there where their map says they are.
// `own`: the item whose sun card (kKindCard, l83) the ray passes over, the billboard it starts from (0xFFFFFFFF none);
// with kOwnWhole (c90) every triangle of that item, opaque ones too (a clump of leaves, a crown, a cloud).
float occluder_at_but(float3 origin, float3 direction, float reach, uint mask, bool any, uint own, out uint met) {
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0f;
    ray.TMax = reach;
    const bool own_whole = own != 0xFFFFFFFFu && (own & kOwnWhole) != 0u;
    const uint own_id = own & ~kOwnWhole;
    const uint flags = (own_whole ? RAY_FLAG_FORCE_NON_OPAQUE
                        : (mask & kMaskCutout) != 0u || own != 0xFFFFFFFFu ? RAY_FLAG_NONE : RAY_FLAG_FORCE_OPAQUE) |
                       (any ? RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH : RAY_FLAG_NONE);
    RayQuery<RAY_FLAG_NONE> query;
    query.TraceRayInline(g_scene, flags, mask, ray);
    while (query.Proceed()) {
        if (query.CandidateType() != CANDIDATE_NON_OPAQUE_TRIANGLE) {
            continue;
        }
        const uint kind = query.CandidateInstanceContributionToHitGroupIndex();
        if (own != 0xFFFFFFFFu && query.CandidateInstanceID() == own_id && (own_whole || (kind & kKindCard) != 0u)) {
            continue;  // the billboard's own sun card; c90: its own clump
        }
        if ((kind & kKindCutout) == 0u ||
            cutout_there(query.CandidateInstanceID(), query.CandidatePrimitiveIndex(), query.CandidateTriangleBarycentrics(),
                         ray.Origin + ray.Direction * query.CandidateTriangleRayT())) {
            query.CommitNonOpaqueTriangleHit();
        }
    }
    InterlockedAdd(g_counters[17], 1u);
    met = 0u;
    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT) {
        return -1.0f;
    }
    met = query.CommittedInstanceID();
    return query.CommittedRayT();
}

float occluder_at(float3 origin, float3 direction, float reach, uint mask, bool any, out uint met) {
    return occluder_at_but(origin, direction, reach, mask, any, 0xFFFFFFFFu, met);
}

// The light a water surface gathers under it (plan 13.3 High: caustics): at the place item `item`'s triangle
// `triangle_` meets the sun's ray, its first map's brightness, two samples drifting apart over time (0.03 of the map
// a second), their product over the square of the map's mean (its smallest level): 1 on the mean, more on the bright
// lines the map's waves make, less between. 1 where the item has no mesh or map.
static const float kCaustic = 0.6f;
float caustic_there(uint item, uint triangle_, float2 where_) {
    const uint at = g_at.x + item * kItemBytes;
    const uint4 first = g_items.Load4(at), second = g_items.Load4(at + 16u);
    const uint map = g_items.Load(at + 32u);
    if (first.z >= 8u || map == 0xFFFFFFFFu) {
        return 1.0f;
    }
    const uint base = mesh_at(first.z);
    const uint3 corner = triangle_corners(first, second, triangle_);
    const float2 c0 = asfloat(arena_words(first.z, base + first.w + corner.x * 24u + 12u).yz);
    const float2 c1 = asfloat(arena_words(first.z, base + first.w + corner.y * 24u + 12u).yz);
    const float2 c2 = asfloat(arena_words(first.z, base + first.w + corner.z * 24u + 12u).yz);
    const float2 coordinate = c0 * (1.0f - where_.x - where_.y) + c1 * where_.x + c2 * where_.y;
    const float seconds = g_stage.z;  // the game's time (l58), not the frames shown
    const float3 luma = float3(0.2126f, 0.7152f, 0.0722f);
    const float a = dot(g_textures[NonUniformResourceIndex(map)].SampleLevel(g_wrap, coordinate + seconds * float2(0.03f, 0.017f), 0.0f).rgb, luma);
    const float b = dot(g_textures[NonUniformResourceIndex(map)].SampleLevel(g_wrap, coordinate * 1.37f - seconds * float2(0.019f, 0.03f), 0.0f).rgb, luma);
    const float mean = max(dot(g_textures[NonUniformResourceIndex(map)].SampleLevel(g_wrap, coordinate, 16.0f).rgb, luma), 0.05f);
    return clamp(1.0f + kCaustic * (a * b / (mean * mean) - 1.0f), 0.5f, 1.8f);
}

// Water's clear way, one for every stage (the owner's D3; FB b84): green's 1/e way in units, so that red loses 10 % in
// 600 units (red's absorption kWaterAbsorb.r / 34270 = 1.756e-4 a unit): this art's lakes are swimming pools, clear
// and colourless; the stage's own (g_look.w, b58) is not read.
static const float kWaterClear = 34270.0f;
float rt_water_clear() {
    return kWaterClear;
}
static const float kWaterLayer = 2.0f;  // water hits this near one another are one sheet of it drawn twice (b58b)
// Pure water's absorption against green's (Pope and Fry 1997: 0.340, 0.0565, 0.0092 per metre at 650, 550, 450 nm;
// LIGHT-MODEL.md 7; FB b58c): the way is in units of the stage's water clear, green's 1/e way.
static const float3 kWaterAbsorb = float3(0.340f, 0.0565f, 0.0092f) / 0.0565f;

// What a clear thing lets through at its own thickness (the owner's "everything clear", the director's table G; FB
// b86), linear: lerp(white, its art's colour, k), k by its class as its own numbers say (item word 15's index, word
// 11's roughness): crystal (index 1.7 and over) 0.25; ice (1.30 to 1.32) 0.35, an ice being (rougher, 0.07 and over)
// 0.7; other glass 0.2 and never under 0.75 luminance. `at`: the item's first byte in g_items.
float3 rt_clear_through(uint at) {
    const uint colour = g_items.Load(at + 48u);
    const float3 art = max(rt_lin(float3(float(colour & 255u), float(colour >> 8u & 255u), float(colour >> 16u & 255u)) / 255.0f),
                           0.002f);
    const uint ior_bits = g_items.Load(at + 60u) >> 12u & 255u;
    const float ior = ior_bits != 0u ? 1.0f + float(ior_bits) / 100.0f : 1.5f;
    const float roughness = float(g_items.Load(at + 44u) >> 16u & 255u) / 255.0f;
    const bool ice = ior >= 1.295f && ior <= 1.325f;
    const float k = ior >= 1.7f ? 0.25f : ice ? (roughness >= 0.07f ? 0.7f : 0.35f) : 0.2f;
    float3 through = lerp(float3(1.0f, 1.0f, 1.0f), art, k);
    if (!ice && ior < 1.7f) {
        // Clear glass keeps at least 0.75 of white's luminance: what it takes, scaled down to that.
        const float lum = dot(through, float3(0.2126f, 0.7152f, 0.0722f));
        through = lum < 0.75f ? 1.0f - (1.0f - through) * (0.25f / max(1.0f - lum, 1e-4f)) : through;
    }
    return through;
}

// What the blended things on a ray let through (plan 13.4), rgb; `first`: how far along the first of them stands (-1:
// none). Every candidate is weighed and none committed (the product does not care for order), at most four: glass and
// crystal their colour's square root (the absorption exp(-sigma t) with kGlassTint 0.5: the thickness cancels) by
// 1 - F at normal incidence (index 1.5); water 0.9 once a ray (a body of water is drawn as several sheets at one
// place: the surface, its back face, a cup; the others weigh nothing and do not count to the four); a translucent
// thing (a cloud, a ghost, jelly, steam, a fall) none under 0.4 alpha at the hit and half from 0.6 (FB b102: the
// plan's "no shadow under 0.5 alpha, half above"); what glows, what is painted and the rest nothing.
float3 let_through(float3 origin, float3 direction, float reach, out float first) {
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0f;
    ray.TMax = reach;
    RayQuery<RAY_FLAG_FORCE_NON_OPAQUE> query;
    query.TraceRayInline(g_scene, RAY_FLAG_FORCE_NON_OPAQUE, kMaskBlended, ray);
    float3 tint = float3(1.0f, 1.0f, 1.0f);
    uint weighed = 0u;
    bool watered = false;
    first = -1.0f;
    // The way the ray runs inside water (R4; FB b58): its water layers' count (hits within kWaterLayer of a layer met
    // are that layer: b58b) and the nearest and farthest.
    uint waters = 0u;
    float water_near = 3.0e38f, water_far = 0.0f;
    while (query.Proceed()) {
        const uint item = query.CandidateInstanceID();
        const uint at = g_at.x + item * kItemBytes;
        const uint class_ = g_items.Load(at + 44u) & 255u;
        float3 passes = float3(1.0f, 1.0f, 1.0f);
        bool counts = true;
        if (class_ == kClassGlass) {
            // The body's transmittance (its class's through-colour, FB b86), half a body a face, and 1 - F a face
            // (A4 K6, A5-B1; FB b75).
            passes = sqrt(rt_clear_through(at)) * 0.96f;
        } else if (class_ == kClassWater) {
            const float t_water = query.CandidateTriangleRayT();
            if (waters == 0u || (abs(t_water - water_near) > kWaterLayer && abs(t_water - water_far) > kWaterLayer)) {
                ++waters;
                water_near = min(water_near, t_water);
                water_far = max(water_far, t_water);
            }
            counts = !watered;
            passes = !counts ? float3(1.0f, 1.0f, 1.0f)
                             : 0.98f * (rt_part(kPartTransmit) >= 3u  // 1 - F head on (A4 K6; FB b75)
                                           ? caustic_there(item, query.CandidatePrimitiveIndex(), query.CandidateTriangleBarycentrics())
                                           : 1.0f).xxx;
            watered = true;
        } else if (class_ == kClassTranslucent) {
            passes = (1.0f - 0.5f * smoothstep(0.4f, 0.6f, alpha_there(item, query.CandidatePrimitiveIndex(),
                                                                       query.CandidateTriangleBarycentrics()))).xxx;
        } else {
            continue;
        }
        tint *= passes;
        const float t = query.CandidateTriangleRayT();
        first = first < 0.0f ? t : min(first, t);
        if (counts && ++weighed == 4u) {
            query.Abort();
        }
    }
    // One layer: the point is under that water, the way is to it; two or more: the ray crosses a body of water.
    if (waters != 0u) {
        const float way = waters == 1u ? water_near : water_far - water_near;
        tint *= exp(-way / rt_water_clear() * kWaterAbsorb);
    }
    return tint;
}

// Whether anything of `mask` stands on the ray (occluder_at).
bool blocked(float3 origin, float3 direction, float reach, uint mask, bool any, out uint met) {
    return occluder_at(origin, direction, reach, mask, any, met) >= 0.0f;
}

// Whether anything of `mask` but the items of slot `own` (a light's own body) stands on the ray before `reach`. The
// walk ends at the first such hit (A2-14): every candidate is offered (forced non-opaque), the own slot's are passed
// over.
bool blocked_but(float3 origin, float3 direction, float reach, uint mask, uint own) {
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0f;
    ray.TMax = reach;
    RayQuery<RAY_FLAG_NONE> query;
    query.TraceRayInline(g_scene, RAY_FLAG_FORCE_NON_OPAQUE, mask, ray);
    while (query.Proceed()) {
        if (query.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
            g_items.Load(g_at.x + query.CandidateInstanceID() * kItemBytes) != own) {
            query.CommitNonOpaqueTriangleHit();
            query.Abort();
        }
    }
    InterlockedAdd(g_counters[17], 1u);
    return query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

// What an emitter adds at a surface unshadowed, as a share of the surface's own colour: its intensity over the
// distance squared and its own size squared, by the surface's facing max(n.l, 0) (LIGHT-MODEL.md section 5: no wrap,
// A2-01), ending at its reach without a step. Nothing on the thing it belongs to (`self`) nor past its reach.
float3 emitter_light(uint emitter, float3 at, float3 facing, uint self, out float4 place, out uint slot) {
    place = asfloat(g_drops.Load4(emitter));
    const float4 colour = asfloat(g_drops.Load4(emitter + 16u));
    slot = g_drops.Load(emitter + 32u);
    // A disc (PLAN-LIGHTS L2, l93: a glowing mesh's kind 1, its word 40 bits 0-2 = 5): lit at its point nearest the
    // surface (Karis 2013, area lights' representative point), by its own cosine there, one-sided unless bit 3.
    // place.w stays its radius: the shadow ray's spread (rt_toward_light).
    float disc_cosine = 1.0f;
    const uint shape = g_drops.Load(emitter + 40u);
    if (g_drops.Load(emitter + 36u) == 1u && (shape & 7u) == 5u) {
        float3 n = float3(float2(shape >> 4u & 0x3FFFu, shape >> 18u) * (2.0f / 16383.0f) - 1.0f, 0.0f);
        n.z = 1.0f - abs(n.x) - abs(n.y);
        const float fold = saturate(-n.z);
        n.xy += float2(n.x >= 0.0f ? -fold : fold, n.y >= 0.0f ? -fold : fold);
        n = normalize(n);
        const float3 off = at - place.xyz;
        float3 across = off - dot(off, n) * n;
        const float wide = length(across);
        across *= wide > place.w ? place.w / wide : 1.0f;
        place.xyz += across;
        const float3 back = at - place.xyz;
        const float c = dot(back, n) * rsqrt(max(dot(back, back), 1e-4f));
        disc_cosine = (shape & 8u) != 0u ? abs(c) : max(c, 0.0f);
    }
    const float3 to = place.xyz - at;
    const float far2 = dot(to, to);
    if (slot == self || far2 > colour.w * colour.w) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    const float facing_it = max(dot(facing, to) * rsqrt(max(far2, 1e-4f)), 0.0f);
    const float fade = saturate(1.0f - far2 / (colour.w * colour.w));
    return colour.rgb * (place.w * place.w / (far2 + place.w * place.w) * facing_it * fade * fade * disc_cosine);
}

// ---- The surface images (rt_surface.hlsl writes them; every later pass reads them) ----
// S.guide   r eye distance, g and b the object's slot in two parts (each whole in a half float), a the smooth normal
//           packed (rt_pack_normal; 0: no surface: every later pass leaves the pixel)
// S.normal  rg the smooth normal, ba the geometric normal, each on the octahedral map (rt_oct)
// S.motion  xy the point's move in pixels to where it was, z its eye distance there (0: unknown), w the lift: how far
//           along the geometric normal the smooth surface's point lies (a ray leaves from at + n * (skin + lift))
// S.material r roughness, g metal, b class (low 4 bits) and flags (high 4: 16 lit, 32 emissive, 64 a painted
//           reflection, 128 drawn from both sides), a the share of its look a painted reflection carries
// S.base    rgb the surface's colour under full light (the record's colour by its first map), a its alpha

float2 rt_oct(float3 n) {
    const float3 a = n / max(abs(n.x) + abs(n.y) + abs(n.z), 1e-6f);
    return a.z >= 0.0f ? a.xy : (1.0f - abs(a.yx)) * float2(a.x >= 0.0f ? 1.0f : -1.0f, a.y >= 0.0f ? 1.0f : -1.0f);
}

float3 rt_unoct(float2 o) {
    float3 n = float3(o, 1.0f - abs(o.x) - abs(o.y));
    const float t = saturate(-n.z);
    n.xy += float2(n.x >= 0.0f ? -t : t, n.y >= 0.0f ? -t : t);
    return normalize(n);
}

static const uint kSurfaceLit = 16u, kSurfaceEmissive = 32u, kSurfaceEnvironment = 64u, kSurfaceTwoSided = 128u;
// The hit record's flags (nebula_model.h NEBULA_HIT_*) a pass reads from item word 11.
static const uint kHitPaintedShadow = 64u;

// A pixel's surface as the surface pass left it.
struct RtSurface {
    float away;        // eye distance
    float3 at;         // the eye-space place
    float3 smooth_;    // the shading normal, toward the eye
    float3 normal;     // the geometric normal, toward the eye
    float lift;        // the smooth surface's point over the geometric (S.motion.w)
    uint slot;         // the object's slot
    float skin;        // how far off the surface a ray leaves (the depth's steps grow with distance)
    float3 origin;     // where a ray toward a light leaves from on the lit side
};

// The opaque layer's place comes from the depth at full precision; the blended layer's (which has no depth of its own)
// from the guide's distance.
RtSurface rt_surface(int2 pixel, float4 guide, float4 normals, float lift) {
    RtSurface s;
    const float2 centre = float2(pixel) + 0.5f;
    s.away = rt_second_layer() ? guide.r : -rt_eye_z(g_depth, g_depth_map.Load(int3(pixel, 0)));
    s.at = rt_eye_ray(g_ray, centre) * guide.r;
    s.smooth_ = rt_unoct(normals.rg);
    s.normal = rt_unoct(normals.ba);
    s.lift = lift;
    s.slot = uint(guide.g) + uint(guide.b) * 1024u;
    s.skin = max(0.003f * s.away, 0.5f);
    s.origin = s.at + s.normal * (s.lift + s.skin);
    return s;
}

#endif
