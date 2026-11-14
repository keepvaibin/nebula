// SPDX-License-Identifier: GPL-3.0-only
// Particles (Game/Particle: ParticleShape, ParticleResource::setPTev/setCTev): the particle texture (0, or 1
// for child particles; its lookup moved by the indirect texture 2 when the emitter has one) combined with the
// emitter's prm and env colours (each particle's or corner's, from the vertex stage) by its colour mode, alpha the
// texture's by prm's or prm's alone; a second texture (3) multiplied in; the shape's alpha test; fog. The colour
// registers below are not written (zero) and not read.
struct MaterialConstants {
    float4 reg[4];        // the colour registers (layout kept; unused)
    float4 kcolor[4];
    float4 mat_color[2];
    float4 amb_color[2];
    float4 ind_mtx[6];    // the indirect matrices (rows 2m, 2m + 1; their scale applied)
    float4 fog;
    float4 fog_color;
    float4 alpha_ref;     // the alpha test's references (0-1)
    float4 params;        // x: the colour mode (ParticleShapeData::mColorMode 0-5), y: the alpha mode (0-1), z: flags (1
                          // indirect, 2 second texture, 4 projected coordinates, 8 child: texture 1), w: an instanced
                          // draw's first corner place (particle_instances.hlsl kQuadPlace)
    float4 test;          // x, y: the alpha test's compares 0 and 1, z: its logic (and, or, xor, xnor), w: an instanced
                          // draw's first corner coordinate (kQuadCoord)
};

#define NATIVE_PARTICLE 1
#define NATIVE_COORDS 3
#include "native_material.hlsli"

// What the vertex stage hands on: coordinate 0 final (with its projection's q in z), 1 the corner's own; prm and env.
void particle_out(float2 uv, float q, float2 corner_uv, float4 prm, float4 env, inout NativeVaryings o) {
    o.color = prm;
    o.uv[0] = float4(uv, q, 1.0);
    o.uv[1] = float4(corner_uv, 1.0, 0.0);
    o.color1 = env;
}

#if !NATIVE_OWN_VERTEX
// Corners the game made (stripes, lines, points): coordinate 2 holds the projection's q.
void material_vertex(NativeVertex v, inout NativeVaryings o) {
    particle_out(v.uv[0], v.uv[2].x, v.uv[1], v.color0, v.color1, o);
}
#endif

bool particle_compare(uint op, float a8, float r8) {
    switch (op) {
    case 0u: return false;
    case 1u: return a8 < r8;
    case 2u: return a8 == r8;
    case 3u: return a8 <= r8;
    case 4u: return a8 > r8;
    case 5u: return a8 != r8;
    case 6u: return a8 >= r8;
    default: return true;
    }
}

float4 material_color(NativeVaryings i) {
    const uint flags = uint(material.params.z);
    float2 uv = i.uv[0].xy;
    if ((flags & 4u) != 0u && i.uv[0].z != 0.0) {
        uv /= i.uv[0].z;
    }
    if ((flags & 1u) != 0u) {
        // the game's indirect lookup: the indirect texture's alpha, blue, green less 128 by indirect matrix 0, in texels.
        uint w, h;
        TEX(0).GetDimensions(w, h);
        const float3 sv = round(TEX(2).Sample(SAMP(2), i.uv[1].xy).abg * 255.0) - 128.0;
        uv += float2(dot(material.ind_mtx[0].xyz, sv), dot(material.ind_mtx[1].xyz, sv)) / float2(w, h);
    }
    const float4 t = (flags & 8u) != 0u ? TEX(1).Sample(SAMP(1), uv) : TEX(0).Sample(SAMP(0), uv);
    const float3 prm = i.color.rgb;
    const float3 env = i.color1.rgb;
    float3 c;
    switch (uint(material.params.x)) {
    case 0u: c = t.rgb; break;
    case 1u: c = prm * t.rgb; break;
    case 2u: c = lerp(prm, float3(1.0, 1.0, 1.0), t.rgb); break;
    case 3u: c = lerp(env, prm, t.rgb); break;
    case 4u: c = env + prm * t.rgb; break;
    default: c = prm; break;
    }
    c = saturate(c);
    float a = uint(material.params.y) == 0u ? t.a * i.color.a : i.color.a;
    if ((flags & 2u) != 0u) {
        const float4 t3 = TEX(3).Sample(SAMP(3), i.uv[1].xy);
        c *= t3.rgb;
        a *= t3.a;
    }
    const float a8 = round(a * 255.0);
    const bool p0 = particle_compare(uint(material.test.x), a8, round(material.alpha_ref.x * 255.0));
    const bool p1 = particle_compare(uint(material.test.y), a8, round(material.alpha_ref.y * 255.0));
    const uint logic = uint(material.test.z);
    const bool passed = logic == 0u ? (p0 && p1) : logic == 1u ? (p0 || p1) : logic == 2u ? (p0 != p1) : (p0 == p1);
    if (!passed) {
        discard;
    }
    c = native_fog(c, i.pos, material.fog, material.fog_color);
    return float4(c, a);
}
