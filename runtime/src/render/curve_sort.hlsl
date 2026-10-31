// Curved surfaces' triangles sorted before their draws (compute, entries classify, offsets, scatter): those the hull
// stage would leave whole to a triangle list, the rest to patches (RendererD3D12::sort_curved). The VERTEX_* and CB_*
// offsets (RenderVertex's, VertexConstants') come before it (curve_sort_source).

float3 curve_unpack(uint packed) {
    if (packed == 0u) {
        return float3(0.0, 0.0, 0.0);
    }
    const float2 p = (float2(float(packed >> 16u), float(packed & 0xFFFFu)) - 32768.0) / 32767.0;
    float3 n = float3(p, 1.0 - abs(p.x) - abs(p.y));
    if (n.z < 0.0) {
        n.xy = (1.0 - abs(p.yx)) * float2(p.x >= 0.0 ? 1.0 : -1.0, p.y >= 0.0 ? 1.0 : -1.0);
    }
    return normalize(n);
}
uint curve_pack(float3 n) {
    const float sum = abs(n.x) + abs(n.y) + abs(n.z);
    if (!(sum > 0.0)) {
        return 0u;
    }
    float2 p = n.xy / sum;
    if (n.z < 0.0) {
        p = (1.0 - abs(p.yx)) * float2(p.x >= 0.0 ? 1.0 : -1.0, p.y >= 0.0 ? 1.0 : -1.0);
    }
    const uint2 h = uint2(round(clamp(p, -1.0, 1.0) * 32767.0) + 32768.0);
    return (h.x << 16u) | h.y;
}

float curve_value(float4 a, float4 b, float3 qa, float3 qb, float3 na, float3 nb, float2 half_viewport) {
    const float3 edge = qb - qa;
    const float edge_length = length(edge);
    if (a.w <= 0.0 || b.w <= 0.0 || !(edge_length > 0.0)) {
        return 0.0;
    }
    const float3 along = edge / edge_length;
    const float tilt = max(abs(dot(along, na)), abs(dot(along, nb)));
    if (!(tilt > 0.0)) {
        return 0.0;
    }
    const float angle = 2.0 * asin(min(tilt, 1.0));
    const float chord = length((a.xy / a.w - b.xy / b.w) * half_viewport);
    return chord * angle * 0.5;
}

struct CurveDraw {
    uint vertices;   // byte offset of the draw's first vertex
    uint indices;    // byte offset of its 16-bit indices
    uint triangles;
    uint constants;  // byte offset of its vertex constants
    uint palette;    // byte offset of its matrix palette
    uint flags;      // bit 0: position matrices from the palette
    uint output;     // byte offset of its output
    uint scratch;    // byte offset of its blocks' masks of whole triangles, then their offsets
};
#ifndef NEBULA_VULKAN
#define VK_BINDING(n)
#else
#define VK_BINDING(n) [[vk::binding(n, 0)]]
#endif
VK_BINDING(0) StructuredBuffer<CurveDraw> draws : register(t0);
VK_BINDING(5) StructuredBuffer<uint2> blocks : register(t5);  // a group's draw and first triangle
VK_BINDING(1) ByteAddressBuffer vertex_data : register(t1);
VK_BINDING(2) ByteAddressBuffer index_data : register(t2);
VK_BINDING(3) ByteAddressBuffer constant_data : register(t3);
VK_BINDING(4) ByteAddressBuffer palette_data : register(t4);
VK_BINDING(6) RWByteAddressBuffer sorted : register(u0);

static const float kCurveFlat = 0.8;
static const float kNearEye = 0.01;

struct Corner {
    float4 clip;
    float3 eye;
    float3 bend;
    float3 crease;
    uint place;
    uint first_neighbour;
    uint second_neighbour;
};

float3 turned(float3x3 cofactor, uint packed) {
    const float3 n = mul(cofactor, curve_unpack(packed));
    return dot(n, n) > 0.0 ? normalize(n) : n;
}

// A group's draw: its projection (rows 0-3), the inline position matrix (4-6), the half viewport (7.xy) and, for
// palette draws, the 64 position-matrix rows from its palette base (8-71), loaded once per group.
groupshared float4 draw_rows[72];

void load_draw_rows(CurveDraw d, uint tid) {
    if (tid < 4u) {
        draw_rows[tid] = asfloat(constant_data.Load4(d.constants + 16u * tid));
    } else if (tid < 7u) {
        draw_rows[tid] = asfloat(constant_data.Load4(d.constants + CB_INLINE_POS + 16u * (tid - 4u)));
    } else if (tid == 7u) {
        draw_rows[7] = float4(0.5 * asfloat(constant_data.Load2(d.constants + CB_RASTER)), 0.0, 0.0);
    }
    if ((d.flags & 1u) != 0u) {
        // The position and texture rows from the draw's blocks in the matrix ring (VertexConstants pos_base and
        // row_bases.y).
        const uint at = tid < 30u ? constant_data.Load(d.constants + CB_POS_BASE) + tid
                                  : constant_data.Load(d.constants + CB_ROW_BASES + 4u) + (tid - 30u);
        draw_rows[8u + tid] = asfloat(palette_data.Load4(16u * at));
    }
    GroupMemoryBarrierWithGroupSync();
}

Corner corner(CurveDraw d, uint vertex) {
    const uint at = d.vertices + vertex * VERTEX_STRIDE;
    const float3 pos = asfloat(vertex_data.Load3(at));
    const uint pn_row = min(vertex_data.Load(at + VERTEX_MTX) & 0x3Fu, 61u);
    const uint third = vertex_data.Load(at + VERTEX_MTX + 8u);
    const uint3 curve = vertex_data.Load3(at + VERTEX_CURVE);
    const uint first_row = (d.flags & 1u) != 0u ? 8u + pn_row : 4u;
    float4 rows[3];
    rows[0] = draw_rows[first_row];
    rows[1] = draw_rows[first_row + 1u];
    rows[2] = draw_rows[first_row + 2u];
    const float4 p = float4(pos, 1.0);
    const float4 wpos = float4(dot(rows[0], p), dot(rows[1], p), dot(rows[2], p), 1.0);
    const float3x3 cofactor = float3x3(cross(rows[1].xyz, rows[2].xyz), cross(rows[2].xyz, rows[0].xyz),
                                       cross(rows[0].xyz, rows[1].xyz));
    const float4x4 projection = float4x4(draw_rows[0], draw_rows[1], draw_rows[2], draw_rows[3]);
    Corner c;
    c.clip = mul(projection, wpos);
    c.eye = wpos.xyz;
    c.bend = turned(cofactor, curve.x);
    c.crease = turned(cofactor, curve.y);
    c.place = curve.z & 0xFFFFu;
    c.first_neighbour = curve.z >> 16u;
    c.second_neighbour = third >> 16u;
    return c;
}

float3 edge_normal(Corner a, Corner b) {
    if (b.place != 0xFFFFu && (b.place == a.first_neighbour || b.place == a.second_neighbour)) {
        return a.crease;
    }
    return a.bend;
}

bool edge_whole(Corner a, Corner b, float2 half_viewport) {
    if (abs(a.clip.w) < kNearEye || abs(b.clip.w) < kNearEye) {
        return false;
    }
    return curve_value(a.clip, b.clip, a.eye, b.eye, edge_normal(a, b), edge_normal(b, a), half_viewport) < kCurveFlat;
}

uint index_at(uint at) {
    const uint word = index_data.Load(at & ~3u);
    return (at & 2u) != 0u ? word >> 16u : word & 0xFFFFu;
}

groupshared uint block_mask[2];
groupshared uint scan[64];

// Pass one, a thread a triangle (a group a block of 64 of one draw): its bit in the block's mask of whole triangles.
[numthreads(64, 1, 1)]
void classify(uint3 group : SV_GroupID, uint tid : SV_GroupIndex) {
    const uint2 job = blocks[group.x];
    const CurveDraw d = draws[job.x];
    const uint t = job.y + tid;
    if (tid < 2u) {
        block_mask[tid] = 0u;
    }
    load_draw_rows(d, tid);
    if (t < d.triangles) {
        const float2 half_viewport = draw_rows[7].xy;
        const uint at = d.indices + t * 6u;
        const Corner c0 = corner(d, index_at(at));
        const Corner c1 = corner(d, index_at(at + 2u));
        const Corner c2 = corner(d, index_at(at + 4u));
        if (edge_whole(c1, c2, half_viewport) && edge_whole(c2, c0, half_viewport) &&
            edge_whole(c0, c1, half_viewport)) {
            InterlockedOr(block_mask[tid >> 5u], 1u << (tid & 31u));
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if (tid < 2u) {
        sorted.Store(d.scratch + 8u * (job.y / 64u) + 4u * tid, block_mask[tid]);
    }
}

// Pass two, a group a draw: the whole triangles before each block; the lists' draw arguments.
[numthreads(64, 1, 1)]
void offsets(uint3 group : SV_GroupID, uint tid : SV_GroupIndex) {
    const CurveDraw d = draws[group.x];
    const uint block_count = (d.triangles + 63u) / 64u;
    const uint before = d.scratch + 8u * block_count;
    uint total = 0u;
    for (uint first = 0u; first < block_count; first += 64u) {
        const uint b = first + tid;
        const uint2 mask = b < block_count ? sorted.Load2(d.scratch + 8u * b) : uint2(0u, 0u);
        const uint count = countbits(mask.x) + countbits(mask.y);
        scan[tid] = count;
        GroupMemoryBarrierWithGroupSync();
        [unroll] for (uint offset = 1u; offset < 64u; offset <<= 1u) {
            const uint earlier = tid >= offset ? scan[tid - offset] : 0u;
            GroupMemoryBarrierWithGroupSync();
            scan[tid] += earlier;
            GroupMemoryBarrierWithGroupSync();
        }
        if (b < block_count) {
            sorted.Store(before + 4u * b, total + scan[tid] - count);
        }
        total += scan[63];
        GroupMemoryBarrierWithGroupSync();
    }
    if (tid == 0u) {
        sorted.Store4(d.output, uint4(total * 3u, 1u, 0u, 0u));
        sorted.Store4(d.output + 16u, uint4(0u, (d.triangles - total) * 3u, 1u, 0u));
        sorted.Store2(d.output + 32u, uint2(0u, 0u));
    }
}

// Pass three, a thread a triangle: to its list, at the whole triangles (or patches) before it in the draw.
[numthreads(64, 1, 1)]
void scatter(uint3 group : SV_GroupID, uint tid : SV_GroupIndex) {
    const uint2 job = blocks[group.x];
    const CurveDraw d = draws[job.x];
    const uint t = job.y + tid;
    if (t >= d.triangles) {
        return;
    }
    const uint block = job.y / 64u;
    const uint2 mask = sorted.Load2(d.scratch + 8u * block);
    const uint below = tid < 32u ? countbits(mask.x & ((1u << tid) - 1u))
                                 : countbits(mask.x) + countbits(mask.y & ((1u << (tid - 32u)) - 1u));
    const bool whole = ((tid < 32u ? mask.x >> tid : mask.y >> (tid - 32u)) & 1u) != 0u;
    const uint block_count = (d.triangles + 63u) / 64u;
    const uint wholes_before = sorted.Load(d.scratch + 8u * block_count + 4u * block) + below;
    const uint at = d.indices + t * 6u;
    const uint3 v = uint3(index_at(at), index_at(at + 2u), index_at(at + 4u));
    const uint whole_out = d.output + 40u;
    const uint slot = whole ? whole_out + 12u * wholes_before : whole_out + 12u * (d.triangles + t - wholes_before);
    sorted.Store3(slot, v);
}
