// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Interpolated constants on the GPU (interp_gpu.h, evaluate()), for both renderers: copy_main fills a sequence's
// blocks from the rings before its first sub-frame, eval_main writes each op at t. The views have no bounds on D3D12:
// every access is clamped to its buffer. Vulkan bindings (set 0): 0 program, 1 constant ring, 2 matrix ring, 3 output,
// 4 blend verdicts, 5 the frame constants.

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
            const float4 a = row(p + 4u * i);
            m.r[i] = a + (row(p + 12u + 4u * i) - a) * at;
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
    if (kind <= 1u) {
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
        [unroll] for (uint i = 0u; i < 3u; ++i) {
            if (kind == 0u) {
                put(dst + 4u * i, asuint(m.r[i].x));
                put(dst + 4u * i + 1u, asuint(m.r[i].y));
                put(dst + 4u * i + 2u, asuint(m.r[i].z));
                put(dst + 4u * i + 3u, asuint(m.r[i].w));
            } else {
                put(dst + 3u * i, asuint(m.r[i].x));
                put(dst + 3u * i + 1u, asuint(m.r[i].y));
                put(dst + 3u * i + 2u, asuint(m.r[i].z));
            }
        }
    } else if (kind <= 3u) {
        for (uint i = 0u; i < n; ++i) {
            const float a = value(p + i);
            const float b = value(p + n + i);
            const float v = a == b ? b : a + (b - a) * at;
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
        const float3 wa = vec(p);
        const float3 va = vec(p + 6u);
        float3 pos = wa + (vec(p + 3u) - wa) * at;
        float3 dir = va + (vec(p + 9u) - va) * at;
        if ((flags & kLightSplit) != 0u) {
            pos = float3(dot(c0.xyz, pos) + c0.w, dot(c1.xyz, pos) + c1.w, dot(c2.xyz, pos) + c2.w);
            dir = float3(dot(c0.xyz, dir), dot(c1.xyz, dir), dot(c2.xyz, dir));
        }
        const float la = value(p + 12u);
        const float lb = value(p + 13u);
        const float ld = length(dir);
        if (ld > 1e-20) {
            dir *= (la + (lb - la) * at) / ld;
        }
        put(dst, asuint(pos.x));
        put(dst + 1u, asuint(pos.y));
        put(dst + 2u, asuint(pos.z));
        put(dst + 3u, asuint(dir.x));
        put(dst + 4u, asuint(dir.y));
        put(dst + 5u, asuint(dir.z));
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

}  // namespace galaxy::gx
