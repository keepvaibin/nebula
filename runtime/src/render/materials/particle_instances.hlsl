// SPDX-License-Identifier: GPL-3.0-only
// The particle material's vertex stage for instanced draws (NEBULA_ITEM_INSTANCES): each instance is a particle
// (NebulaParticleInstance, ten rows from the instance table's z), each six vertices a quad of it (twelve for a crossed
// shape). A corner's place in the particle and its coordinates come from the quad tables (the game's jpa_pos and jpa_crd,
// computed) at the draw's first entries (params.w, test.w); the particle's matrices now and a game frame ago take it into the world,
// where it stands at the presented frame's time. The pixel stage is particle.hlsl's.
#define NATIVE_OWN_VERTEX 1
#include "particle.hlsl"

// The quad tables (the game's jpa_pos and jpa_crd) as arithmetic: an indexed constant array is private memory on some
// drivers (Adreno's compiler fails the pipeline). Places: three planes (xy, xz, yz) of nine 50-unit quads, the quad's
// corner c going (u0, v0), (u1, v0), (u1, v1), (u0, v1), u0 = -25 * (k % 3), v0 = 25 * (k / 3).
float3 quad_place(uint i) {
    const uint plane = min(i / 36u, 2u);
    const uint k = (i % 36u) / 4u;
    const uint c = i & 3u;
    const float u = -25.0 * float(k % 3u) + ((c == 1u || c == 2u) ? 50.0 : 0.0);
    const float v = 25.0 * float(k / 3u) - (c >= 2u ? 50.0 : 0.0);
    return plane == 0u ? float3(u, v, 0.0) : (plane == 1u ? float3(u, 0.0, -v) : float3(0.0, v, u));
}

// Coordinates: four quads of one or two repeats each way (s & 1 across, s >> 1 down).
float2 quad_coord(uint i) {
    const uint s = (i / 4u) & 3u;
    const uint c = i & 3u;
    return float2((c == 1u || c == 2u) ? float((s & 1u) + 1u) : 0.0, c >= 2u ? float((s >> 1u) + 1u) : 0.0);
}

// A quad's two triangles: corners 0, 1, 2 and 0, 2, 3. A crossed shape's second quad takes the places 72 on.
uint quad_corner(uint v) {
    return v < 3u ? v : (v == 3u ? 0u : v - 2u);
}

float3 particle_times(uint row, float4 p) {
    return float3(dot(rows[row], p), dot(rows[row + 1u], p), dot(rows[row + 2u], p));
}

float4 particle_color(uint word) {
    return float4(uint4(word >> 24u, word >> 16u, word >> 8u, word) & 255u) / 255.0;
}

struct ParticleVertexIn {
    uint vertex : SV_VertexID;
    uint instance : SV_InstanceID;
};

NativeVaryings native_vs(ParticleVertexIn input) {
    const uint at = instances[draw_first_instance].z + input.instance * 10u;
    const uint quad = input.vertex / 6u;
    const uint corner = quad_corner(input.vertex - quad * 6u);
    const float4 p = float4(quad_place(uint(material.params.w) + quad * 72u + corner), 1.0);
    const float2 st = quad_coord(uint(material.test.w) + corner);
    const uint4 words = asuint(rows[at + 9u]);
    float2 uv = st;
    float q = 1.0;
    if (words.z == 1u) {
        const float4 c = float4(st, 1.0, 1.0);
        uv = float2(dot(rows[at + 6u], c), dot(rows[at + 7u], c));
    } else if (words.z == 2u) {
        uv = float2(dot(rows[at + 6u], p), dot(rows[at + 7u], p));
        q = dot(rows[at + 8u], p);
    }
    const float3 now = particle_times(at, p);
    const float3 was = particle_times(at + 3u, p);
    const Place place = native_place_moved(lerp(was, now, view_scale.y), lerp(was, now, view_scale.z), 0u, 0u);
    NativeVaryings o = (NativeVaryings)0;
    PLACE_OUT(o, place);
    particle_out(uv, q, st, particle_color(words.x), particle_color(words.y), o);
    return o;
}
