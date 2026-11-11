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
    if (arena >= 8u) {
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

// The pixel's place on screen of an eye-space point (rt_eye_ray's inverse).
float2 pixel_of(float3 p) {
    const float away = max(-p.z, 1e-3f);
    return float2((p.x / away - g_ray.y) / g_ray.x, (p.y / away - g_ray.w) / g_ray.z);
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
    if (arena >= 8u) {
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
    // The map's footprint: the triangle's area on the map over its area on screen.
    const float2 s0 = pixel_of(place[0]), s1 = pixel_of(place[1]), s2 = pixel_of(place[2]);
    const float2 m1 = uv[1] - uv[0], m2 = uv[2] - uv[0], p1 = s1 - s0, p2 = s2 - s0;
    map_per_pixel = abs(m1.x * m2.y - m1.y * m2.x) / max(abs(p1.x * p2.y - p1.y * p2.x), 1e-6f);
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

// The item map's triangle where the prepass could not name it (a Vulkan device without geometry shaders, D11).
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
        if (query.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE && query.CandidateInstanceID() == item - 1u) {
            const float t = query.CandidateTriangleRayT();
            const float off = away > 0.0f ? abs(t - away) : t;
            if (off < best) {
                best = off;
                found = query.CandidatePrimitiveIndex();
            }
        }
    }
    InterlockedAdd(g_counters[17], 1u);
    return found;
}

// A water surface's normal bent by its own moving map (plan 13.3; l54): item word 18 names the two rows of its first
// map's coordinate (0: none); the luminance's change a texel along u' and v', at the level the pixel covers, tilts the
// smooth normal against the rise, kWave of it. `place`: the pixel's point on the triangle (eye space).
static const float kWave = 0.15f;
float3 water_ripple(uint item_at, uint4 first, uint4 second, uint stream, uint map, uint triangle_, float3 place, float3 smooth_) {
    const uint rows_at = g_items.Load(item_at + 72u);
    const uint arena = first.z;
    if (rows_at == 0u || arena >= 8u) {
        return smooth_;
    }
    const float4 ra = asfloat(g_items.Load4(g_at.x + rows_at)), rb = asfloat(g_items.Load4(g_at.x + rows_at + 16u));
    const uint base = mesh_at(arena);
    const uint3 corner = triangle_corners(first, second, triangle_);
    float3 p[3];
    float2 t[3];
    for (uint c = 0u; c < 3u; ++c) {
        const uint row = vertex_rows(arena, base, second.w, stream, corner[c]);
        const float4 r0 = asfloat(g_rows.Load4(row)), r1 = asfloat(g_rows.Load4(row + 16u)), r2 = asfloat(g_rows.Load4(row + 32u));
        const float4 q = float4(asfloat(arena_words(arena, base + second.z + corner[c] * 12u)), 1.0f);
        p[c] = float3(dot(r0, q), dot(r1, q), dot(r2, q));
        const float4 uv = float4(asfloat(arena_words(arena, base + first.w + corner[c] * 24u + 12u).yz), 1.0f, 1.0f);
        t[c] = float2(dot(uv, ra), dot(uv, rb));
    }
    // The point among the corners, and the triangle's directions of u' and v' (the change of place a unit of each).
    const float3 e1 = p[1] - p[0], e2 = p[2] - p[0], e = place - p[0];
    const float d11 = dot(e1, e1), d12 = dot(e1, e2), d22 = dot(e2, e2), de1 = dot(e, e1), de2 = dot(e, e2);
    const float area = d11 * d22 - d12 * d12;
    const float2 m1 = t[1] - t[0], m2 = t[2] - t[0];
    const float turn = m1.x * m2.y - m1.y * m2.x;
    if (!(abs(area) > 1e-12f) || !(abs(turn) > 1e-12f)) {
        return smooth_;
    }
    const float v = (d22 * de1 - d12 * de2) / area, w = (d11 * de2 - d12 * de1) / area;
    const float2 here = t[0] + m1 * v + m2 * w;
    const float3 along_u = (e1 * m2.y - e2 * m1.y) / turn, along_v = (e2 * m1.x - e1 * m2.x) / turn;
    // The level the pixel covers: a pixel's width there (stretched along the surface as it turns from the eye) over
    // the place a unit of u' spans, in texels.
    uint map_w, map_h;
    g_textures[NonUniformResourceIndex(map)].GetDimensions(map_w, map_h);
    const float pixel = -place.z * abs(g_ray.x) / max(abs(dot(smooth_, normalize(place))), 0.05f);
    const float texels = pixel * float(max(map_w, map_h)) / max(min(length(along_u), length(along_v)), 1e-6f);
    const float level = max(log2(max(texels, 1.0f)), 0.0f);
    const float2 step_ = exp2(level) / float2(max(map_w, 1u), max(map_h, 1u));
    const float3 luma = float3(0.2126f, 0.7152f, 0.0722f);
    const float l0 = dot(g_textures[NonUniformResourceIndex(map)].SampleLevel(g_wrap, here, level).rgb, luma);
    const float lu = dot(g_textures[NonUniformResourceIndex(map)].SampleLevel(g_wrap, here + float2(step_.x, 0.0f), level).rgb, luma);
    const float lv = dot(g_textures[NonUniformResourceIndex(map)].SampleLevel(g_wrap, here + float2(0.0f, step_.y), level).rgb, luma);
    // The directions in the surface's plane (unit), the normal tilted against the rise.
    const float3 tu = along_u - smooth_ * dot(along_u, smooth_), tv = along_v - smooth_ * dot(along_v, smooth_);
    const float su = dot(tu, tu), sv = dot(tv, tv);
    if (!(su > 1e-12f) || !(sv > 1e-12f)) {
        return smooth_;
    }
    const float3 bent = normalize(smooth_ - kWave * ((lu - l0) * tu * rsqrt(su) + (lv - l0) * tv * rsqrt(sv)));
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
    // The pixel's triangle: the prepass's, or where it could not name one the ray's (D11). The first layer with none
    // found keeps the depth's own normal (no mesh: arena 8).
    uint triangle_ = named.y;
    if (triangle_ == kItemNoTriangle) {
        triangle_ = triangle_by_ray(item, centre, second ? -1.0f : away);
    }
    const uint4 shaded = triangle_ == kItemNoTriangle ? uint4(words4.xy, 8u, words4.w) : words4;
    if (second) {
        if (!triangle_place(shaded, mesh, third.z, triangle_, rt_eye_ray(g_ray, centre), at, normal)) {
            no_surface(id.xy, second);
            return;
        }
        away = -at.z;
    } else {
        normal = surface_normal(pixel, at);
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
    // Water shades by its rippled normal (l54); the guide keeps the smooth one for the filters. `shaded` and the
    // resolved `triangle_`: with no triangle found its arena is 8 and the ripple stays out.
    const float3 shading = (third.w & 255u) == kClassWater && third.x != 0xFFFFFFFFu
                               ? water_ripple(at_item, shaded, mesh, third.z, third.x, triangle_, at, smooth_)
                               : smooth_;
    g_normals[id.xy] = float4(rt_oct(shading), rt_oct(normal));
    // The material: the hit record's numbers as the item table carries them.
    const uint look = third.w;
    const uint class_ = look & 255u, hit_flags = look >> 8u & 255u;
    const uint flags = ((words4.y & kItemLit) != 0u ? kSurfaceLit : 0u) | (class_ == kClassEmissive ? kSurfaceEmissive : 0u) |
                       ((hit_flags & 16u) != 0u ? kSurfaceEnvironment : 0u) | ((words4.y & kItemThin) != 0u ? kSurfaceTwoSided : 0u);
    g_material[id.xy] = float4(float(look >> 16u & 255u), float(look >> 24u), float((class_ & 15u) | flags), float(fourth.x >> 24u)) / 255.0f;
    // Its colour: the record's under its first map at the pixel (the map's level by the pixel's footprint on it), by
    // the vertex's colour where the material takes it.
    float4 base = float4(float(fourth.x & 255u), float(fourth.x >> 8u & 255u), float(fourth.x >> 16u & 255u), 255.0f) / 255.0f;
    if (third.x != 0xFFFFFFFFu) {
        uint map_width, map_height;
        g_textures[NonUniformResourceIndex(third.x)].GetDimensions(map_width, map_height);
        const float level = 0.5f * log2(max(map_per_pixel * float(map_width) * float(map_height), 1.0f));
        base *= g_textures[NonUniformResourceIndex(third.x)].SampleLevel(g_wrap, coordinate, level);
    }
    if ((hit_flags & 8u) != 0u) {
        base *= vertex_colour;
    }
    g_base[id.xy] = saturate(base);
}
