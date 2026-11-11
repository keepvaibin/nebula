// SPDX-License-Identifier: GPL-3.0-only
// The light pass: what reaches each pixel of the scene's opaque surfaces, before they are shaded. The prepass left
// the scene's depth and each pixel's item. From them, a pixel gets:
//   r  the sun's share on a lit surface: one ray toward a point of the stage's shadow sun (every solid thing stops it);
//   b  the point light's share on a lit surface: one ray toward a point of the lamp of its light set;
//   a  the ground's share, on every surface that takes shadows: darkened where one of the game's drop shadows falls
//      (an actor stands on the line back along its drop: the game's landing cue, with the actor's own outline) and,
//      on a painted surface, where a lit object stands between it and the sun (painted surfaces carry their own
//      shading, so only what moves is added, by the ratio the stage's ambient and key give); the darker of the two.
// The programs' traced forms read them (program.hlsli, program_lit.hlsli): a lit program multiplies its key and point
// light by r and b, every program its colour by a. g is the filter's (rt_light_time.hlsl). Everything is in the
// scene camera's eye space, as the scene's instances are.
#include "rt_bindings.hlsli"

RaytracingAccelerationStructure g_scene : register(t0);
// Twelve uints an item: its slot; bits 0 lit, 1 takes drop shadows, 2 a surface of the scene, 3 a sheet drawn from both
// sides, 8-31 its light set from 1; then its mesh for a pixel that wants its triangle: the arena bound (8 up: none),
// its attributes' and indices' first bytes there, an index's bytes, its positions' first byte, its matrix's first row;
// then, for an alpha-tested material, its first map among the textures (all ones: none) and the alpha from which it
// is there; its matrix stream's first byte from 1 (0: the one row places every vertex; else a uint a vertex names the
// matrix, six rows each from the item's first row); what a ray that meets it shows: its material's class, flags,
// roughness and metal, a byte each; its colour under full light, a byte a channel, and the share of its look a
// painted reflection carries; the first row its matrix had the frame before; two spare. Sixteen in all.
// Bit 4 of the flags word: the sky (shown as it is).
ByteAddressBuffer g_items : register(t1);
ByteAddressBuffer g_sets : register(t2);   // 16 bytes a set: its lamp's place and radius (0: none)
ByteAddressBuffer g_drops : register(t3);  // 48 bytes a drop: its base, its reach across; its direction, its length; its caster's slot
ByteAddressBuffer g_tiles : register(t4);  // two uints a tile (first entry, how many), then one uint an entry (a drop)
Texture2D<float> g_depth_map : register(t5);
Texture2D<uint2> g_item_map : register(t6);  // the pixel's item, from 1, and its triangle
// The mesh memory: the arenas the frame's meshes lie in, and the frame's rows (six a matrix: three of its place,
// three of its normal).
ByteAddressBuffer g_arena0 : register(t15);
ByteAddressBuffer g_arena1 : register(t16);
ByteAddressBuffer g_arena2 : register(t17);
ByteAddressBuffer g_arena3 : register(t18);
ByteAddressBuffer g_arena4 : register(t19);
ByteAddressBuffer g_arena5 : register(t20);
ByteAddressBuffer g_arena6 : register(t21);
ByteAddressBuffer g_arena7 : register(t22);
ByteAddressBuffer g_rows : register(t23);
// Every texture of the renderer's, by the numbers the draws' programs use, and one sampler (linear, repeating).
Texture2D<float4> g_textures[] : register(t0, space1);
SamplerState g_wrap : register(s0);
RT_FORMAT(rgba16f) RWTexture2D<float4> g_out : register(u0);
// What the filter tells surfaces apart by: the pixel's eye distance, its object's slot in two parts (each whole in a
// half float), and where the pixel is a surface of the prepass its normal (rt_pack_normal: 1 and up; 0 no surface).
RT_FORMAT(rgba16f) RWTexture2D<float4> g_guide : register(u1);
// The added light: what the local lights give the pixel, as a share of the surface's own colour. The first layer
// writes it for the filter (red, green, blue in r, b, a: the channels the filter averages); the second layer writes
// rgb as the programs read it.
RT_FORMAT(rgba16f) RWTexture2D<float4> g_added : register(u2);
// What the pixel mirrors: rgb what its mirror ray sees by how much the surface mirrors; a how much of the surface's
// own colour stays under it, plus 2 in the second layer (program.hlsli traced_mirror).
RT_FORMAT(rgba16f) RWTexture2D<float4> g_mirror : register(u3);
// Where the pixel's surface point was in the frame before: xy its move in pixels (to there), z its distance from the
// eye there, w 1 where known. The point itself is followed: its triangle's corners through the rows their matrices
// had the frame before (a moving thing, a bending skin and the camera's own move alike).
RT_FORMAT(rgba16f) RWTexture2D<float4> g_motion : register(u4);
// The pixel's reservoir (Bitterli et al. 2020): the one light kept of all that were weighed for it. r, g: the light's
// number in this frame's list, from 1, in two parts (each whole in a half float; 0: none); b: its weight (what its
// unshadowed light is multiplied by: the sum of the weights it was chosen among over its own, 0 where the light was
// found in shadow); a: how many candidates stand behind it (no more than 20).
RT_FORMAT(rgba16f) RWTexture2D<float4> g_reservoirs : register(u5);
Texture2D<float4> g_reservoirs_before : register(t9);  // the frame before's
Texture2D<float4> g_guide_before : register(t10);      // and its guide: what surface each of those pixels was
RWStructuredBuffer<uint> g_counters : register(u6);

cbuffer LightConstants : register(b0) {
    float4 g_ray;       // rt_eye_ray
    float4 g_depth;     // rt_eye_z
    float4 g_viewport;  // the scene's viewport in target pixels
    uint4 g_at;         // where each buffer's first byte is: items, sets, drops, tiles
    uint4 g_more;       // x: the entries' first byte (in the tiles' buffer); y: the frame's number (the noise's turn);
                        // z: bit 0 the sun is traced, bit 1 the lamps, bit 2 the drops, bit 3 the sky's light near
                        // the surface, bit 4 the second layer (the item map is the blended surfaces': a pixel's
                        // place is where its eye ray meets its triangle, rays go straight, pixels without an item
                        // keep what the picture holds), bit 5 mirrors (what glass, metal and gloss show of the scene),
                        // bit 6 the actors' point lights are emitters: the light set's own lamp is put out,
                        // bit 7 the emitters alone (--diagnostics rt-added: no sun, no sky)
    uint4 g_grid;       // tiles across, tiles down, pixels a tile
    float4 g_sun;       // xyz: the direction to the shadow sun; w: the tangent of its half angle
    float4 g_stage;     // x: the stage's ambient, y: its key light, as brightness: a painted surface in the key's
                        // shadow keeps x / (x + y * n.l) of its colour; z: how dark a drop shadow is; w: the tangent
                        // of the drop light's half angle
    float4 g_look;      // x: what a surface in the sun's shadow keeps of the sky's light (the sky turned down so the
                        // sun's shadows read); y 1: a painted surface is shadowed by lit things only (the painted
                        // style), else by everything solid; z: the facing (n.l) from which a painted surface keeps
                        // its authored brightness; w: how far the sky's ray looks for what stands near
    uint4 g_glow;       // the emitters (48 bytes each: centre, radius; colour, reach; slot): x their first byte in the
                        // drops' buffer, y their tiles' and z their entries' first byte in the tiles' buffer, w how many
    uint4 g_mesh_at[3]; // where each arena's first byte is in its buffer (eight of them), then the rows'
    float4 g_sky;       // rgb: what a mirror shows where its ray meets nothing at all
    float4 g_up;        // xyz: the player's up in the eye's space (where the sky is)
    float4 g_ray_before;   // rt_eye_ray of the frame before
    float4 g_before[3];    // the frame before's eye space from this frame's (rows of a 3x4 matrix): for a pixel whose
                           // mesh the pass cannot read, only the camera's move is known
    uint4 g_kept;          // x: the first byte, in the drops' buffer, of where each emitter of the frame before is in
                           // this frame's list (a uint each; past the list: gone); y: how many the frame before had;
                           // z 1: there is a frame before to read
};

static const float kFarLight = 20000.0f;
static const uint kItemLit = 1u, kItemGround = 2u, kItemSurface = 4u, kItemThin = 8u;
static const uint kItemBytes = 64u;
static const uint kItemSky = 16u;
// Material classes (nebula_model.h NEBULA_HIT_*).
static const uint kClassGloss = 1u, kClassMetal = 2u, kClassGlass = 3u, kClassEmissive = 6u;

// Two numbers in 0-1 for a pixel, a frame and a light: interleaved gradient noise (Jimenez 2014), turned each frame,
// and a hash of the same.
float2 light_noise(uint2 pixel, uint frame, uint light) {
    if ((g_more.z & 16u) != 0u) {
        return float2(0.0f, 0.0f);  // the second layer: straight at the light (nothing averages it after)
    }
    const float2 at = float2(pixel) + 5.588238f * float((frame + light * 17u) & 63u);
    const float a = frac(52.9829189f * frac(0.06711056f * at.x + 0.00583715f * at.y));
    const float b = float(rt_hash(pixel.x + pixel.y * 8192u + frame * 9781u + light * 6271u) & 0xFFFFu) / 65536.0f;
    return float2(a, b);
}

// The eye-space place of a pixel of the depth map, or z = 0 where there is none.
float3 place_at(int2 pixel) {
    const float depth = g_depth_map.Load(int3(pixel, 0));
    if (rt_far(g_depth, depth)) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    return rt_eye_ray(g_ray, float2(pixel) + 0.5f) * -rt_eye_z(g_depth, depth);
}

// The surface's own normal from the depth around the pixel: across and down, each from the neighbour nearer in
// depth, so an edge takes its own side.
float3 surface_normal(int2 pixel, float3 at) {
    const float3 l = place_at(pixel + int2(-1, 0)), r = place_at(pixel + int2(1, 0));
    const float3 u = place_at(pixel + int2(0, -1)), d = place_at(pixel + int2(0, 1));
    const float3 across = (r.z != 0.0f && (l.z == 0.0f || abs(r.z - at.z) < abs(l.z - at.z))) ? r - at : (l.z != 0.0f ? at - l : float3(0.0f, 0.0f, 0.0f));
    const float3 down = (d.z != 0.0f && (u.z == 0.0f || abs(d.z - at.z) < abs(u.z - at.z))) ? d - at : (u.z != 0.0f ? at - u : float3(0.0f, 0.0f, 0.0f));
    // Pixels go right and down; the eye looks along -z: this cross faces the eye.
    const float3 n = cross(down, across);
    const float size = dot(n, n);
    return size > 0.0f ? n * rsqrt(size) : float3(0.0f, 0.0f, 1.0f);
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

// The surface's shading normal at the pixel, in the eye's space: its triangle's three vertex normals by where the
// pixel lies among the corners, through the mesh's matrix. `geometric` where the item gives no triangle (a skinned or
// frame-made mesh) or the numbers fail. It faces the way `geometric` does.
// `lifted`: the pixel's place on the smooth surface the triangle stands for, where a ray toward a light leaves from.
// The place set on each corner's tangent plane (never below the triangle), the three by the same weights (Hanika,
// "Hacking the Shadow Terminator", Ray Tracing Gems II 2021): a ray from a rounded mesh's facet near the edge of the
// light then clears the facets beside it, and the edge of the light is the smooth normal's, not the facets'.
// `before`: where the pixel's point was in the frame before's eye space (its corners through the rows of the frame
// before, `row_before`), and `known` whether the mesh gave it.
float3 shading_normal(uint4 first, uint4 second, uint stream, uint row_before, uint triangle_, float3 at, float3 geometric,
                      out float3 lifted, out float3 before, out bool known) {
    lifted = at;
    before = at;
    known = false;
    const uint arena = first.z;
    if (arena >= 8u) {
        return geometric;
    }
    const uint base = mesh_at(arena);
    const uint indices = base + second.x;
    uint3 corner;
    if (second.y == 2u) {
        corner = uint3(index16(arena, indices, triangle_ * 3u), index16(arena, indices, triangle_ * 3u + 1u), index16(arena, indices, triangle_ * 3u + 2u));
    } else {
        corner = arena_words(arena, indices + triangle_ * 12u);
    }
    float3 place[3], normal[3], place_before[3];
    for (uint c = 0u; c < 3u; ++c) {
        const uint row = vertex_rows(arena, base, second.w, stream, corner[c]);
        const float4 r0 = asfloat(g_rows.Load4(row)), r1 = asfloat(g_rows.Load4(row + 16u)), r2 = asfloat(g_rows.Load4(row + 32u));
        const float3 n0 = asfloat(g_rows.Load3(row + 48u)), n1 = asfloat(g_rows.Load3(row + 64u)), n2 = asfloat(g_rows.Load3(row + 80u));
        const float4 p = float4(asfloat(arena_words(arena, base + second.z + corner[c] * 12u)), 1.0f);
        place[c] = float3(dot(r0, p), dot(r1, p), dot(r2, p));
        const float3 n = asfloat(arena_words(arena, base + first.w + corner[c] * 24u));
        normal[c] = float3(dot(n0, n), dot(n1, n), dot(n2, n));
        const uint old_row = vertex_rows(arena, base, row_before, stream, corner[c]);
        const float4 b0 = asfloat(g_rows.Load4(old_row)), b1 = asfloat(g_rows.Load4(old_row + 16u)), b2 = asfloat(g_rows.Load4(old_row + 32u));
        place_before[c] = float3(dot(b0, p), dot(b1, p), dot(b2, p));
    }
    // Where the pixel lies among the corners.
    const float3 e0 = place[1] - place[0], e1 = place[2] - place[0], e2 = at - place[0];
    const float d00 = dot(e0, e0), d01 = dot(e0, e1), d11 = dot(e1, e1), d20 = dot(e2, e0), d21 = dot(e2, e1);
    const float scale = d00 * d11 - d01 * d01;
    if (!(abs(scale) > 1e-12f)) {
        return geometric;
    }
    const float v = saturate((d11 * d20 - d01 * d21) / scale);
    const float w = saturate((d00 * d21 - d01 * d20) / scale);
    const float u = saturate(1.0f - v - w);
    // The same point of the triangle, where its corners were.
    before = (place_before[0] * u + place_before[1] * v + place_before[2] * w) / max(u + v + w, 1e-6f);
    known = true;
    float3 n = normal[0] * u + normal[1] * v + normal[2] * w;
    const float size = dot(n, n);
    if (!(size > 1e-12f)) {
        return geometric;
    }
    n *= rsqrt(size);
    // Vertex normals that do not go with the surface they are on (an unlit model's, never looked at when it was
    // made: a flat slab with normals along the ground) are not its shape: the surface's own normal serves.
    if (abs(dot(n, geometric)) < 0.5f) {
        return geometric;
    }
    const float side = dot(n, geometric) < 0.0f ? -1.0f : 1.0f;
    const float3 weight = float3(u, v, w);
    float3 rise = float3(0.0f, 0.0f, 0.0f);
    for (uint k = 0u; k < 3u; ++k) {
        const float size_k = dot(normal[k], normal[k]);
        if (size_k > 1e-12f) {
            const float3 n_k = normal[k] * (side * rsqrt(size_k));
            rise -= n_k * (weight[k] * min(0.0f, dot(at - place[k], n_k)));
        }
    }
    // No farther than the triangle is wide: a mesh with normals that disagree wildly keeps its place.
    const float limit = 0.5f * sqrt(max(d00, d11));
    const float rise_size = length(rise);
    lifted = at + (rise_size > limit ? rise * (limit / rise_size) : rise);
    return n * side;
}

// Where a pixel's eye ray (`ray`, from the eye at the origin) meets its triangle, and the triangle's normal toward the
// eye: the second layer's surfaces have no depth of their own. False where the item gives no mesh or the ray misses
// the triangle's plane.
bool triangle_place(uint4 first, uint4 second, uint stream, uint triangle_, float3 ray, out float3 at, out float3 normal) {
    at = float3(0.0f, 0.0f, 0.0f);
    normal = float3(0.0f, 0.0f, 1.0f);
    const uint arena = first.z;
    if (arena >= 8u) {
        return false;
    }
    const uint base = mesh_at(arena);
    const uint indices = base + second.x;
    uint3 corner;
    if (second.y == 2u) {
        corner = uint3(index16(arena, indices, triangle_ * 3u), index16(arena, indices, triangle_ * 3u + 1u), index16(arena, indices, triangle_ * 3u + 2u));
    } else {
        corner = arena_words(arena, indices + triangle_ * 12u);
    }
    float3 place[3];
    for (uint c = 0u; c < 3u; ++c) {
        const uint row = vertex_rows(arena, base, second.w, stream, corner[c]);
        const float4 r0 = asfloat(g_rows.Load4(row)), r1 = asfloat(g_rows.Load4(row + 16u)), r2 = asfloat(g_rows.Load4(row + 32u));
        const float4 p = float4(asfloat(arena_words(arena, base + second.z + corner[c] * 12u)), 1.0f);
        place[c] = float3(dot(r0, p), dot(r1, p), dot(r2, p));
    }
    float3 n = cross(place[1] - place[0], place[2] - place[0]);
    const float size = dot(n, n);
    if (!(size > 1e-12f)) {
        return false;
    }
    n *= rsqrt(size);
    const float toward = dot(n, ray);
    if (!(abs(toward) > 1e-6f)) {
        return false;
    }
    const float along = dot(n, place[0]) / toward;
    if (!(along > 0.0f)) {
        return false;
    }
    at = ray * along;
    normal = toward < 0.0f ? n : -n;
    return true;
}

// A direction within `spread` (a tangent) of `toward`, by two numbers in 0-1.
float3 within(float3 toward, float spread, float2 noise) {
    const float turn = 6.2831853f * noise.x;
    const float out_ = sqrt(noise.y) * spread;
    const float3 side = normalize(abs(toward.y) < 0.9f ? cross(toward, float3(0.0f, 1.0f, 0.0f)) : cross(toward, float3(1.0f, 0.0f, 0.0f)));
    const float3 up = cross(side, toward);
    return normalize(toward + (side * cos(turn) + up * sin(turn)) * out_);
}

// A direction over the half-space `n` faces, more often near `n` (by the cosine), by two numbers in 0-1.
float3 around(float3 n, float2 noise) {
    const float turn = 6.2831853f * noise.x;
    const float out_ = sqrt(noise.y);
    const float3 side = normalize(abs(n.y) < 0.9f ? cross(n, float3(0.0f, 1.0f, 0.0f)) : cross(n, float3(1.0f, 0.0f, 0.0f)));
    const float3 up = cross(side, n);
    return normalize(side * (out_ * cos(turn)) + up * (out_ * sin(turn)) + n * sqrt(max(1.0f - noise.y, 0.0f)));
}

// How far the ray goes before something solid stands on it: `reach` when nothing does.
float clear_to(float3 origin, float3 direction, float reach) {
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0f;
    ray.TMax = reach;
    RayQuery<RAY_FLAG_NONE> query;
    query.TraceRayInline(g_scene, RAY_FLAG_FORCE_OPAQUE, kMaskSolid, ray);
    query.Proceed();
    InterlockedAdd(g_counters[17], 1u);
    return query.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? query.CommittedRayT() : reach;
}

// Whether an alpha-tested surface is there where a ray meets it: item `item`'s triangle `triangle_`, at `where_`
// among its corners (the second and third's weights). Its first map's alpha at the vertices' first coordinates,
// against the material's threshold: what the cutout of a leaf, a fence or a flower is drawn by. Where the item gives
// no mesh or no map (a skinned or frame-made mesh), it is not there: a sheet whose holes the pass cannot read casts
// no shadow rather than a solid one (counted).
bool cutout_there(uint item, uint triangle_, float2 where_) {
    const uint at = g_at.x + item * kItemBytes;
    const uint4 first = g_items.Load4(at), second = g_items.Load4(at + 16u);
    const uint2 map = g_items.Load2(at + 32u);
    const uint arena = first.z;
    if (arena >= 8u || map.x == 0xFFFFFFFFu) {
        InterlockedAdd(g_counters[22], 1u);
        return false;
    }
    const uint base = mesh_at(arena);
    const uint indices = base + second.x;
    uint3 corner;
    if (second.y == 2u) {
        corner = uint3(index16(arena, indices, triangle_ * 3u), index16(arena, indices, triangle_ * 3u + 1u), index16(arena, indices, triangle_ * 3u + 2u));
    } else {
        corner = arena_words(arena, indices + triangle_ * 12u);
    }
    // A vertex's colour word and first coordinate: the 12 bytes from its 12th.
    const float2 c0 = asfloat(arena_words(arena, base + first.w + corner.x * 24u + 12u).yz);
    const float2 c1 = asfloat(arena_words(arena, base + first.w + corner.y * 24u + 12u).yz);
    const float2 c2 = asfloat(arena_words(arena, base + first.w + corner.z * 24u + 12u).yz);
    const float2 coordinate = c0 * (1.0f - where_.x - where_.y) + c1 * where_.x + c2 * where_.y;
    InterlockedAdd(g_counters[21], 1u);
    return g_textures[NonUniformResourceIndex(map.x)].SampleLevel(g_wrap, coordinate, 0.0f).a >= asfloat(map.y);
}

// Whether anything of `mask` stands on the ray; `met`: the item of what does (the nearest, unless `any` asks only
// whether). With kMaskCutout in the mask, alpha-tested surfaces stand there where their map says they are.
bool blocked(float3 origin, float3 direction, float reach, uint mask, bool any, out uint met) {
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0f;
    ray.TMax = reach;
    const uint flags = ((mask & kMaskCutout) != 0u ? RAY_FLAG_NONE : RAY_FLAG_FORCE_OPAQUE) |
                       (any ? RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH : RAY_FLAG_NONE);
    RayQuery<RAY_FLAG_NONE> query;
    query.TraceRayInline(g_scene, flags, mask, ray);
    while (query.Proceed()) {
        if (query.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
            cutout_there(query.CandidateInstanceID(), query.CandidatePrimitiveIndex(), query.CandidateTriangleBarycentrics())) {
            query.CommitNonOpaqueTriangleHit();
        }
    }
    InterlockedAdd(g_counters[17], 1u);
    met = 0u;
    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT) {
        return false;
    }
    met = query.CommittedInstanceID();
    return true;
}

// What a ray sees: the colour of the nearest thing on it, as the scene's light leaves it. The scene's solid things
// and its cutouts where their maps say they are, else the sky's dome, else g_sky. A thing shows its first map at
// the place met (the level of the map by how wide the ray has grown: `wide` at its start, `widens` a unit of way),
// or its material's colour; under the sun where the sun reaches the place met, else what a shadow keeps; the sky
// and what shows itself without light as they are.
float3 seen_along(float3 origin, float3 direction, float wide, float widens) {
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0f;
    ray.TMax = 4000000.0f;
    RayQuery<RAY_FLAG_NONE> query;
    query.TraceRayInline(g_scene, RAY_FLAG_NONE, kMaskSolid | kMaskCutout | kMaskSky, ray);
    while (query.Proceed()) {
        if (query.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
            cutout_there(query.CandidateInstanceID(), query.CandidatePrimitiveIndex(), query.CandidateTriangleBarycentrics())) {
            query.CommitNonOpaqueTriangleHit();
        }
    }
    InterlockedAdd(g_counters[23], 1u);
    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT) {
        return g_sky.rgb;
    }
    const uint at = g_at.x + query.CommittedInstanceID() * kItemBytes;
    const uint4 first = g_items.Load4(at), second = g_items.Load4(at + 16u), third = g_items.Load4(at + 32u);
    const uint base_word = g_items.Load(at + 48u);
    const uint class_ = third.w & 255u;
    const bool sky = (first.y & kItemSky) != 0u;
    float3 colour = float3(float(base_word & 255u), float(base_word >> 8u & 255u), float(base_word >> 16u & 255u)) / 255.0f;
    float3 facing = -direction;
    const uint arena = first.z;
    if (arena < 8u) {
        const uint base = mesh_at(arena);
        const uint indices = base + second.x;
        const uint triangle_ = query.CommittedPrimitiveIndex();
        uint3 corner;
        if (second.y == 2u) {
            corner = uint3(index16(arena, indices, triangle_ * 3u), index16(arena, indices, triangle_ * 3u + 1u), index16(arena, indices, triangle_ * 3u + 2u));
        } else {
            corner = arena_words(arena, indices + triangle_ * 12u);
        }
        float3 place[3];
        float2 coordinate[3];
        for (uint c = 0u; c < 3u; ++c) {
            const uint row = vertex_rows(arena, base, second.w, third.z, corner[c]);
            const float4 r0 = asfloat(g_rows.Load4(row)), r1 = asfloat(g_rows.Load4(row + 16u)), r2 = asfloat(g_rows.Load4(row + 32u));
            const float4 p = float4(asfloat(arena_words(arena, base + second.z + corner[c] * 12u)), 1.0f);
            place[c] = float3(dot(r0, p), dot(r1, p), dot(r2, p));
            coordinate[c] = asfloat(arena_words(arena, base + first.w + corner[c] * 24u + 12u).yz);
        }
        const float3 across = cross(place[1] - place[0], place[2] - place[0]);
        const float area = length(across);
        if (area > 1e-9f) {
            const float3 n = across / area;
            facing = dot(n, direction) < 0.0f ? n : -n;
        }
        if (third.x != 0xFFFFFFFFu) {
            const float2 where_ = query.CommittedTriangleBarycentrics();
            const float2 uv = coordinate[0] * (1.0f - where_.x - where_.y) + coordinate[1] * where_.x + coordinate[2] * where_.y;
            // The map's level: how many of its texels the ray's width covers at the place met.
            uint map_width, map_height;
            g_textures[NonUniformResourceIndex(third.x)].GetDimensions(map_width, map_height);
            const float2 e1 = coordinate[1] - coordinate[0], e2 = coordinate[2] - coordinate[0];
            const float texels = abs(e1.x * e2.y - e1.y * e2.x) * float(map_width) * float(map_height);
            const float across_ray = wide + widens * query.CommittedRayT();
            const float level = 0.5f * log2(max(texels / max(area, 1e-9f) * across_ray * across_ray, 1.0f));
            colour = g_textures[NonUniformResourceIndex(third.x)].SampleLevel(g_wrap, uv, level).rgb;
        }
    }
    if (sky || class_ == kClassEmissive) {
        return colour;
    }
    // Its light: the sun where it reaches the place met, as a painted surface takes it (main, below).
    float sunlit = 1.0f;
    if ((g_more.z & 1u) != 0u) {
        const float lambert = dot(facing, g_sun.xyz);
        sunlit = 0.0f;
        if (lambert > 0.0f) {
            const float3 met_at = origin + direction * query.CommittedRayT();
            uint met;
            if (!blocked(met_at + facing * max(0.002f * query.CommittedRayT(), 0.5f), g_sun.xyz, 200000.0f, kMaskSolid | kMaskCutout, true, met)) {
                sunlit = saturate(lambert / g_look.z);
            }
        }
    }
    return colour * lerp(g_look.x, 1.0f, sunlit);
}

// What an emitter adds at a surface unshadowed, as a share of the surface's own colour: its intensity over the
// distance squared and its own size squared, by the surface's facing, ending at its reach without a step. Nothing on
// the thing it belongs to (`self`) nor past its reach.
float3 emitter_light(uint emitter, float3 at, float3 facing, uint self, out float4 place, out uint slot) {
    place = asfloat(g_drops.Load4(emitter));
    const float4 colour = asfloat(g_drops.Load4(emitter + 16u));
    slot = g_drops.Load(emitter + 32u);
    const float3 to = place.xyz - at;
    const float far2 = dot(to, to);
    if (slot == self || far2 > colour.w * colour.w) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    const float facing_it = max(dot(facing, to) * rsqrt(max(far2, 1e-4f)), 0.0f);
    const float fade = saturate(1.0f - far2 / (colour.w * colour.w));
    return colour.rgb * (place.w * place.w / (far2 + place.w * place.w) * (0.25f + 0.75f * facing_it) * fade * fade);
}

float rt_unit(uint seed) {
    return float(seed & 0xFFFFFFu) / 16777216.0f;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint width, height;
    g_out.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) {
        return;
    }
    float4 share = float4(1.0f, 1.0f, 1.0f, 1.0f);
    float4 guide = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float3 added = float3(0.0f, 0.0f, 0.0f);
    float4 mirror = float4(0.0f, 0.0f, 0.0f, 1.0f);
    const int2 pixel = int2(id.xy);
    const float2 centre = float2(id.xy) + 0.5f;
    const uint2 named = g_item_map.Load(int3(pixel, 0));
    const uint item = named.x;
    const float depth = g_depth_map.Load(int3(pixel, 0));
    const bool second = (g_more.z & 16u) != 0u;
    if (!second) {
        g_reservoirs[id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);  // none, until the local lights say otherwise
    }
    if (second && item == 0u) {
        return;  // no blended surface here: the picture keeps the first layer's light
    }
    if (item != 0u && (second || !rt_far(g_depth, depth)) && centre.x >= g_viewport.x && centre.y >= g_viewport.y &&
        centre.x < g_viewport.x + g_viewport.z && centre.y < g_viewport.y + g_viewport.w) {
        const uint4 words4 = g_items.Load4(g_at.x + (item - 1u) * kItemBytes);
        const uint4 mesh = g_items.Load4(g_at.x + (item - 1u) * kItemBytes + 16u);
        const uint stream = g_items.Load(g_at.x + (item - 1u) * kItemBytes + 40u);
        const uint2 words = words4.xy;
        if ((words.y & kItemSurface) == 0u) {
            // In the prepass for its depth only (the sky): no light of the scene's falls on it.
            g_out[id.xy] = share;
            if (!second) {
                g_guide[id.xy] = guide;
            }
            g_added[id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
            g_mirror[id.xy] = float4(0.0f, 0.0f, 0.0f, second ? 3.0f : 1.0f);
            if (!second) {
                g_motion[id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
            }
            return;
        }
        const bool lit = (words.y & kItemLit) != 0u, ground = (words.y & kItemGround) != 0u;
        const uint set = words.y >> 8u;
        float away = -rt_eye_z(g_depth, depth);
        float3 at = rt_eye_ray(g_ray, centre) * away;
        float3 normal;
        if (second) {
            if (!triangle_place(words4, mesh, stream, named.y, rt_eye_ray(g_ray, centre), at, normal)) {
                g_out[id.xy] = share;
                g_added[id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
                g_mirror[id.xy] = float4(0.0f, 0.0f, 0.0f, 3.0f);
                return;
            }
            away = -at.z;
        } else {
            normal = surface_normal(pixel, at);
        }
        // The surface as its mesh shades it: the light falls off over it smoothly, whatever its facets.
        float3 lifted;
        float3 before;
        bool before_known;
        const float3 smooth_ = shading_normal(words4, mesh, stream, g_items.Load(g_at.x + (item - 1u) * kItemBytes + 52u), named.y, at, normal, lifted,
                                              before, before_known);
        if (!before_known) {
            const float4 here = float4(at, 1.0f);
            before = float3(dot(g_before[0], here), dot(g_before[1], here), dot(g_before[2], here));
        }
        if (!second) {
            float4 motion = float4(0.0f, 0.0f, 0.0f, 0.0f);
            if (before.z < -1.0f) {
                const float away_before = -before.z;
                const float2 was = float2((before.x / away_before - g_ray_before.y) / g_ray_before.x, (before.y / away_before - g_ray_before.w) / g_ray_before.z);
                motion = float4(was - centre, away_before, 1.0f);
            }
            g_motion[id.xy] = motion;
        }
        // From just off the surface: along its normal, more the farther the pixel is (depth's steps grow).
        const float skin = max(0.003f * away, 0.5f);
        const float3 origin = lifted + normal * skin;
        guide = float4(away, float(words.x & 1023u), float(words.x >> 10u), rt_pack_normal(smooth_));
        uint met;
        InterlockedAdd(g_counters[16], 1u);

        // The sun. A sheet drawn from both sides (a leaf) is lit from either; its ray leaves from the side the sun
        // is on. A side turned from the sun is in its own shadow, by the mesh's smooth normal (a lit program's sum
        // says so by itself; for any other surface the pass says it): no ray.
        const float turned = dot(smooth_, g_sun.xyz);
        const bool thin = (words.y & kItemThin) != 0u;
        const float facing = thin ? abs(turned) : turned;
        const float lambert = saturate(facing);
        float in_sun = 1.0f;  // how much of the sun the surface takes (1 with no sun traced)
        if ((g_more.z & 1u) != 0u) {
            bool shadowed = false;
            if (facing > 0.0f) {
                const float3 toward = within(g_sun.xyz, g_sun.w, light_noise(id.xy, g_more.y, 0u));
                // From a place within the pixel, another each frame: the average over time then holds the share of
                // the pixel a hard edge covers (a smooth edge, not steps). Where the eye's ray through that place
                // meets the surface's own plane, no farther than two pixels' width at this distance.
                float3 within_pixel = float3(0.0f, 0.0f, 0.0f);
                if (!second) {
                    const float3 through = rt_eye_ray(g_ray, centre + light_noise(id.xy, g_more.y, 11u) - 0.5f);
                    const float slant = dot(normal, through);
                    if (abs(slant) > 1e-4f) {
                        const float3 moved = through * (dot(normal, at) / slant) - at;
                        const float most = 2.0f * abs(g_ray.x) * away;
                        const float size = length(moved);
                        within_pixel = size > most ? moved * (most / size) : moved;
                    }
                }
                const float3 from = (turned >= 0.0f ? origin : at - normal * skin) + within_pixel;
                // A lit surface is shadowed by everything solid; so is a painted one, unless the painted style asks
                // for lit things only (what the painting could not know).
                shadowed = blocked(from, toward, 200000.0f, lit || g_look.y == 0.0f ? kMaskSolid | kMaskCutout : kMaskLit, true, met);
                if (shadowed) {
                    InterlockedAdd(g_counters[18], 1u);
                }
            } else if (!lit) {
                shadowed = true;
            }
            // How much of the sun the surface takes: all of it facing it, none turned from it or in shadow. A lit
            // program sums its key by that share (r): its shadow is the key it loses, and its ambient stays whole.
            // A painted surface keeps its authored brightness where the sun reaches it at g_look.z or more of full
            // facing, and falls to g_look.x of it as the sun leaves it: never brighter than painted, and one number
            // says how dark a shadow is in every stage.
            const float reached = shadowed ? 0.0f : 1.0f;
            const float sunlit = reached * saturate(lambert / g_look.z);
            share.r = reached;
            share.a = lit ? 1.0f : lerp(g_look.x, 1.0f, sunlit);
            in_sun = lit ? reached * lambert : sunlit;
        }

        // The sky's light. A frame takes one of two rays (the average over time holds both): over the half-space
        // the surface faces, short: what stands close by hides that much of the sky (corners, the foot of a wall);
        // or toward the sky itself, about straight up, long: what stands over the surface hides the sky's brightest
        // part, the soft shadow everything has on an overcast day and in a sunny day's shade. So a thing the sun
        // does not reach still stands on a shadow of its own. In the sun the sky's part of the light is small.
        if ((g_more.z & 8u) != 0u && g_look.w > 0.0f) {
            const float2 chance = light_noise(id.xy, g_more.y, 7u);
            float seen;
            if (light_noise(id.xy, g_more.y, 9u).y < 0.5f) {
                const float open = clear_to(origin, around(smooth_, chance), g_look.w) / g_look.w;
                seen = lerp(0.5f, 1.0f, open * (2.0f - open));
            } else {
                const float3 over = within(g_up.xyz, 0.6f, chance);
                seen = clear_to(origin, over, 1200.0f) < 1200.0f ? 0.42f : 1.0f;  // what stands within 12 m over it
            }
            share.a *= lerp(seen, 1.0f, 0.65f * in_sun);
        }

        // The lamp of the pixel's light set: the game's one point light. Where the actors' point lights are
        // emitters (bit 6: each lights everything near it, below), a lit program's own share of it is put out.
        if ((g_more.z & 64u) != 0u) {
            share.b = 0.0f;
        } else if ((g_more.z & 2u) != 0u && lit && set != 0u) {
            const float4 lamp = asfloat(g_sets.Load4(g_at.y + (set - 1u) * 16u));
            const float3 to = lamp.xyz - at;
            const float reach = length(to);
            if (lamp.w != 0.0f && reach > 1.0f && dot(normal, to) > 0.0f) {
                const float3 toward = within(to / reach, reach > kFarLight ? 0.0047f : lamp.w / reach, light_noise(id.xy, g_more.y, 1u));
                if (blocked(origin, toward, min(reach, 200000.0f), kMaskSolid | kMaskCutout, true, met)) {
                    InterlockedAdd(g_counters[20], 1u);
                    share.b = 0.0f;
                }
            }
        }

        // What the surface mirrors: glass, metal and glossy things show the scene and the sky along the eye's ray
        // turned at the surface, more at a glancing look (Schlick's term), a metal in its own colour; and the sun's
        // own glint where that ray runs at the sun unhindered.
        if ((g_more.z & 32u) != 0u) {
            const uint look = g_items.Load(g_at.x + (item - 1u) * kItemBytes + 44u);
            const uint tint_word = g_items.Load(g_at.x + (item - 1u) * kItemBytes + 48u);
            const uint class_ = look & 255u;
            const float rough = float(look >> 16u & 255u) / 255.0f;
            float head_on = 0.0f;  // how much it mirrors seen head on
            float3 tint = float3(1.0f, 1.0f, 1.0f);
            if (class_ == kClassGlass) {
                head_on = 0.1f;
            } else if (class_ == kClassMetal) {
                head_on = 0.55f;
                tint = lerp(float3(1.0f, 1.0f, 1.0f), float3(float(tint_word & 255u), float(tint_word >> 8u & 255u), float(tint_word >> 16u & 255u)) / 255.0f, 0.5f);
            } else if (class_ == kClassGloss) {
                head_on = 0.05f * (1.0f - rough);
            }
            if (head_on > 0.0f) {
                const float3 look_along = normalize(at);
                const float3 n = dot(smooth_, look_along) < 0.0f ? smooth_ : normal;
                const float glance = 1.0f - saturate(-dot(n, look_along));
                const float share_ = head_on + (1.0f - head_on) * glance * glance * glance * glance * glance;
                const float3 turned_ray = reflect(look_along, n);
                const float pixel_wide = abs(g_ray.x);  // a pixel's width a unit of way from the eye
                float3 seen = seen_along(at + n * skin, turned_ray, away * pixel_wide, pixel_wide);
                if ((g_more.z & 1u) != 0u && dot(turned_ray, g_sun.xyz) > 0.9995f &&
                    !blocked(at + n * skin, g_sun.xyz, 200000.0f, kMaskSolid | kMaskCutout, true, met)) {
                    seen += float3(4.0f, 3.8f, 3.4f);  // the sun itself, far brighter than the picture's white
                }
                mirror.rgb = seen * tint * share_;
                // The painted reflection it was drawn with gives way to the real one: that much less of the colour
                // under the mirror (a blended surface: of its alpha, so glass clears).
                mirror.a = 1.0f - 0.6f * float(tint_word >> 24u) / 255.0f;
            }
        }

        const uint2 tile = id.xy / max(g_grid.z, 1u);
        // The local lights (every emitter: the game's point lights, glowing meshes, each particle of a glowing
        // effect), by spatiotemporal reservoir resampling (ReSTIR DI: Bitterli et al. 2020; resampled importance
        // sampling: Talbot et al. 2005). A pixel keeps ONE light, chosen in proportion to what each would add here
        // unshadowed, out of:
        //   this frame's candidates: up to eight of the tile's lights, one from each equal run of its list;
        //   the reservoirs of the frame before at the pixel's own point (found by its motion) and at two points
        //   near it, where the surface there is the pixel's own kind (in its plane, facing its way): each stands
        //   for all the candidates it was itself chosen among, so a pixel sees what hundreds of tries would find.
        // ONE ray then asks whether the kept light reaches the surface, and what is written is its light times its
        // weight: the sum of the weights it was chosen among, over its own and over how many of the candidates could
        // have chosen it (those whose surface it reaches: without that share a light's edge darkens). On average the
        // sum of every light with its shadow, at a cost that does not grow with the lights. A light found in shadow
        // is kept with no weight, so it is not handed on. The filter after the pass (rt_lights_time,
        // rt_lights_space) takes what noise is left. Not on itself. The second layer has no filter after it: there
        // up to 32 of the lights are summed as they are, unshadowed.
        if (g_glow.w != 0u && tile.x < g_grid.x && tile.y < g_grid.y) {
            static const float3 kLuminance = float3(0.2126f, 0.7152f, 0.0722f);
            const uint2 run = g_tiles.Load2(g_glow.y + (tile.y * g_grid.x + tile.x) * 8u);
            const uint lights = run.y;
            float4 place;
            uint slot;
            if (second) {
                const uint tries = min(lights, 32u);
                const float each = float(lights) / float(max(tries, 1u));
                for (uint k = 0u; k < tries; ++k) {
                    const uint index = g_tiles.Load(g_glow.z + (run.x + min(uint(float(k) * each), lights - 1u)) * 4u);
                    added += emitter_light(g_glow.x + index * 48u, at, smooth_, words.x, place, slot) * each;
                }
            } else {
                uint seed = rt_hash(id.x + id.y * 8192u + g_more.y * 9781u + 4099u);
                float sum = 0.0f;      // of the weights: what each would add here, over its chance of being tried
                float counted = 0.0f;  // the candidates behind what is combined
                uint kept = 0xFFFFFFFFu, kept_slot = 0u;
                float3 kept_light = float3(0.0f, 0.0f, 0.0f);
                float kept_target = 0.0f;
                float4 kept_at = float4(0.0f, 0.0f, 0.0f, 0.0f);
                // This frame's candidates.
                const uint tries = min(lights, 8u);
                const float each = float(lights) / float(max(tries, 1u));
                for (uint k = 0u; k < tries; ++k) {
                    seed = rt_hash(seed + k * 7919u + 1u);
                    const float within = lights <= tries ? 0.0f : rt_unit(seed);
                    const uint index = g_tiles.Load(g_glow.z + (run.x + min(uint((float(k) + within) * each), lights - 1u)) * 4u);
                    const float3 light = emitter_light(g_glow.x + index * 48u, at, smooth_, words.x, place, slot);
                    const float mine = dot(light, kLuminance);
                    if (!(mine > 0.0f)) {
                        continue;
                    }
                    const float w = mine * float(lights);  // a light is tried with the chance of one in `lights`
                    sum += w;
                    seed = rt_hash(seed);
                    if (rt_unit(seed) * sum < w) {
                        kept = index;
                        kept_light = light;
                        kept_target = mine;
                        kept_at = place;
                        kept_slot = slot;
                    }
                }
                counted = float(tries);
                // The frame before's reservoirs.
                float3 others[3] = {float3(0.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 0.0f)};
                float others_count[3] = {0.0f, 0.0f, 0.0f};
                if (g_kept.z != 0u && before.z < -1.0f) {
                    uint wide, high;
                    g_reservoirs_before.GetDimensions(wide, high);
                    const float away_before = -before.z;
                    const float2 was = float2((before.x / away_before - g_ray_before.y) / g_ray_before.x,
                                              (before.y / away_before - g_ray_before.w) / g_ray_before.z);
                    const float3 facing_before = float3(dot(g_before[0].xyz, smooth_), dot(g_before[1].xyz, smooth_), dot(g_before[2].xyz, smooth_));
                    for (uint j = 0u; j < 3u; ++j) {
                        float2 from_at = was;
                        if (j != 0u) {
                            seed = rt_hash(seed + 977u);
                            const float turn = float(seed & 0xFFFFu) / 65536.0f * 6.2831853f;
                            from_at += float2(cos(turn), sin(turn)) * (sqrt(float(seed >> 16u) / 65536.0f) * 12.0f);
                        }
                        const int2 from = int2(floor(from_at));
                        if (from.x < 0 || from.y < 0 || from.x >= int(wide) || from.y >= int(high)) {
                            continue;
                        }
                        const float4 there = g_guide_before.Load(int3(from, 0));
                        if (there.a == 0.0f) {
                            continue;
                        }
                        const float3 place_before = rt_eye_ray(g_ray_before, float2(from) + 0.5f) * there.r;
                        if (abs(dot(facing_before, place_before - before)) > 0.02f * away_before + 2.0f ||
                            dot(facing_before, rt_unpack_normal(there.a)) < 0.85f) {
                            continue;
                        }
                        const float4 theirs = g_reservoirs_before.Load(int3(from, 0));
                        const float behind = min(theirs.a, 20.0f);
                        if (!(behind > 0.0f)) {
                            continue;
                        }
                        // Where that surface point is in this frame's eye space (the camera's move undone).
                        const float3 moved = place_before - float3(g_before[0].w, g_before[1].w, g_before[2].w);
                        others[j] = g_before[0].xyz * moved.x + g_before[1].xyz * moved.y + g_before[2].xyz * moved.z;
                        others_count[j] = behind;
                        counted += behind;
                        const uint was_index = uint(theirs.r) + uint(theirs.g) * 1024u;
                        if (was_index == 0u || was_index > g_kept.y || !(theirs.b > 0.0f)) {
                            continue;  // it kept none, or one found in shadow: its candidates count, its light is not handed on
                        }
                        const uint index = g_drops.Load(g_kept.x + (was_index - 1u) * 4u);
                        if (index >= g_glow.w) {
                            continue;  // that light is gone
                        }
                        const float3 light = emitter_light(g_glow.x + index * 48u, at, smooth_, words.x, place, slot);
                        const float mine = dot(light, kLuminance);
                        const float w = mine * theirs.b * behind;
                        if (!(w > 0.0f)) {
                            continue;
                        }
                        sum += w;
                        seed = rt_hash(seed);
                        if (rt_unit(seed) * sum < w) {
                            kept = index;
                            kept_light = light;
                            kept_target = mine;
                            kept_at = place;
                            kept_slot = slot;
                        }
                    }
                }
                float weight = 0.0f;
                if (kept != 0xFFFFFFFFu) {
                    // How many of the candidates could have chosen this light: this frame's, and each reservoir's
                    // whose surface the light reaches.
                    const float reach = asfloat(g_drops.Load(g_glow.x + kept * 48u + 28u));
                    float could = float(tries);
                    for (uint j = 0u; j < 3u; ++j) {
                        const float3 to_it = kept_at.xyz - others[j];
                        if (others_count[j] > 0.0f && dot(to_it, to_it) < reach * reach) {
                            could += others_count[j];
                        }
                    }
                    weight = min(sum / (max(could, 1.0f) * kept_target), 60000.0f);
                    // Its shadow: one ray, stopping short of the source's own body.
                    const float3 to = kept_at.xyz - at;
                    const float far = length(to);
                    const float short_of = far - min(max(kept_at.w, 10.0f), 60.0f);
                    if (short_of > 1.0f && blocked(origin, to / max(far, 1e-3f), short_of, kMaskSolid, false, met) &&
                        g_items.Load(g_at.x + met * kItemBytes) != kept_slot) {
                        weight = 0.0f;
                    }
                    // Far over what a picture shows is cut: the filter's moments stay finite.
                    added = min(kept_light * weight, float3(32.0f, 32.0f, 32.0f));
                }
                g_reservoirs[id.xy] = float4(float((kept + 1u) & 1023u), float((kept + 1u) >> 10u), weight, min(counted, 20.0f));
            }
        }

        // The drop shadows whose reach holds the pixel: a ray back along the drop that meets the drop's own caster
        // first. Not on the caster itself.
        if ((g_more.z & 4u) != 0u && ground && tile.x < g_grid.x && tile.y < g_grid.y) {
            const uint2 run = g_tiles.Load2(g_at.w + (tile.y * g_grid.x + tile.x) * 8u);
            bool dropped = false;
            for (uint e = 0u; e < run.y && !dropped; ++e) {
                const uint drop = g_at.z + g_tiles.Load(g_more.x + (run.x + e) * 4u) * 48u;
                const float4 base = asfloat(g_drops.Load4(drop));
                const float4 line_ = asfloat(g_drops.Load4(drop + 16u));
                const uint caster = g_drops.Load(drop + 32u);
                const float3 from = at - base.xyz;
                const float along = dot(from, line_.xyz);
                const float3 aside = from - along * line_.xyz;
                // The game's volume ends in a shape as wide as its caster: the reach goes that much past the length.
                if (caster == words.x || along < 0.0f || along > line_.w + base.w || dot(aside, aside) > base.w * base.w) {
                    continue;
                }
                const float3 toward = within(-line_.xyz, g_stage.w, light_noise(id.xy, g_more.y, 2u + e));
                if (blocked(origin, toward, along + base.w, kMaskOpaque, false, met) && g_items.Load(g_at.x + met * kItemBytes) == caster) {
                    dropped = true;
                }
            }
            if (dropped) {
                InterlockedAdd(g_counters[19], 1u);
                share.a = min(share.a, 1.0f - g_stage.z);
            }
        }
    }
    if ((g_more.z & 128u) != 0u && item != 0u) {
        share = float4(0.0f, 0.0f, 0.0f, 0.0f);  // the emitters alone
    }
    g_out[id.xy] = share;
    if (!second) {
        g_guide[id.xy] = guide;
    }
    g_added[id.xy] = second ? float4(added, 0.0f) : float4(added.r, 0.0f, added.g, added.b);
    g_mirror[id.xy] = float4(mirror.rgb, mirror.a + (second ? 2.0f : 0.0f));
    if (!second && item == 0u) {
        g_motion[id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
    }
}
