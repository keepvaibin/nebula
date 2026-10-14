// SPDX-License-Identifier: GPL-3.0-only
// Ray-traced scene (DXR 1.1 inline ray queries, cs_6_5, compiled by DXC at build time).
//
// The acceleration structure holds the game frame's opaque 3D draws in eye space: the same
// decoded vertices, index lists, position matrices and palettes the raster used, so traced
// hits land on the rasterised surfaces. Every buffer read is clamped to its buffer: root views
// carry no bounds and some compilers evaluate both sides of a select.

cbuffer RtFrame : register(b0)
{
    uint g_draw_count;
    uint g_triangle_count;
    uint g_vertex_ring_limit;   // bytes - 144
    uint g_resident_limit;      // bytes - 144
    uint g_index_ring_limit;    // bytes - 4
    uint g_constant_ring_limit; // bytes - 1280
    uint g_matrix_ring_limit;   // bytes - 48
    uint g_frame_flags;
    float4 g_projection;        // GX projection A, B, C, D
    float4 g_depth;             // D3D projection [2][2], [2][3]; viewport min depth, max depth
    float4 g_viewport;          // left, top, width, height (EFB pixels)
    uint2 g_target_size;        // EFB pixels
    float g_near;               // near-plane distance (eye units)
    uint g_triangle_base;       // first triangle of this geometry dispatch
};

// RtDraw, 32 bytes: vertex byte offset; flags (bit 0 resident source, bit 1 indexed, bit 2
// 32-bit indices, bit 3 lit, bits 8-11 surface class); vertex count; index byte offset; first
// triangle; triangle count; VS constants byte offset; palette byte offset.
ByteAddressBuffer g_draws : register(t0);
ByteAddressBuffer g_vertex_ring : register(t1);
ByteAddressBuffer g_resident : register(t2);
ByteAddressBuffer g_index_ring : register(t3);
ByteAddressBuffer g_constant_ring : register(t4);
ByteAddressBuffer g_matrix_ring : register(t5);
RWByteAddressBuffer g_positions : register(u0);
// Per triangle, 16 bytes: its vertices' eye-space normals (octahedral 16:16, 0 without one) and
// the draw's flags (RtDraw.flags).
RWByteAddressBuffer g_surfaces : register(u3);

RaytracingAccelerationStructure g_scene : register(t6);
Texture2D<float> g_efb_depth : register(t7);
RWByteAddressBuffer g_stats : register(u1);
// Diagnostics (g_frame_flags bit 0): per pixel class, raster eye z, traced eye z, depth.
RWStructuredBuffer<float4> g_dump : register(u2);

static const uint kVertexStride = 144u;
static const uint kVertexMatrixIndex = 120u;
static const uint kVertexNormal = 12u;
static const uint kVsFlags = 200u;
static const uint kVsPaletteBase = 204u;
static const uint kVsInlinePosMatrix = 224u;

uint load_index(uint index_offset, uint i, bool wide)
{
    if (wide) {
        return g_index_ring.Load(min(index_offset + i * 4u, g_index_ring_limit));
    }
    const uint address = index_offset + i * 2u;
    const uint word = g_index_ring.Load(min(address & ~3u, g_index_ring_limit));
    return (word >> ((address & 2u) * 8u)) & 0xFFFFu;
}

// Octahedral, 1-65535 per axis: 0 is no normal.
uint pack_vertex_normal(float3 n)
{
    const float len = abs(n.x) + abs(n.y) + abs(n.z);
    if (!(len > 1e-12f)) {
        return 0u;
    }
    n /= len;
    float2 e = n.xy;
    if (n.z < 0.0f) {
        e = (1.0f - abs(n.yx)) * float2(n.x >= 0.0f ? 1.0f : -1.0f, n.y >= 0.0f ? 1.0f : -1.0f);
    }
    const uint2 u = uint2(round(saturate(e * 0.5f + 0.5f) * 65534.0f)) + 1u;
    return u.x | (u.y << 16u);
}

// A vertex in eye space, and its normal by the inverse transpose of the same matrix (rows of
// cofactors; the determinant's sign keeps mirrored matrices facing out).
float3 transform_vertex(uint4 draw, uint4 draw2, uint vertex, uint vs_flags, uint palette_base, out float3 normal)
{
    const uint address = draw.x + min(vertex, max(draw.z, 1u) - 1u) * kVertexStride;
    float3 position;
    uint matrix_index;
    [branch] if ((draw.y & 1u) != 0u) {
        const uint a = min(address, g_resident_limit);
        position = asfloat(g_resident.Load3(a));
        normal = asfloat(g_resident.Load3(a + kVertexNormal));
        matrix_index = g_resident.Load(a + kVertexMatrixIndex);
    } else {
        const uint a = min(address, g_vertex_ring_limit);
        position = asfloat(g_vertex_ring.Load3(a));
        normal = asfloat(g_vertex_ring.Load3(a + kVertexNormal));
        matrix_index = g_vertex_ring.Load(a + kVertexMatrixIndex);
    }
    float4 r0, r1, r2;
    [branch] if ((vs_flags & 1u) != 0u) {
        // Per-vertex position matrix: XF row index (PNMTX n at row 3n), clamped as in the VS.
        const uint row = min(matrix_index & 0x3Fu, 61u);
        const uint m = min(draw2.w + (palette_base + row) * 16u, g_matrix_ring_limit);
        r0 = asfloat(g_matrix_ring.Load4(m));
        r1 = asfloat(g_matrix_ring.Load4(m + 16u));
        r2 = asfloat(g_matrix_ring.Load4(m + 32u));
    } else {
        const uint c = min(draw2.z, g_constant_ring_limit) + kVsInlinePosMatrix;
        r0 = asfloat(g_constant_ring.Load4(c));
        r1 = asfloat(g_constant_ring.Load4(c + 16u));
        r2 = asfloat(g_constant_ring.Load4(c + 32u));
    }
    const float3 c0 = cross(r1.xyz, r2.xyz);
    const float3 c1 = cross(r2.xyz, r0.xyz);
    const float3 c2 = cross(r0.xyz, r1.xyz);
    normal = float3(dot(c0, normal), dot(c1, normal), dot(c2, normal)) * (dot(r0.xyz, c0) < 0.0f ? -1.0f : 1.0f);
    const float4 p = float4(position, 1.0f);
    return float3(dot(r0, p), dot(r1, p), dot(r2, p));
}

// One thread per output triangle: three eye-space vertices (36 bytes) in draw order and its
// surface record (16 bytes). A dispatch
// covers the draws noted since the last one (g_triangle_count triangles from g_triangle_base).
[numthreads(64, 1, 1)]
void GeometryCS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= g_triangle_count || g_draw_count == 0u) {
        return;
    }
    const uint tri = g_triangle_base + id.x;
    // The last draw whose first triangle is not after this one.
    uint lo = 0u;
    uint hi = g_draw_count - 1u;
    while (lo < hi) {
        const uint mid = (lo + hi + 1u) >> 1u;
        if (g_draws.Load(mid * 32u + 16u) <= tri) {
            lo = mid;
        } else {
            hi = mid - 1u;
        }
    }
    const uint4 draw = g_draws.Load4(lo * 32u);
    const uint4 draw2 = g_draws.Load4(lo * 32u + 16u);
    const uint local = tri - draw2.x;
    const uint constants = min(draw2.z, g_constant_ring_limit);
    const uint vs_flags = g_constant_ring.Load(constants + kVsFlags);
    const uint palette_base = g_constant_ring.Load(constants + kVsPaletteBase);
    const bool indexed = (draw.y & 2u) != 0u;
    const bool wide = (draw.y & 4u) != 0u;
    float3 corners[3];
    uint normals[3];
    [unroll] for (uint k = 0u; k < 3u; ++k) {
        const uint i = local * 3u + k;
        const uint vertex = indexed ? load_index(draw.w, i, wide) : i;
        float3 normal;
        corners[k] = transform_vertex(draw, draw2, vertex, vs_flags, palette_base, normal);
        normals[k] = pack_vertex_normal(normal);
    }
    const uint out_address = tri * 36u;
    g_positions.Store3(out_address, asuint(corners[0]));
    g_positions.Store3(out_address + 12u, asuint(corners[1]));
    g_positions.Store3(out_address + 24u, asuint(corners[2]));
    g_surfaces.Store4(tri * 16u, uint4(normals[0], normals[1], normals[2], draw.y));
}

// Eye-space direction through an EFB pixel centre (GX perspective: clip x = A x + B z,
// clip y = C y + D z, clip w = -z).
float3 pixel_direction(float2 pixel)
{
    const float ndc_x = (pixel.x - g_viewport.x) / g_viewport.z * 2.0f - 1.0f;
    const float ndc_y = 1.0f - (pixel.y - g_viewport.y) / g_viewport.w * 2.0f;
    return normalize(float3((ndc_x + g_projection.y) / g_projection.x,
                            (ndc_y + g_projection.w) / g_projection.z, -1.0f));
}

// Eye-space z (negative) of a reversed-Z depth sample: ndc = (d - min) / (max - min) and
// D3D clip z / w = ([2][2] z + [2][3]) / -z.
float depth_to_eye_z(float depth)
{
    const float ndc = (depth - g_depth.z) / max(g_depth.w - g_depth.z, 1e-7f);
    return -g_depth.y / (ndc + g_depth.x);
}

// Stats words: 0 pixels, 1 raster background, 2 raster geometry, 3 hit within 0.1 %,
// 4 hit within 1 %, 5 hit nearer, 6 hit farther, 7 miss on raster geometry,
// 8 hit on raster background.
void count(uint slot, bool condition)
{
    const uint n = WaveActiveCountBits(condition);
    if (WaveIsFirstLane() && n != 0u) {
        g_stats.InterlockedAdd(slot * 4u, n);
    }
}

[numthreads(8, 8, 1)]
void ValidateCS(uint3 id : SV_DispatchThreadID)
{
    // Pixels of the scene viewport only: outside it the EFB keeps its clear depth.
    const bool inside = id.x < g_target_size.x && id.y < g_target_size.y &&
                        float(id.x) + 0.5f >= g_viewport.x && float(id.x) + 0.5f < g_viewport.x + g_viewport.z &&
                        float(id.y) + 0.5f >= g_viewport.y && float(id.y) + 0.5f < g_viewport.y + g_viewport.w;
    const uint2 texel = min(id.xy, max(g_target_size, 1u) - 1u);
    const float depth = g_efb_depth.Load(int3(texel, 0));
    const bool background = depth <= g_depth.z + 1e-6f;
    const float3 direction = pixel_direction(float2(texel) + 0.5f);

    RayDesc ray;
    ray.Origin = float3(0.0f, 0.0f, 0.0f);
    ray.Direction = direction;
    ray.TMin = g_near / max(-direction.z, 1e-6f);
    ray.TMax = 1e30f;
    RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
    query.TraceRayInline(g_scene, RAY_FLAG_NONE, 0xFFu, ray);
    query.Proceed();
    const bool hit = query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    const float hit_z = direction.z * (hit ? query.CommittedRayT() : 0.0f);
    const float raster_z = depth_to_eye_z(depth);
    const float error = abs(hit_z - raster_z) / max(abs(raster_z), 1e-6f);

    count(0u, inside);
    count(1u, inside && background);
    count(2u, inside && !background);
    count(3u, inside && !background && hit && error <= 1e-3f);
    count(4u, inside && !background && hit && error > 1e-3f && error <= 1e-2f);
    count(5u, inside && !background && hit && error > 1e-2f && hit_z > raster_z);
    count(6u, inside && !background && hit && error > 1e-2f && hit_z < raster_z);
    count(7u, inside && !background && !hit);
    count(8u, inside && background && hit);

    if ((g_frame_flags & 1u) != 0u && id.x < g_target_size.x && id.y < g_target_size.y) {
        // 0 background, 1 background hit, 2 within 0.1 %, 3 within 1 %, 4 nearer, 5 farther, 6 miss,
        // 7 outside the viewport; w: the hit triangle.
        const float kind = !inside ? 7.0f
                         : background ? (hit ? 1.0f : 0.0f)
                         : !hit ? 6.0f
                         : error <= 1e-3f ? 2.0f
                         : error <= 1e-2f ? 3.0f
                         : hit_z > raster_z ? 4.0f : 5.0f;
        g_dump[id.y * g_target_size.x + id.x] =
            float4(kind, raster_z, hit_z, asfloat(hit ? query.CommittedPrimitiveIndex() : 0xFFFFFFFFu));
    }
}
