// SPDX-License-Identifier: GPL-3.0-only
// The surface pass: what every later pass of the frame needs to know of a pixel's surface, worked out once from the
// visibility buffer (the prepass's depth and item map) and written to the surface images (rt_common.hlsli): its
// distance and object, its smooth and geometric normals, where its point was in the frame before, how far the smooth
// surface lies over the facets, its material and its colour. Run once for the opaque surfaces and once (g_pass.x 1)
// for the nearest blended surface a pixel, whose place is where the pixel's eye ray meets its triangle.
#include "rt_common.hlsli"

RT_FORMAT(rgba16f) RWTexture2D<float4> g_guide : register(u0);
RT_FORMAT(rgba16f) RWTexture2D<float4> g_normals : register(u1);
RT_FORMAT(rgba16f) RWTexture2D<float4> g_motion : register(u2);
RT_FORMAT(rgba8) RWTexture2D<float4> g_material : register(u3);
RT_FORMAT(rgba8) RWTexture2D<float4> g_base : register(u4);
// The second layer's item at the pixel, from 1 (0: none), as its low ten bits and the rest, each exact in a half: the
// blended draws own this layer's light where it is theirs (program.hlsli traced_mirror_own). Bound on that layer only.
RT_FORMAT(rg16f) RWTexture2D<float2> g_layer_item : register(u5);

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

// Where a pixel's eye ray (`ray`, from the eye at the origin) meets its triangle, and the triangle's normal toward the
// eye: the second layer's surfaces have no depth of their own. False where the item gives no mesh or the ray misses
// the triangle's plane.
bool triangle_place(uint4 first, uint4 second, uint stream, uint triangle_, float3 ray, out float3 at, out float3 normal) {
    at = float3(0.0f, 0.0f, 0.0f);
    normal = float3(0.0f, 0.0f, 1.0f);
    const uint arena = first.z;
    if (arena >= kMeshNone) {
        return false;
    }
    const uint base = mesh_at(arena);
    const uint3 corner = triangle_corners(first, second, triangle_);
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

// The surface as its mesh shades it at the pixel, in the eye's space: its triangle's three vertex normals by where the
// pixel lies among the corners, through the mesh's matrix (`geometric` where the item gives no triangle, a skinned or
// frame-made mesh, or the numbers fail; it faces the way `geometric` does).
// `lifted`: the pixel's place on the smooth surface the triangle stands for, where a ray toward a light leaves from.
// The place set on each corner's tangent plane (never below the triangle), the three by the same weights (Hanika,
// "Hacking the Shadow Terminator", Ray Tracing Gems II 2021): a ray from a rounded mesh's facet near the edge of the
// light then clears the facets beside it, and the edge of the light is the smooth normal's, not the facets'.
// `before`: where the pixel's point was in the frame before's eye space (its corners through the rows of the frame
// before, `row_before`), and `known` whether the mesh gave it. `coordinate`, `vertex_colour`: the vertices' first map
// coordinate and colour at the pixel; `texels_per_pixel` how much of the map (in its own units: multiply by its size)
// a pixel covers there.
float3 shading_normal(uint4 first, uint4 second, uint stream, uint row_before, uint triangle_, float3 at, float3 geometric,
                      out float3 lifted, out float3 before, out bool known, out float2 coordinate, out float4 vertex_colour,
                      out float map_per_pixel) {
    lifted = at;
    before = at;
    known = false;
    coordinate = float2(0.0f, 0.0f);
    vertex_colour = float4(1.0f, 1.0f, 1.0f, 1.0f);
    map_per_pixel = 0.0f;
    const uint arena = first.z;
    if (arena >= kMeshNone) {
        return geometric;
    }
    const uint base = mesh_at(arena);
    const uint3 corner = triangle_corners(first, second, triangle_);
    float3 place[3], normal[3], place_before[3];
    float2 uv[3];
    float4 colour[3];
    for (uint c = 0u; c < 3u; ++c) {
        const uint row = vertex_rows(arena, base, second.w, stream, corner[c]);
        const float4 r0 = asfloat(g_rows.Load4(row)), r1 = asfloat(g_rows.Load4(row + 16u)), r2 = asfloat(g_rows.Load4(row + 32u));
        const float3 n0 = asfloat(g_rows.Load3(row + 48u)), n1 = asfloat(g_rows.Load3(row + 64u)), n2 = asfloat(g_rows.Load3(row + 80u));
        const float4 p = float4(asfloat(arena_words(arena, base + second.z + corner[c] * 12u)), 1.0f);
        place[c] = float3(dot(r0, p), dot(r1, p), dot(r2, p));
        const float3 n = asfloat(arena_words(arena, base + first.w + corner[c] * 24u));
        normal[c] = float3(dot(n0, n), dot(n1, n), dot(n2, n));
        const uint3 rest = arena_words(arena, base + first.w + corner[c] * 24u + 12u);  // colour word, first coordinate
        colour[c] = float4(float(rest.x & 255u), float(rest.x >> 8u & 255u), float(rest.x >> 16u & 255u), float(rest.x >> 24u)) / 255.0f;
        uv[c] = asfloat(rest.yz);
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
    const float total = max(u + v + w, 1e-6f);
    // The same point of the triangle, where its corners were.
    before = (place_before[0] * u + place_before[1] * v + place_before[2] * w) / total;
    known = true;
    coordinate = (uv[0] * u + uv[1] * v + uv[2] * w) / total;
    vertex_colour = (colour[0] * u + colour[1] * v + colour[2] * w) / total;
    // The map's footprint, as a ray's cone (rt_hit_read; Akenine-Moller et al., "Improved Shader and Texture Level of
    // Detail Using Ray Cones", JCGT 2021): the triangle's area on the map over its area in the eye's space, times the
    // pixel's width at the point squared over the facing squared (lambda = 0.5 log2(ta / pa) + log2 w - log2 |n.d|).
    // Its corners' screen places would not do: a corner behind the eye (a large floor's) has none.
    const float2 m1 = uv[1] - uv[0], m2 = uv[2] - uv[0];
    // |e0 x e1| from the cross itself: `scale` is its square only in exact arithmetic (a long sliver's d00 d11 - d01^2
    // cancels to below 0, and its root is NaN).
    const float3 across = cross(e0, e1);
    const float area = length(across);
    if (area > 0.0f) {
        const float facing = max(abs(dot(across, at)) / (area * max(length(at), 1e-6f)), 0.05f);
        const float width = rt_cone_width(at, 0.0f, 0.0f) / facing;
        map_per_pixel = abs(m1.x * m2.y - m1.y * m2.x) / area * width * width;
    }
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

void no_surface(uint2 id, bool second) {
    g_guide[id] = float4(0.0f, 0.0f, 0.0f, 0.0f);
    g_normals[id] = float4(0.0f, 0.0f, 0.0f, 0.0f);
    g_motion[id] = float4(0.0f, 0.0f, 0.0f, 0.0f);
    g_material[id] = float4(0.0f, 0.0f, 0.0f, 0.0f);
    g_base[id] = float4(0.0f, 0.0f, 0.0f, 0.0f);
}

// The item map's triangle where the prepass could not name it (a Vulkan device without geometry shaders).
static const uint kItemNoTriangle = 0xFFFFFFFFu;

// The pixel's triangle of `item` (from 1) by one ray from the eye along its view ray: of that item's triangles the
// ray meets, the one nearest `away` (the depth; an alpha-cut part of the item may stand nearer and let the pixel
// through), or the nearest where `away` < 0 (no depth of the layer's). kItemNoTriangle where it meets none.
uint triangle_by_ray(uint item, float2 centre, float away) {
    RayDesc ray;
    ray.Origin = float3(0.0f, 0.0f, 0.0f);
    ray.Direction = rt_eye_ray(g_ray, centre);  // z -1: the ray's t is the distance along -z, as `away`
    ray.TMin = 0.0f;
    ray.TMax = away > 0.0f ? away * 1.01f + 1.0f : 1.0e30f;
    RayQuery<RAY_FLAG_FORCE_NON_OPAQUE> query;
    query.TraceRayInline(g_scene, RAY_FLAG_FORCE_NON_OPAQUE, 0xFFu, ray);
    uint found = kItemNoTriangle;
    float best = 1.0e30f;
    while (query.Proceed()) {
        if (query.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE && rt_hit_item(query.CandidateInstanceID()) == item - 1u) {
            const float t = query.CandidateTriangleRayT();
            const float off = away > 0.0f ? abs(t - away) : t;
            if (off < best) {
                best = off;
                found = query.CandidatePrimitiveIndex();
            }
        }
    }
    rt_count(17u);
    return found;
}

// The sea's waves under ray tracing, every body of water alike: a height field of kSeaWaves directional waves, a
// wind sea's equilibrium range (elevation spectrum k^-3: each wave the same slope variance, its length a geometric step
// below the last's and jittered inside its step, so no two are commensurate and the field never repeats as a grid),
// spread wide about one wind (a short-crested sea: a far view keeps only the longest waves, and they cross), each its
// own phase, moving by deep water's dispersion (omega^2 = g k, g kSeaGravity units a second squared: longer waves run faster) on the game's time.
// Lengths 920 units (some six Marios) down to 35; the slopes sum to 0.17 root mean square, a light breeze's over open
// water (Cox and Munk 1954: slope variance 0.003 + 0.00512 U, U 5 m/s): the mirror and the bed seen through the water
// bend as a calm sea's do. Table: tools/wave_table.py's output, fixed seed.
// `at`: the point on one of the mesh's three planes, in its units (sea_field), so the waves stand still on the water
// as the eye moves. The slope (dh/da, dh/db). A wave shorter than kWaveFootprints pixels' footprints (`pixel`, units)
// fades out over the next doubling, and the slope it no longer gives goes to `variance` (half its steepness squared a
// sine: the roughness, Toksvig): no shimmer where a pixel holds many waves, and the far sea's mirror blurs as it does
// in life.
static const uint kSeaWaves = 32u;
static const float kSeaGravity = 980.0f;  // a unit a centimetre: Mario stands some 160 units
static const float kWaveFootprints = 3.0f;
static const float4 kSeaWave[kSeaWaves] = {
    // x, y: direction on the water's plane (the wind along x); z: length (units); w: steepness (a k)
    float4(0.7490f, 0.6626f, 919.1f, 0.0425f),
    float4(1.0000f, -0.0089f, 786.3f, 0.0425f),
    float4(0.6915f, 0.7224f, 757.6f, 0.0425f),
    float4(0.9588f, -0.2842f, 658.1f, 0.0425f),
    float4(0.8090f, 0.5879f, 594.0f, 0.0425f),
    float4(0.9923f, -0.1242f, 533.1f, 0.0425f),
    float4(0.7700f, 0.6381f, 475.5f, 0.0425f),
    float4(0.4911f, -0.8711f, 436.4f, 0.0425f),
    float4(0.7772f, 0.6293f, 400.7f, 0.0425f),
    float4(0.7470f, -0.6648f, 366.4f, 0.0425f),
    float4(0.8573f, 0.5148f, 326.7f, 0.0425f),
    float4(0.7934f, -0.6087f, 286.5f, 0.0425f),
    float4(0.3147f, 0.9492f, 265.2f, 0.0425f),
    float4(-0.3443f, -0.9389f, 233.4f, 0.0425f),
    float4(0.9585f, 0.2850f, 204.0f, 0.0425f),
    float4(0.6253f, -0.7804f, 187.9f, 0.0425f),
    float4(0.9971f, 0.0763f, 167.7f, 0.0425f),
    float4(0.9675f, -0.2528f, 158.4f, 0.0425f),
    float4(0.7839f, 0.6209f, 137.6f, 0.0425f),
    float4(0.1999f, -0.9798f, 120.7f, 0.0425f),
    float4(-0.0029f, 1.0000f, 110.8f, 0.0425f),
    float4(0.9069f, -0.4212f, 98.3f, 0.0425f),
    float4(0.9532f, 0.3022f, 90.1f, 0.0425f),
    float4(0.9738f, -0.2272f, 85.3f, 0.0425f),
    float4(0.8827f, 0.4698f, 74.8f, 0.0425f),
    float4(0.9783f, -0.2072f, 69.6f, 0.0425f),
    float4(-0.0751f, 0.9972f, 58.7f, 0.0425f),
    float4(0.9431f, -0.3325f, 54.5f, 0.0425f),
    float4(0.9825f, 0.1860f, 50.7f, 0.0425f),
    float4(0.5406f, -0.8413f, 45.6f, 0.0425f),
    float4(0.9981f, 0.0610f, 39.2f, 0.0425f),
    float4(0.0944f, -0.9955f, 35.5f, 0.0425f),
};
static const float kSeaPhase[kSeaWaves] = {
    2.5417f, 2.0214f, 0.0412f, 6.2296f, 3.2735f, 5.2301f, 2.7313f, 2.9287f,
    3.6394f, 3.0527f, 1.5669f, 2.4679f, 2.8295f, 5.1364f, 1.9897f, 2.4458f,
    5.6042f, 4.6238f, 5.8538f, 0.4385f, 0.1526f, 4.3898f, 4.3694f, 1.9047f,
    6.0301f, 0.0457f, 0.4596f, 3.1574f, 3.5181f, 4.1028f, 1.5276f, 4.1594f,
};
// Value noise on the plane, 0 to 1, smooth (a cell a unit; an integer hash a corner: no sine of large numbers).
float sea_noise(float2 p) {
    const float2 cell = floor(p);
    float2 f = p - cell;
    f = f * f * (3.0f - 2.0f * f);
    const int2 c = int2(cell);
    float corner[4];
    [unroll] for (uint k = 0u; k < 4u; ++k) {
        uint h = uint(c.x + int(k & 1u)) * 0x8DA6B343u ^ uint(c.y + int(k >> 1u)) * 0xD8163841u;
        h ^= h >> 15u;
        h *= 0x2C1B3C6Du;
        h ^= h >> 12u;
        corner[k] = float(h & 0xFFFFu) / 65535.0f;
    }
    return lerp(lerp(corner[0], corner[1], f.x), lerp(corner[2], corner[3], f.x), f.y);
}
// A sea is never the same twice across it: gusts make calm and rough patches (each third of the bands, longest to
// shortest, by its own noise kSeaPatch units across: 0.35 to 1.65 of its height, 1 on the mean, so the sea's slope
// is kept), and the crests wander (the plane's coordinates bent kSeaBend units by a noise kSeaWander across, drifting
// slowly on the game's time): the sum of fixed waves shows no repeat and no ruled line at any distance.
static const float3 kSeaPatch = float3(6000.0f, 2400.0f, 900.0f);
static const float kSeaWander = 3000.0f;
static const float kSeaBend = 260.0f;
float2 sea_waves(float2 at, float pixel, out float variance) {
    variance = 0.0f;
    const float seconds = g_stage.z;  // the game's time: the same waves at every rate
    const float2 drift = seconds * float2(0.004f, 0.0027f);  // the patterns' slow drift, in their cells a second
    const float2 bend = float2(sea_noise(at / kSeaWander + drift), sea_noise(at / kSeaWander + float2(37.2f, 11.9f) - drift)) - 0.5f;
    const float2 place = at + 2.0f * kSeaBend * bend;
    float patch_[3];
    [unroll] for (uint b = 0u; b < 3u; ++b) {
        patch_[b] = 0.35f + 1.3f * sea_noise(at / kSeaPatch[b] + float2(17.0f * float(b), 5.0f * float(b)) + drift);
    }
    float2 slope = float2(0.0f, 0.0f);
    [unroll] for (uint i = 0u; i < kSeaWaves; ++i) {
        const float4 wave = kSeaWave[i];
        const float k = 6.2831853f / wave.z;
        const float omega = sqrt(kSeaGravity * k);
        const float kept = saturate(wave.z / (kWaveFootprints * max(pixel, 1e-3f)) - 1.0f);
        const float steep = wave.w * patch_[min(i * 3u / kSeaWaves, 2u)];
        if (kept > 0.0f) {
            const float phase = k * dot(wave.xy, place) - omega * seconds + kSeaPhase[i];
            slope += kept * steep * cos(phase) * wave.xy;
        }
        variance += (1.0f - kept * kept) * 0.5f * steep * steep;
    }
    return slope;
}
// The waves on any body of water, in its mesh's space, with no seam: the field on each of the mesh's three planes
// (x = 0, y = 0, z = 0; the level sea is the y plane's, the wind along x), each a slope on its plane, blended by how
// the surface faces them (the normal's components to the fourth power: triplanar, so a sphere, a ring or a wall of
// water takes waves all round and a level sea one plane's alone). `at` the point, `n` the smooth normal, both in the
// mesh's space; `pixel` the footprint in its units. The gradient (dh/dx, dh/dy, dh/dz); `variance` the slope the
// filter took away (sea_waves).
float3 sea_field(float3 at, float3 n, float pixel, out float variance) {
    float3 w = n * n;
    w *= w;
    w = max(w / (w.x + w.y + w.z) - 0.02f, 0.0f);
    w /= w.x + w.y + w.z;
    float3 gradient = float3(0.0f, 0.0f, 0.0f);
    variance = 0.0f;
    float v;
    if (w.y > 0.0f) {
        const float2 s = sea_waves(at.xz, pixel, v);
        gradient += w.y * float3(s.x, 0.0f, s.y);
        variance += w.y * v;
    }
    if (w.x > 0.0f) {
        const float2 s = sea_waves(at.zy + float2(3719.0f, 1931.0f), pixel, v);  // offset: the planes' waves unalike
        gradient += w.x * float3(0.0f, s.y, s.x);
        variance += w.x * v;
    }
    if (w.z > 0.0f) {
        const float2 s = sea_waves(at.xy + float2(-2659.0f, 4421.0f), pixel, v);
        gradient += w.z * float3(s.x, s.y, 0.0f);
        variance += w.z * v;
    }
    return gradient;
}
// A water surface's normal at the pixel under ray tracing: bent by the sea's waves alone (sea_field, in the mesh's
// space: a code-drawn sea as model water). The game's moving ripple maps are its colour, not its shape: their tiles,
// added to the waves, repeated as a grid across a whole sea. `place`: the pixel's point on the triangle (eye space).
float3 water_ripple(uint4 first, uint4 second, uint stream, uint triangle_, float3 place, float3 smooth_,
                    out float wave_variance) {
    wave_variance = 0.0f;
    const uint arena = first.z;
    if (arena >= kMeshNone) {
        return smooth_;
    }
    const uint base = mesh_at(arena);
    const uint3 corner = triangle_corners(first, second, triangle_);
    float3 p[3], q[3];
    float4 r0 = float4(0.0f, 0.0f, 0.0f, 0.0f), r1 = r0, r2 = r0;  // the mesh's matrix (the last corner's rows)
    for (uint c = 0u; c < 3u; ++c) {
        const uint row = vertex_rows(arena, base, second.w, stream, corner[c]);
        r0 = asfloat(g_rows.Load4(row));
        r1 = asfloat(g_rows.Load4(row + 16u));
        r2 = asfloat(g_rows.Load4(row + 32u));
        q[c] = asfloat(arena_words(arena, base + second.z + corner[c] * 12u));
        const float4 h = float4(q[c], 1.0f);
        p[c] = float3(dot(r0, h), dot(r1, h), dot(r2, h));
    }
    // The point among the corners.
    const float3 e1 = p[1] - p[0], e2 = p[2] - p[0], e = place - p[0];
    const float d11 = dot(e1, e1), d12 = dot(e1, e2), d22 = dot(e2, e2), de1 = dot(e, e1), de2 = dot(e, e2);
    const float area = d11 * d22 - d12 * d12;
    if (!(abs(area) > 1e-12f)) {
        return smooth_;
    }
    const float v = (d22 * de1 - d12 * de2) / area, w = (d11 * de2 - d12 * de1) / area;
    const float3 at_mesh = q[0] + (q[1] - q[0]) * v + (q[2] - q[0]) * w;
    // The mesh's scale (its matrix's columns' mean length) and the smooth normal in its space (the transpose: a
    // rotation and a scale), so the field stands on the mesh and the normal is as continuous as the eye's.
    const float scale = (length(float3(r0.x, r1.x, r2.x)) + length(float3(r0.y, r1.y, r2.y)) + length(float3(r0.z, r1.z, r2.z))) / 3.0f;
    const float3 n_mesh = r0.xyz * smooth_.x + r1.xyz * smooth_.y + r2.xyz * smooth_.z;
    if (!(scale > 1e-9f) || !(dot(n_mesh, n_mesh) > 1e-18f)) {
        return smooth_;
    }
    // The pixel's footprint on the surface, in the mesh's units.
    const float pixel = -place.z * abs(g_ray.x) / max(abs(dot(smooth_, normalize(place))), 0.05f) / scale;
    const float3 g = sea_field(at_mesh, normalize(n_mesh), pixel, wave_variance);
    // The gradient in the eye's space (slopes keep under a scale), along the surface; the normal tilted against it.
    float3 tilt = float3(dot(r0.xyz, g), dot(r1.xyz, g), dot(r2.xyz, g)) / scale;
    tilt -= smooth_ * dot(tilt, smooth_);
    const float3 bent = normalize(smooth_ - tilt);
    return dot(bent, smooth_) > 0.5f ? bent : smooth_;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint width, height;
    g_guide.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) {
        return;
    }
    const int2 pixel = int2(id.xy);
    const float2 centre = float2(id.xy) + 0.5f;
    const uint2 named = g_item_map.Load(int3(pixel, 0));
    const uint item = named.x;
    const bool second = rt_second_layer();
    if (second) {
        g_layer_item[id.xy] = float2(float(item & 1023u), float(item >> 10u));
    }
    if (second && item == 0u) {
        g_guide[id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);  // no blended surface here: its filters leave the pixel
        return;  // and the picture keeps the opaque layer's light
    }
    const float depth = g_depth_map.Load(int3(pixel, 0));
    if (item == 0u || (!second && rt_far(g_depth, depth)) || !in_viewport(centre)) {
        no_surface(id.xy, second);
        return;
    }
    const uint at_item = g_at.x + (item - 1u) * kItemBytes;
    const uint4 words4 = g_items.Load4(at_item);
    const uint4 mesh = g_items.Load4(at_item + 16u);
    const uint4 third = g_items.Load4(at_item + 32u);  // first map, alpha from, matrix stream, look
    const uint4 fourth = g_items.Load4(at_item + 48u);  // colour and painted share, row before
    if ((words4.y & kItemSurface) == 0u) {
        no_surface(id.xy, second);  // in the prepass for its depth only (the sky): no light of the scene's falls on it
        return;
    }
    float away = -rt_eye_z(g_depth, depth);
    float3 at = rt_eye_ray(g_ray, centre) * away;
    float3 normal;
    // The pixel's triangle: the prepass's, or where it could not name one the ray's. The first layer with none
    // found keeps the depth's own normal (no mesh: kMeshNone).
    uint triangle_ = named.y;
    if (triangle_ == kItemNoTriangle) {
        triangle_ = triangle_by_ray(item, centre, second ? -1.0f : away);
    }
    const uint4 shaded = triangle_ == kItemNoTriangle ? uint4(words4.xy, kMeshNone, words4.w) : words4;
    if (second) {
        if (!triangle_place(shaded, mesh, third.z, triangle_, rt_eye_ray(g_ray, centre), at, normal)) {
            no_surface(id.xy, second);
            return;
        }
        away = -at.z;
    } else {
        // The facet's own normal where the triangle is named and its plane passes through the pixel's place: the
        // depth's is quantized, takes a neighbour facet across an edge and moves with the jitter, and a ray's origin
        // and the terminator's lift go along it (the shadow terminator's sawtooth and its shimmer).
        float3 on_facet, facet;
        if (triangle_ != kItemNoTriangle && triangle_place(shaded, mesh, third.z, triangle_, rt_eye_ray(g_ray, centre), on_facet, facet) &&
            abs(-on_facet.z - away) < 0.02f * away + 1.0f) {
            normal = facet;
        } else {
            normal = surface_normal(pixel, at);
        }
    }
    float3 lifted, before;
    bool before_known;
    float2 coordinate;
    float4 vertex_colour;
    float map_per_pixel;
    const float3 smooth_ = shading_normal(shaded, mesh, third.z, fourth.y, triangle_, at, normal, lifted, before, before_known,
                                          coordinate, vertex_colour, map_per_pixel);
    {
        // Where the point was the frame before (both layers: each follows its own surfaces).
        if (!before_known) {
            const float4 here = float4(at, 1.0f);
            before = float3(dot(g_before[0], here), dot(g_before[1], here), dot(g_before[2], here));
        }
        float2 move = float2(0.0f, 0.0f);
        float away_before = 0.0f;
        if (before.z < -1.0f) {
            away_before = -before.z;
            const float2 was = float2((before.x / away_before - g_ray_before.y) / g_ray_before.x, (before.y / away_before - g_ray_before.w) / g_ray_before.z);
            move = was - centre;
        }
        g_motion[id.xy] = float4(move, away_before, dot(lifted - at, normal));  // xy, z: both layers
    }
    g_guide[id.xy] = float4(away, float(words4.x & 1023u), float(words4.x >> 10u), rt_pack_normal(smooth_));
    // Water shades by its rippled normal; the guide keeps the smooth one for the filters. `shaded` and the
    // resolved `triangle_`: with no triangle found its slot is kMeshNone and the ripple stays out.
    float wave_variance = 0.0f;  // the slope variance the waves' filter took away
    const float3 shading = (third.w & 255u) == kClassWater
                               ? water_ripple(shaded, mesh, third.z, triangle_, at, smooth_, wave_variance)
                               : smooth_;
    g_normals[id.xy] = float4(rt_oct(shading), rt_oct(normal));
    // The material: the hit record's numbers as the item table carries them.
    const uint look = third.w;
    const uint class_ = look & 255u, hit_flags = look >> 8u & 255u;
    const uint flags = ((words4.y & kItemLit) != 0u ? kSurfaceLit : 0u) | (class_ == kClassEmissive ? kSurfaceEmissive : 0u) |
                       ((hit_flags & 16u) != 0u ? kSurfaceEnvironment : 0u) | ((words4.y & kItemThin) != 0u ? kSurfaceTwoSided : 0u);
    // Water's roughness is its waves' (Toksvig): alpha^2 the slope variance the level's filter took away,
    // written as sqrt(alpha) as every pass squares it; 0 where nothing was lost (a mirror, as before).
    const float rough_byte = class_ == kClassWater ? saturate(sqrt(sqrt(saturate(wave_variance)))) * 255.0f
                                                   : float(look >> 16u & 255u);
    g_material[id.xy] = float4(rough_byte, float(look >> 24u), float((class_ & 15u) | flags), float(fourth.x >> 24u)) / 255.0f;
    // Its colour: the record's under its first map at the pixel (the map's level by the pixel's footprint on it), by
    // the vertex's colour where the material takes it.
    float4 base = float4(float(fourth.x & 255u), float(fourth.x >> 8u & 255u), float(fourth.x >> 16u & 255u), 255.0f) / 255.0f;
    if (third.x != 0xFFFFFFFFu) {
        uint map_width, map_height;
        g_textures[NonUniformResourceIndex(third.x)].GetDimensions(map_width, map_height);
        const float level = 0.5f * log2(max(map_per_pixel * float(map_width) * float(map_height), 1.0f));
        float4 m = g_textures[NonUniformResourceIndex(third.x)].SampleLevel(g_wrap, coordinate, level);
        // Its colour from the map whose colour it shows (item word 20; rt_shade.hlsli rt_hit_read), its alpha from
        // the first.
        const uint colour_map = g_items.Load(at_item + 80u);
        if (colour_map != third.x && colour_map != 0xFFFFFFFFu) {
            g_textures[NonUniformResourceIndex(colour_map)].GetDimensions(map_width, map_height);
            m.rgb = g_textures[NonUniformResourceIndex(colour_map)]
                        .SampleLevel(g_wrap, coordinate, 0.5f * log2(max(map_per_pixel * float(map_width) * float(map_height), 1.0f)))
                        .rgb;
        }
        base *= m;
    }
    // The vertex colour as the hit record reads it (kVertexGrey: the cooker read it mid grey; rt_hit_read the same); its
    // alpha as it is.
    if ((hit_flags & 8u) != 0u) {
        base *= float4(vertex_colour.rgb * kVertexGrey, vertex_colour.a);
    }
    g_base[id.xy] = saturate(base);
}
