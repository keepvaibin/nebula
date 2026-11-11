// SPDX-License-Identifier: GPL-3.0-only
// 5.3 G-2: the opacity micromap of an alpha-tested mesh that stands still, from the item that draws it: the same map,
// coordinates and threshold cutout_there reads at a hit, so the two cannot disagree. One group a triangle (its number
// is the group's x plus 65535 times its y), one thread each of its 256 micro-triangles (subdivision level 4). A
// micro-triangle is OPAQUE (1) where every texel a bilinear lookup at the map's first level can read inside it is at
// least the threshold and a step, TRANSPARENT (0) where every one is below the threshold by a step, else UNKNOWN (3):
// a ray that meets it there asks cutout_there as before. A footprint over kMostTexels texels is left UNKNOWN.
// Pass constants: x the item, y the triangles, z the byte of the triangles' descriptors, w of their indices. Writes
// into u9 (rt_backend.h MicromapInput): from 0 each triangle's 64 bytes of states, at z its descriptor (the first byte
// of its states, level 4, format 2 = four states), at w its index (itself).
#include "rt_common.hlsli"

RWByteAddressBuffer g_omm : register(u9);

static const uint kLevel = 4u, kSegments = 16u;
static const uint kGroupsAcross = 65535u;
static const uint kMostTexels = 1024u;
static const float kStep = 1.0f / 255.0f;

groupshared uint g_states[16];

// The Vulkan specification's reference (VK_EXT_opacity_micromap, "Reference Code"): which micro-triangle of a
// triangle at `level` holds the point at barycentrics u, v (the second and third corners' weights).
uint micro_index(float u, float v, uint level) {
    u = clamp(u, 0.0f, 1.0f);
    v = clamp(v, 0.0f, 1.0f);
    const float fu = u * float(1u << level), fv = v * float(1u << level);
    uint iu = uint(fu), iv = uint(fv);
    const float uf = fu - float(iu), vf = fv - float(iv);
    if (iu >= (1u << level)) {
        iu = (1u << level) - 1u;
    }
    if (iv >= (1u << level)) {
        iv = (1u << level) - 1u;
    }
    const uint iuv = iu + iv;
    if (iuv >= (1u << level)) {
        iu -= iuv - (1u << level) + 1u;
    }
    uint iw = ~(iu + iv);
    if (uf + vf >= 1.0f && iuv < (1u << level) - 1u) {
        --iw;
    }
    uint b0 = ~(iu ^ iw);
    b0 &= (1u << level) - 1u;
    const uint t = (iu ^ iv) & b0;
    uint f = t;
    f ^= f >> 1u;
    f ^= f >> 2u;
    f ^= f >> 4u;
    f ^= f >> 8u;
    uint b1 = ((f ^ iu) & ~b0) | t;
    b0 = (b0 | (b0 << 8u)) & 0x00ff00ffu;
    b0 = (b0 | (b0 << 4u)) & 0x0f0f0f0fu;
    b0 = (b0 | (b0 << 2u)) & 0x33333333u;
    b0 = (b0 | (b0 << 1u)) & 0x55555555u;
    b1 = (b1 | (b1 << 8u)) & 0x00ff00ffu;
    b1 = (b1 | (b1 << 4u)) & 0x0f0f0f0fu;
    b1 = (b1 | (b1 << 2u)) & 0x33333333u;
    b1 = (b1 | (b1 << 1u)) & 0x55555555u;
    return b0 | (b1 << 1u);
}

// Micro-triangle `m` of the grid, row by row from the first corner's edge (row r holds 2 (16 - r) - 1, upright and
// upside down by turns): its corners' barycentrics.
void micro_corners(uint m, out float2 a, out float2 b, out float2 c) {
    uint row = 0u;
    uint left = m;
    [loop] while (row + 1u < kSegments && left >= 2u * (kSegments - row) - 1u) {
        left -= 2u * (kSegments - row) - 1u;
        ++row;
    }
    const uint column = left / 2u;
    const float n = float(kSegments);
    if ((left & 1u) == 0u) {
        a = float2(float(column), float(row)) / n;
        b = float2(float(column + 1u), float(row)) / n;
        c = float2(float(column), float(row + 1u)) / n;
    } else {
        a = float2(float(column + 1u), float(row)) / n;
        b = float2(float(column), float(row + 1u)) / n;
        c = float2(float(column + 1u), float(row + 1u)) / n;
    }
}

[numthreads(256, 1, 1)]
void main(uint3 group : SV_GroupID, uint thread : SV_GroupIndex) {
    const uint triangle_ = group.x + group.y * kGroupsAcross;
    if (triangle_ >= g_pass.y) {
        return;  // the whole group
    }
    if (thread < 16u) {
        g_states[thread] = 0u;
    }
    GroupMemoryBarrierWithGroupSync();
    const uint at = g_at.x + g_pass.x * kItemBytes;
    const uint4 first = g_items.Load4(at), second = g_items.Load4(at + 16u);
    const uint2 map = g_items.Load2(at + 32u);
    float2 a, b, c;
    micro_corners(thread, a, b, c);
    uint state = 0u;  // where the map cannot be read, cutout_there says nothing is there
    if (first.z < 8u && map.x != 0xFFFFFFFFu) {
        const uint base = mesh_at(first.z);
        const uint3 corner = triangle_corners(first, second, triangle_);
        const float2 c0 = asfloat(arena_words(first.z, base + first.w + corner.x * 24u + 12u).yz);
        const float2 c1 = asfloat(arena_words(first.z, base + first.w + corner.y * 24u + 12u).yz);
        const float2 c2 = asfloat(arena_words(first.z, base + first.w + corner.z * 24u + 12u).yz);
        const float2 ua = c0 * (1.0f - a.x - a.y) + c1 * a.x + c2 * a.y;
        const float2 ub = c0 * (1.0f - b.x - b.y) + c1 * b.x + c2 * b.y;
        const float2 uc = c0 * (1.0f - c.x - c.y) + c1 * c.x + c2 * c.y;
        uint wide, high;
        g_textures[NonUniformResourceIndex(map.x)].GetDimensions(wide, high);
        const float2 size = float2(float(wide), float(high));
        // The texels a bilinear lookup anywhere inside reads: from the one left of and above the lowest centre to the
        // one right of and below the highest.
        const float2 low = floor(min(ua, min(ub, uc)) * size - 0.5f);
        const float2 top = floor(max(ua, max(ub, uc)) * size - 0.5f) + 1.0f;
        const float2 count = top - low + 1.0f;
        state = 3u;
        if (all(count > 0.0f) && count.x * count.y <= float(kMostTexels) && wide != 0u && high != 0u) {
            const float threshold = asfloat(map.y);
            float least = 1.0f, most = 0.0f;
            const int2 from = int2(low);
            const int2 span = int2(count);
            for (int y = 0; y < span.y; ++y) {
                const int ty = ((from.y + y) % int(high) + int(high)) % int(high);  // the map wraps (g_wrap)
                for (int x = 0; x < span.x; ++x) {
                    const int tx = ((from.x + x) % int(wide) + int(wide)) % int(wide);
                    const float alpha = g_textures[NonUniformResourceIndex(map.x)].Load(int3(tx, ty, 0)).a;
                    least = min(least, alpha);
                    most = max(most, alpha);
                }
            }
            state = least >= threshold + kStep ? 1u : most < threshold - kStep ? 0u : 3u;
        }
    }
    const float2 centre = (a + b + c) / 3.0f;
    const uint micro = micro_index(centre.x, centre.y, kLevel);
    InterlockedOr(g_states[micro >> 4u], state << ((micro & 15u) * 2u));
    GroupMemoryBarrierWithGroupSync();
    if (thread < 16u) {
        g_omm.Store(triangle_ * 64u + thread * 4u, g_states[thread]);
    }
    if (thread == 0u) {
        g_omm.Store2(g_pass.z + triangle_ * 8u, uint2(triangle_ * 64u, kLevel | 2u << 16u));
        g_omm.Store(g_pass.w + triangle_ * 4u, triangle_);
    }
}
