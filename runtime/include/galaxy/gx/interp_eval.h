// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The interpolation's compute shaders, for both renderers. The views have no bounds on D3D12: every access is clamped
// to its buffer.
// - kInterpEvalCs (interp_gpu.h, evaluate()): copy_main fills a sequence's blocks from the rings before its first
//   sub-frame, eval_main writes each op at t. Vulkan set 0: 0 program, 1 constant ring, 2 matrix ring, 3 output,
//   4 blend verdicts, 5 the frame constants.
// - kBlendValidateCs and kStripeGatherCs (vertex blending): Vulkan set 0: 0 vertex ring, 1 resident vertices, 2 the
//   sequence's table, 3 the output; the limits are push constants.

#include <cstdint>

namespace galaxy::gx {

inline constexpr char kInterpEvalCs[] = R"HLSL(
#ifndef NEBULA_VULKAN
#define VK_BINDING(n)
#else
#define VK_BINDING(n) [[vk::binding(n, 0)]]
#endif
VK_BINDING(0) ByteAddressBuffer program : register(t0);
VK_BINDING(1) ByteAddressBuffer constant_ring : register(t1);
VK_BINDING(2) ByteAddressBuffer matrix_ring : register(t2);
VK_BINDING(3) RWByteAddressBuffer output : register(u0);
VK_BINDING(4) RWStructuredBuffer<uint4> verdicts : register(u1);
VK_BINDING(5) cbuffer Frame : register(b0) {
    float4 camera0;
    float4 camera1;
    float4 camera2;
    float t;
    uint count;
    uint table;
    uint payload;
    uint program_last;
    uint output_last;
    uint constants_last;
    uint matrices_last;
    uint frame_slot;
    float t_prev;
    float2 jitter;
    float4 camera_prev0;
    float4 camera_prev1;
    float4 camera_prev2;
    uint mode;  // 1: only the stores and the ops at the frame before (the frame at t = 1)
};
static const uint kSplit = 0x100u;
static const uint kStill = 0x200u;
static const uint kRigid = 0x400u;
static const uint kLightSplit = 0x800u;
static const uint kPrev = 0x1000u;
static const uint kEndA = 0x2000u;
static const uint kEndB = 0x4000u;
// interp_gpu.h kHeader*: the camera at t = 0 and t = 1, then 0 and 1 as data.
static const uint kHeaderCamera0 = 0u;
static const uint kHeaderCamera1 = 12u;
static const uint kHeaderZero = 24u;
static const uint kHeaderOne = 25u;
static const uint kLightWords = 14u;
uint word(uint index) {
    return program.Load(min(payload + index * 4u, program_last));
}
float value(uint index) {
    return asfloat(word(index));
}
float4 row(uint index) {
    return float4(value(index), value(index + 1u), value(index + 2u), value(index + 3u));
}
float3 vec(uint index) {
    return float3(value(index), value(index + 1u), value(index + 2u));
}
void put(uint index, uint bits) {
    output.Store(min(index * 4u, output_last), bits);
}
struct Affine {
    float4 r[3];
};
Affine multiply(Affine a, Affine b) {
    Affine o;
    [unroll] for (uint i = 0u; i < 3u; ++i) {
        o.r[i] = a.r[i].x * b.r[0] + a.r[i].y * b.r[1] + a.r[i].z * b.r[2];
        o.r[i].w += a.r[i].w;
    }
    return o;
}
Affine transform_at(uint p, uint flags, float at) {
    Affine m;
    if ((flags & kStill) != 0u) {
        [unroll] for (uint i = 0u; i < 3u; ++i) {
            m.r[i] = row(p + 4u * i);
        }
        return m;
    }
    if ((flags & kRigid) == 0u) {
        [unroll] for (uint i = 0u; i < 3u; ++i) {
            m.r[i] = row(p + 4u * i) * (1.0 - at) + row(p + 12u + 4u * i) * at;
        }
        return m;
    }
    const float theta = value(p + 32u);
    const float inv_sin = value(p + 33u);
    float wa = 1.0 - at;
    float wb = at;
    if (theta != 0.0) {
        wa = sin((1.0 - at) * theta) * inv_sin;
        wb = sin(at * theta) * inv_sin;
    }
    float4 q = row(p) * wa + row(p + 4u) * wb;
    q *= rsqrt(dot(q, q));
    const float3 r0 = float3(1.0 - 2.0 * (q.y * q.y + q.z * q.z), 2.0 * (q.x * q.y - q.z * q.w),
                             2.0 * (q.x * q.z + q.y * q.w));
    const float3 r1 = float3(2.0 * (q.x * q.y + q.z * q.w), 1.0 - 2.0 * (q.x * q.x + q.z * q.z),
                             2.0 * (q.y * q.z - q.x * q.w));
    const float3 r2 = float3(2.0 * (q.x * q.z - q.y * q.w), 2.0 * (q.y * q.z + q.x * q.w),
                             1.0 - 2.0 * (q.x * q.x + q.y * q.y));
    float3 s[3];
    [unroll] for (uint k = 0u; k < 3u; ++k) {
        const float3 a = vec(p + 8u + 3u * k);
        s[k] = a + (vec(p + 17u + 3u * k) - a) * at;
    }
    const float3 ta = vec(p + 26u);
    const float3 tr = ta + (vec(p + 29u) - ta) * at;
    m.r[0] = float4(r0.x * s[0] + r0.y * s[1] + r0.z * s[2], tr.x);
    m.r[1] = float4(r1.x * s[0] + r1.y * s[1] + r1.z * s[2], tr.y);
    m.r[2] = float4(r2.x * s[0] + r2.y * s[1] + r2.z * s[2], tr.z);
    return m;
}
uint transform_words(uint flags) {
    return (flags & kStill) != 0u ? 12u : (flags & kRigid) != 0u ? 34u : 24u;
}
// An Affine (kind 0) or Normal (kind 1) op's value at `at` with the camera rows c, before its endpoint terms.
Affine transform_value(uint p, uint flags, uint kind, float at, float4 c0, float4 c1, float4 c2) {
    Affine m = transform_at(p, flags, at);
    Affine c;
    c.r[0] = c0;
    c.r[1] = c1;
    c.r[2] = c2;
    if (kind == 1u) {
        [unroll] for (uint i = 0u; i < 3u; ++i) {
            m.r[i].w = 0.0;
            c.r[i].w = 0.0;
        }
    }
    if ((flags & kSplit) != 0u) {
        m = multiply(c, m);
    }
    return m;
}
// |v| without the squares overflowing (transform_history.h vector_length).
float safe_length(float3 v) {
    const float largest = max(max(abs(v.x), abs(v.y)), abs(v.z));
    return largest > 0.0 && isfinite(largest) ? largest * length(v / largest) : largest;
}
// A light op's position (xyz of the first) and direction at `at`.
void light_value(uint p, uint flags, float at, float4 c0, float4 c1, float4 c2, out float3 pos, out float3 dir) {
    pos = vec(p) * (1.0 - at) + vec(p + 3u) * at;
    dir = vec(p + 6u) * (1.0 - at) + vec(p + 9u) * at;
    if ((flags & kLightSplit) != 0u) {
        pos = float3(dot(c0.xyz, pos) + c0.w, dot(c1.xyz, pos) + c1.w, dot(c2.xyz, pos) + c2.w);
        dir = float3(dot(c0.xyz, dir), dot(c1.xyz, dir), dot(c2.xyz, dir));
    }
    const float ld = safe_length(dir);
    if (ld > 1e-20) {
        dir *= (value(p + 12u) * (1.0 - at) + value(p + 13u) * at) / ld;
    }
}
// value + (1 - t)(A - value at 0) + t(B - value at 1), each term only where it is finite.
float endpoint_exact(float v, float at, bool has_a, float a, float v0, bool has_b, float b, float v1) {
    float da = has_a ? a - v0 : 0.0;
    float db = has_b ? b - v1 : 0.0;
    da = isfinite(da) ? da : 0.0;
    db = isfinite(db) ? db : 0.0;
    return v + (1.0 - at) * da + at * db;
}
[numthreads(64, 1, 1)]
void copy_main(uint3 group : SV_GroupID, uint lane : SV_GroupIndex) {
    if (group.x >= count) {
        return;
    }
    const uint4 c = program.Load4(min(table + group.x * 16u, program_last));
    for (uint w = lane; w < c.w; w += 64u) {
        const uint from = c.y + w * 4u;
        const uint a = constant_ring.Load(min(from, constants_last));
        const uint b = matrix_ring.Load(min(from, matrices_last));
        put(c.z / 4u + w, c.x == 0u ? a : b);
    }
}
[numthreads(64, 1, 1)]
void eval_main(uint3 id : SV_DispatchThreadID) {
    if (id.x == 0u) {
        verdicts[min(frame_slot, 8192u)] = uint4(asuint(t), asuint(t_prev), asuint(jitter));
    }
    if (id.x >= count) {
        return;
    }
    const uint4 op = program.Load4(min(table + id.x * 16u, program_last));
    const uint kind = op.x & 0xFFu;
    const uint flags = op.x & 0xFF00u;
    const uint n = min(op.x >> 16u, 16u);
    const uint dst = op.y;
    const uint p = op.z;
    const bool previous = (flags & kPrev) != 0u;
    if (!previous && mode != 0u && kind != 6u) {
        return;
    }
    const float at = previous ? t_prev : t;
    const float4 c0 = previous ? camera_prev0 : camera0;
    const float4 c1 = previous ? camera_prev1 : camera1;
    const float4 c2 = previous ? camera_prev2 : camera2;
    const bool has_a = (flags & kEndA) != 0u;
    const bool has_b = (flags & kEndB) != 0u;
    // The ends are evaluated with 0 and 1 read as data: f(0) runs the instructions f(t) runs at t = 0.
    const float zero = value(kHeaderZero);
    const float one = value(kHeaderOne);
    if (kind <= 1u) {
        const Affine m = transform_value(p, flags, kind, at, c0, c1, c2);
        Affine m0 = m;
        Affine m1 = m;
        if (has_a || has_b) {
            m0 = transform_value(p, flags, kind, zero, row(kHeaderCamera0), row(kHeaderCamera0 + 4u),
                                 row(kHeaderCamera0 + 8u));
            m1 = transform_value(p, flags, kind, one, row(kHeaderCamera1), row(kHeaderCamera1 + 4u),
                                 row(kHeaderCamera1 + 8u));
        }
        const uint end_a = p + transform_words(flags);
        const uint end_b = end_a + (has_a ? n : 0u);
        const uint columns = kind == 0u ? 4u : 3u;
        for (uint i = 0u; i < n; ++i) {
            const uint r = i / columns;
            const uint k = i % columns;
            const float v = endpoint_exact(m.r[r][k], at, has_a, has_a ? value(end_a + i) : 0.0, m0.r[r][k], has_b,
                                           has_b ? value(end_b + i) : 0.0, m1.r[r][k]);
            put(dst + i, asuint(v));
        }
    } else if (kind <= 3u) {
        for (uint i = 0u; i < n; ++i) {
            const float a = value(p + i);
            const float b = value(p + n + i);
            const float v = a == b ? b : a * (1.0 - at) + b * at;
            put(dst + i, kind == 2u ? asuint(v) : asuint(int(v >= 0.0 ? floor(v + 0.5) : ceil(v - 0.5))));
        }
    } else if (kind == 4u) {
        const uint a = word(p);
        const uint b = word(p + 1u);
        uint c = 0u;
        [unroll] for (uint s = 0u; s < 32u; s += 8u) {
            const float x = float((a >> s) & 0xFFu);
            const float y = float((b >> s) & 0xFFu);
            c |= uint(floor(clamp(x + (y - x) * at, 0.0, 255.0) + 0.5)) << s;
        }
        put(dst, c);
    } else if (kind == 5u) {
        float3 pos, dir;
        light_value(p, flags, at, c0, c1, c2, pos, dir);
        float3 pos0 = pos, dir0 = dir, pos1 = pos, dir1 = dir;
        if (has_a || has_b) {
            light_value(p, flags, zero, row(kHeaderCamera0), row(kHeaderCamera0 + 4u), row(kHeaderCamera0 + 8u),
                        pos0, dir0);
            light_value(p, flags, one, row(kHeaderCamera1), row(kHeaderCamera1 + 4u), row(kHeaderCamera1 + 8u),
                        pos1, dir1);
        }
        const uint end_a = p + kLightWords;
        const uint end_b = end_a + (has_a ? 6u : 0u);
        const float v[6] = {pos.x, pos.y, pos.z, dir.x, dir.y, dir.z};
        const float v0[6] = {pos0.x, pos0.y, pos0.z, dir0.x, dir0.y, dir0.z};
        const float v1[6] = {pos1.x, pos1.y, pos1.z, dir1.x, dir1.y, dir1.z};
        [unroll] for (uint i = 0u; i < 6u; ++i) {
            put(dst + i, asuint(endpoint_exact(v[i], at, has_a, has_a ? value(end_a + i) : 0.0, v0[i], has_b,
                                               has_b ? value(end_b + i) : 0.0, v1[i])));
        }
    } else if (kind == 6u) {
        for (uint i = 0u; i < n; ++i) {
            put(dst + i, word(p + i));
        }
    }
}
)HLSL";


// kInterpEvalCs's cbuffer Frame (root constants on D3D12, a uniform buffer on Vulkan).
struct InterpEvalFrame {
    float camera[12];
    float t;
    std::uint32_t count;
    std::uint32_t table;
    std::uint32_t payload;
    std::uint32_t program_last;
    std::uint32_t output_last;
    std::uint32_t constants_last;
    std::uint32_t matrices_last;
    std::uint32_t frame_slot;
    float t_prev;
    float jitter[2];
    float camera_prev[12];
    std::uint32_t mode;
};
static_assert(sizeof(InterpEvalFrame) == 37u * 4u);

// Blend-pair check: one group per pair compares the positions of the two
// draws vertex by vertex (GxVertexOut, 144-byte stride; position first).
inline constexpr char kBlendValidateCs[] = R"HLSL(
#ifndef NEBULA_VULKAN
#define VK_BINDING(n)
#define PUSH_CONSTANTS(name, reg) cbuffer name : register(reg)
#else
#define VK_BINDING(n) [[vk::binding(n, 0)]]
// A push-constant block takes no register (glslang would decorate a binding on it).
#define PUSH_CONSTANTS(name, reg) [[vk::push_constant]] cbuffer name
#endif
VK_BINDING(0) ByteAddressBuffer ring : register(t0);
VK_BINDING(1) ByteAddressBuffer resident : register(t1);
VK_BINDING(2) StructuredBuffer<uint4> pairs : register(t2);
VK_BINDING(3) RWStructuredBuffer<uint4> verdicts : register(u0);
// Last valid Load3 offset of each buffer. Root views have no bounds, and
// compilers may run both loads of a select: both stay inside their buffers.
PUSH_CONSTANTS(Limits, b0) {
    uint ring_last;
    uint resident_last;
};
groupshared uint g_same;
groupshared uint g_worst;
groupshared uint g_lo[3];
groupshared uint g_hi[3];
// Float bits ordered like the floats, for integer min/max.
uint ordered(float f) {
    const uint u = asuint(f);
    return (u & 0x80000000u) != 0u ? ~u : (u | 0x80000000u);
}
float unordered(uint u) {
    return asfloat((u & 0x80000000u) != 0u ? (u & 0x7FFFFFFFu) : ~u);
}
float3 position(uint in_resident, uint offset) {
    const float3 r = asfloat(resident.Load3(min(offset, resident_last)));
    const float3 g = asfloat(ring.Load3(min(offset, ring_last)));
    return in_resident != 0u ? r : g;
}
[numthreads(64, 1, 1)]
void main(uint3 group : SV_GroupID, uint lane : SV_GroupIndex) {
    const uint4 pair = pairs[group.x];
    if (lane == 0u) {
        g_same = 0u;
        g_worst = 0u;
        [unroll] for (uint k = 0u; k < 3u; ++k) {
            g_lo[k] = 0xFFFFFFFFu;
            g_hi[k] = 0u;
        }
    }
    GroupMemoryBarrierWithGroupSync();
    uint same = 0u, worst = 0u;
    uint lo[3] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
    uint hi[3] = {0u, 0u, 0u};
    for (uint i = lane; i < pair.z; i += 64u) {
        const float3 c = position(pair.w & 1u, pair.x + i * 144u);
        const float3 p = position((pair.w >> 1u) & 1u, pair.y + i * 144u);
        same += all(c == p) ? 1u : 0u;
        const float3 d = c - p;
        const float dist = sqrt(dot(d, d));
        worst = max(worst, isfinite(dist) ? asuint(dist) : 0x7F800000u);
        [unroll] for (uint k = 0u; k < 3u; ++k) {
            lo[k] = min(lo[k], min(ordered(c[k]), ordered(p[k])));
            hi[k] = max(hi[k], max(ordered(c[k]), ordered(p[k])));
        }
    }
    InterlockedAdd(g_same, same);
    InterlockedMax(g_worst, worst);
    [unroll] for (uint k = 0u; k < 3u; ++k) {
        InterlockedMin(g_lo[k], lo[k]);
        InterlockedMax(g_hi[k], hi[k]);
    }
    GroupMemoryBarrierWithGroupSync();
    if (lane == 0u) {
        float3 extent;
        [unroll] for (uint k = 0u; k < 3u; ++k) {
            extent[k] = unordered(g_hi[k]) - unordered(g_lo[k]);
        }
        const float size = sqrt(dot(extent, extent));
        verdicts[group.x] = uint4(pair.z, g_same, g_worst, asuint(isfinite(size) ? size : 0.0));
    }
}
)HLSL";

// Earlier stripe vertices, one 144-byte GxVertexOut per output vertex, from the
// ring or the resident buffer at the source's byte offset; dispatched at the
// start of a sequence's first sub-frame, before its draws.
inline constexpr char kStripeGatherCs[] = R"HLSL(
#ifndef NEBULA_VULKAN
#define VK_BINDING(n)
#define PUSH_CONSTANTS(name, reg) cbuffer name : register(reg)
#else
#define VK_BINDING(n) [[vk::binding(n, 0)]]
// A push-constant block takes no register (glslang would decorate a binding on it).
#define PUSH_CONSTANTS(name, reg) [[vk::push_constant]] cbuffer name
#endif
VK_BINDING(0) ByteAddressBuffer ring : register(t0);
VK_BINDING(1) ByteAddressBuffer resident : register(t1);
VK_BINDING(2) StructuredBuffer<uint2> sources : register(t2);
VK_BINDING(3) RWByteAddressBuffer earlier : register(u0);
PUSH_CONSTANTS(Limits, b0) {
    uint ring_last;
    uint resident_last;
    uint vertices;
};
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const uint k = id.x / 36u;
    if (k >= vertices) {
        return;
    }
    const uint word = id.x - k * 36u;
    const uint2 source = sources[k];
    const uint at = source.x + word * 4u;
    // Both loads stay inside their buffers (compilers may run both).
    const uint a = ring.Load(min(at, ring_last));
    const uint b = resident.Load(min(at, resident_last));
    earlier.Store(k * 144u + word * 4u, source.y != 0u ? b : a);
}
)HLSL";

}  // namespace galaxy::gx
