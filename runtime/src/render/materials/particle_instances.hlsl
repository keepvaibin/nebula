// SPDX-License-Identifier: GPL-3.0-only
// The particle material's vertex stage for instanced draws (NEBULA_ITEM_INSTANCES): each instance is a particle
// (NebulaParticleInstance, ten rows from the instance table's z), each six vertices a quad of it (twelve for a crossed
// shape). A corner's place in the particle and its coordinates come from the quad tables (the game's jpa_pos and jpa_crd)
// at the draw's first entries (params.w, test.w); the particle's matrices now and a game frame ago take it into the world,
// where it stands at the presented frame's time. The pixel stage is particle.hlsl's.
#define NATIVE_OWN_VERTEX 1
#include "particle.hlsl"

static const float3 kQuadPlace[108] = {
    float3(0, 0, 0), float3(50, 0, 0), float3(50, -50, 0), float3(0, -50, 0), float3(-25, 0, 0), float3(25, 0, 0),
    float3(25, -50, 0), float3(-25, -50, 0), float3(-50, 0, 0), float3(0, 0, 0), float3(0, -50, 0), float3(-50, -50, 0),
    float3(0, 25, 0), float3(50, 25, 0), float3(50, -25, 0), float3(0, -25, 0), float3(-25, 25, 0), float3(25, 25, 0),
    float3(25, -25, 0), float3(-25, -25, 0), float3(-50, 25, 0), float3(0, 25, 0), float3(0, -25, 0), float3(-50, -25, 0),
    float3(0, 50, 0), float3(50, 50, 0), float3(50, 0, 0), float3(0, 0, 0), float3(-25, 50, 0), float3(25, 50, 0),
    float3(25, 0, 0), float3(-25, 0, 0), float3(-50, 50, 0), float3(0, 50, 0), float3(0, 0, 0), float3(-50, 0, 0),
    float3(0, 0, 0), float3(50, 0, 0), float3(50, 0, 50), float3(0, 0, 50), float3(-25, 0, 0), float3(25, 0, 0),
    float3(25, 0, 50), float3(-25, 0, 50), float3(-50, 0, 0), float3(0, 0, 0), float3(0, 0, 50), float3(-50, 0, 50),
    float3(0, 0, -25), float3(50, 0, -25), float3(50, 0, 25), float3(0, 0, 25), float3(-25, 0, -25), float3(25, 0, -25),
    float3(25, 0, 25), float3(-25, 0, 25), float3(-50, 0, -25), float3(0, 0, -25), float3(0, 0, 25), float3(-50, 0, 25),
    float3(0, 0, -50), float3(50, 0, -50), float3(50, 0, 0), float3(0, 0, 0), float3(-25, 0, -50), float3(25, 0, -50),
    float3(25, 0, 0), float3(-25, 0, 0), float3(-50, 0, -50), float3(0, 0, -50), float3(0, 0, 0), float3(-50, 0, 0),
    float3(0, 0, 0), float3(0, 0, 50), float3(0, -50, 50), float3(0, -50, 0), float3(0, 0, -25), float3(0, 0, 25),
    float3(0, -50, 25), float3(0, -50, -25), float3(0, 0, -50), float3(0, 0, 0), float3(0, -50, 0), float3(0, -50, -50),
    float3(0, 25, 0), float3(0, 25, 50), float3(0, -25, 50), float3(0, -25, 0), float3(0, 25, -25), float3(0, 25, 25),
    float3(0, -25, 25), float3(0, -25, -25), float3(0, 25, -50), float3(0, 25, 0), float3(0, -25, 0), float3(0, -25, -50),
    float3(0, 50, 0), float3(0, 50, 50), float3(0, 0, 50), float3(0, 0, 0), float3(0, 50, -25), float3(0, 50, 25),
    float3(0, 0, 25), float3(0, 0, -25), float3(0, 50, -50), float3(0, 50, 0), float3(0, 0, 0), float3(0, 0, -50),
};
static const float2 kQuadCoord[16] = {
    float2(0, 0), float2(1, 0), float2(1, 1), float2(0, 1), float2(0, 0), float2(2, 0), float2(2, 1), float2(0, 1),
    float2(0, 0), float2(1, 0), float2(1, 2), float2(0, 2), float2(0, 0), float2(2, 0), float2(2, 2), float2(0, 2),
};

// A quad's two triangles: corners 0, 1, 2 and 0, 2, 3. A crossed shape's second quad takes the places 72 on.
static const uint kQuadCorner[6] = {0u, 1u, 2u, 0u, 2u, 3u};

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
    const uint corner = kQuadCorner[input.vertex - quad * 6u];
    const float4 p = float4(kQuadPlace[uint(material.params.w) + quad * 72u + corner], 1.0);
    const float2 st = kQuadCoord[uint(material.test.w) + corner];
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
