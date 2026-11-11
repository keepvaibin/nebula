// SPDX-License-Identifier: GPL-3.0-only
// The blue noise into its image, once (rt_walk.cpp rt_noise_fill): 128 x 128 texels of four bytes, from the bytes
// rt_blue_noise.inc holds, uploaded to t1 at byte g_pass.w.
#include "rt_bindings.hlsli"
#include "rt_frame.hlsli"

ByteAddressBuffer g_bytes : register(t1);
RT_FORMAT(rgba8) RWTexture2D<float4> g_noise : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= 128u || id.y >= 128u) {
        return;
    }
    const uint word = g_bytes.Load(g_pass.w + (id.y * 128u + id.x) * 4u);
    g_noise[id.xy] = float4(float(word & 255u), float(word >> 8u & 255u), float(word >> 16u & 255u), float(word >> 24u)) / 255.0f;
}
