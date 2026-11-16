// SPDX-License-Identifier: GPL-3.0-only
// A mesh whose vertices carry their own matrices, as the scene draws it this frame: each vertex through its draw
// matrix's place (the walk's rows, three of every six), then back through the block's root matrix, into the tracer's
// vertex buffer. The vertices are in the root's space: they change with the pose, not with the camera, and the
// instance of the structure built from them takes the root matrix.
#include "rt_bindings.hlsli"

ByteAddressBuffer g_positions : register(t1);  // three floats a vertex
ByteAddressBuffer g_matrices : register(t2);   // one uint a vertex: its draw matrix
ByteAddressBuffer g_rows : register(t3);       // the walk's rows, four floats each
RWByteAddressBuffer g_out : register(u9);      // three floats a vertex

cbuffer SkinConstants : register(b0) {
    uint4 g_at;       // bytes: the first position, the first matrix number, the block's first row, the first output
    uint4 g_count;    // x: vertices
    float4 g_root[3]; // the root's space from the eye's
};

// Whether each of four floats, by its bits, is a number: an exponent not all ones. A bit test, because a float test of
// a computed value may be folded to true: a Vulkan driver takes NotNaN and NotInf for every float operation of a module
// without SignedZeroInfNanPreserve (spirvenv, "Precision and Operation"); the inputs, as loaded, pass through none.
bool4 numbers(uint4 bits) { return (bits & 0x7F800000u) != 0x7F800000u; }

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= g_count.x) {
        return;
    }
    const uint3 position = g_positions.Load3(g_at.x + id.x * 12u);
    const float4 at = float4(asfloat(position), 1.0f);
    const uint row = g_at.z + g_matrices.Load(g_at.y + id.x * 4u) * 96u;
    const uint4 r0 = g_rows.Load4(row), r1 = g_rows.Load4(row + 16u), r2 = g_rows.Load4(row + 32u);
    const float3 eye = float3(dot(asfloat(r0), at), dot(asfloat(r1), at), dot(asfloat(r2), at));
    const float4 seen = float4(eye, 1.0f);
    const float3 placed = float3(dot(g_root[0], seen), dot(g_root[1], seen), dot(g_root[2], seen));
    // A vertex its inputs cannot place (a NaN or infinite position, row or root, or a product past the float range)
    // goes to the root's origin, one point for every such vertex: a triangle of three of them is a point, active and
    // never met, which a refit may make and unmake (DXR and Vulkan: a degenerate triangle may; a NaN vertex's, inactive,
    // may not). The inputs are tested as loaded, before any arithmetic.
    const bool placeable = all(numbers(r0)) && all(numbers(r1)) && all(numbers(r2)) && all(numbers(uint4(position, 0u))) &&
                           all(numbers(asuint(g_root[0]))) && all(numbers(asuint(g_root[1]))) && all(numbers(asuint(g_root[2]))) &&
                           all(numbers(uint4(asuint(placed), 0u)));
    g_out.Store3(g_at.w + id.x * 12u, placeable ? asuint(placed) : uint3(0u, 0u, 0u));
}
