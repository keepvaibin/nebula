// SPDX-License-Identifier: GPL-3.0-only
// A mesh whose vertices carry their own matrices, as the scene draws it this frame: each vertex through its draw
// matrix's place (the walk's rows, three of every six), then back through the block's root matrix, into the tracer's
// vertex buffer. The vertices are in the root's space: they change with the pose, not with the camera, and the
// instance of the structure built from them takes the root matrix.
#include "rt_bindings.hlsli"

ByteAddressBuffer g_positions : register(t1);  // three floats a vertex
ByteAddressBuffer g_matrices : register(t2);   // one uint a vertex: its draw matrix
ByteAddressBuffer g_rows : register(t3);       // the walk's rows, four floats each
RWByteAddressBuffer g_out : register(u7);      // three floats a vertex

cbuffer SkinConstants : register(b0) {
    uint4 g_at;       // bytes: the first position, the first matrix number, the block's first row, the first output
    uint4 g_count;    // x: vertices
    float4 g_root[3]; // the root's space from the eye's
};

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= g_count.x) {
        return;
    }
    const float4 at = float4(asfloat(g_positions.Load3(g_at.x + id.x * 12u)), 1.0f);
    const uint row = g_at.z + g_matrices.Load(g_at.y + id.x * 4u) * 96u;
    const float3 eye = float3(dot(asfloat(g_rows.Load4(row)), at), dot(asfloat(g_rows.Load4(row + 16u)), at),
                              dot(asfloat(g_rows.Load4(row + 32u)), at));
    const float4 seen = float4(eye, 1.0f);
    g_out.Store3(g_at.w + id.x * 12u, asuint(float3(dot(g_root[0], seen), dot(g_root[1], seen), dot(g_root[2], seen))));
}
