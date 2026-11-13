// SPDX-License-Identifier: GPL-3.0-only
// A written material: runtime/src/render/materials/<name>.hlsl includes this, defines MaterialConstants (its site's
// SiteMaterial::constants, floats in order), `void material_vertex(NativeVertex v, inout NativeVaryings o)` (its texture
// coordinates) and `float4 material_color(NativeVaryings i)` (its colour, alpha tested by the material itself). Bindings
// and output variants are every program's (program.hlsli); b1 holds the material in both stages. NATIVE_WIDE: the vertices
// are corners a site's line or point became. NATIVE_PARTICLE: the corners place themselves by the presented frame's time.
// NATIVE_OWN_VERTEX: the includer writes its own vertex stage (particle_instances.hlsl).
// NATIVE_COORDS (set by the includer before this header): how many texture coordinates the material reads and its
// varyings carry. Any number: the device's own interpolator count is the limit.
//
// The coordinates come first (TEXCOORD0 up), the motion varyings after them (TEXCOORD NATIVE_COORDS and the next), as a
// generated program's: the prepass's pixel stage (no motion varyings) reads the motion form's first outputs, so a motion
// draw's prepass (its vertex stage's motion form) links.

#include "program.hlsli"
// NATIVE_LIT (set by a written material that sums the game's lights in its vertex stage: fur, the Mii face, the
// trample star): its traced form (NATIVE_TRACED, a vertex form of its own) carries where the vertex is, its normal,
// its light set and its colour 0 to the pixel stage, which sums the same lights again, each by its share
// (program_lit.hlsli: light_term by traced_light, the ambient by AMBIENT_TERM), through material_relight
// (RT-MASTER-PLAN 14.3 item 4). Its other forms are as they were.
#if NATIVE_LIT
#include "program_lit.hlsli"
#else
#define AMBIENT_TERM(x) (x)
#endif

#ifndef NATIVE_COORDS
#error a written material defines NATIVE_COORDS before including native_material.hlsli
#endif
#if NATIVE_COORDS < 1
#error NATIVE_COORDS is at least 1
#endif

#if NATIVE_COORDS == 1
#define NEBULA_MOTION_CURRENT_INDEX 1
#define NEBULA_MOTION_PREVIOUS_INDEX 2
#elif NATIVE_COORDS == 2
#define NEBULA_MOTION_CURRENT_INDEX 2
#define NEBULA_MOTION_PREVIOUS_INDEX 3
#elif NATIVE_COORDS == 3
#define NEBULA_MOTION_CURRENT_INDEX 3
#define NEBULA_MOTION_PREVIOUS_INDEX 4
#elif NATIVE_COORDS == 4
#define NEBULA_MOTION_CURRENT_INDEX 4
#define NEBULA_MOTION_PREVIOUS_INDEX 5
#elif NATIVE_COORDS == 5
#define NEBULA_MOTION_CURRENT_INDEX 5
#define NEBULA_MOTION_PREVIOUS_INDEX 6
#elif NATIVE_COORDS == 6
#define NEBULA_MOTION_CURRENT_INDEX 6
#define NEBULA_MOTION_PREVIOUS_INDEX 7
#else
#error a written material reads at most 6 texture coordinates (the motion varyings take the next two of 8)
#endif
#define NATIVE_UV_SEMANTIC TEXCOORD0

cbuffer MaterialBlock : register(b2) {
    MaterialConstants material;
};

// How much of this pixel lies on the contour where `value` rounds to `level` (both 0-255): a line of the same share of the
// picture at every resolution, its edge antialiased.
float level_line(float value, float level) {
    return saturate((0.5 - abs(value - level)) / max(abs(ddx_fine(value)) + abs(ddy_fine(value)), 0.001) + 0.5);
}

static uint g_light_base;
float4 native_position_row(uint r) { return rows[r]; }
float4 native_normal_row(uint r) { return rows[r + 3u]; }
// Row w of light n, or zero when the set holds no light n.
float4 native_light_row(uint n, uint w) { return n < light_count(g_light_base) ? rows[light_row(g_light_base, n) + w] : float4(0.0, 0.0, 0.0, 0.0); }
uint native_light_count() { return light_count(g_light_base); }
float4 channel_light(uint n, uint atten, uint diffuse, float3 pos, float3 nrm) {
    return light_term(g_light_base, n, atten, diffuse, pos, nrm);
}

// fog over the colour, from its values (fog: start, end, near, far; fog_color: rgb, w the fog type): its type (fog_color.w: perspective 2-7 by the eye depth, orthographic 10-15
// by the depth between near and far), the range start-end, its curve.
float3 native_fog(float3 c, float4 pos, float4 fog, float4 fog_color) {
    const uint type = uint(fog_color.w);
    if (type == 0u) {
        return c;
    }
    const float ze = (type & 8u) != 0u ? lerp(fog.z, fog.w, pos.z) : pos.w;
    float f = saturate((ze - fog.x) / max(fog.y - fog.x, 1.0e-6));
    switch (type & 7u) {
    case 4u: f = 1.0 - exp2(-8.0 * f); break;
    case 5u: f = 1.0 - exp2(-8.0 * f * f); break;
    case 6u: f = exp2(-8.0 * (1.0 - f)); break;
    case 7u: f = 1.0 - f; f = exp2(-8.0 * f * f); break;
    default: break;
    }
    // A traced form fogs after its light, the fog lit by the traced share (program.hlsli, 15.3; FB b57).
    return program_fog_curve(c, fog_color.rgb, f, float4(fog.x, fog.y, ze, float(type)));
}

#if NATIVE_LIT && NATIVE_TRACED
#if NATIVE_COORDS > 2
#error the light varyings of a lit written material follow the motion pair and at most two coordinates (TEXCOORD4-7)
#endif
#define NATIVE_LIT_MEMBERS PER_SAMPLE float3 lit_eye : TEXCOORD4; PER_SAMPLE float3 lit_nrm : TEXCOORD5; \
    nointerpolation uint lit_light : TEXCOORD6; PER_SAMPLE float4 lit_color0 : TEXCOORD7;
#else
#define NATIVE_LIT_MEMBERS
#endif

#define NATIVE_VARYING_MEMBERS \
    float4 pos : SV_Position; PER_SAMPLE float4 color : COLOR0; PER_SAMPLE float4 color1 : COLOR1; PER_SAMPLE float4 uv[NATIVE_COORDS] : NATIVE_UV_SEMANTIC; \
    VARYINGS_MOTION NATIVE_LIT_MEMBERS

// What the vertex stage writes and the pixel stage reads.
struct NativeVaryings {
    NATIVE_VARYING_MEMBERS
};

float4 material_color(NativeVaryings i);

#if !NATIVE_OWN_VERTEX
struct NativeVertexIn {
    float3 pos : POSITION;
    float3 nrm : NORMAL;
#if NATIVE_WIDE
    float3 other : TANGENT;    // a widened line corner's other end
    uint widen : BLENDINDICES1; // 1 a point, 2 a line; 4 its right edge, 8 its bottom edge
#else
    float3 tangent : TANGENT;
#endif
    float3 binormal : BINORMAL;
    float4 color0 : COLOR0;
    float4 color1 : COLOR1;
    float2 uv[NATIVE_COORDS] : TEXCOORD;
    uint draw_matrix : BLENDINDICES0;
#if NATIVE_MOTION
    float3 before : POSITION1;  // where the vertex was a frame ago
#endif
    uint instance : SV_InstanceID;
};

// What a material's vertex function sees: the vertex in the space its draw sent it (object), its position in the eye
// space of the draw's position matrix, its colours (0-1) and texture coordinates.
struct NativeVertex {
    float3 pos;
    float3 eye;
    float3 nrm;      // as sent (object space)
    float3 eye_nrm;  // in eye space (the position matrix's turn; unit length or zero)
    float3 tangent;
    float3 binormal;
    float4 color0;
    float4 color1;
    float2 uv[NATIVE_COORDS];
    uint pn_row;     // the position matrix's first row
};

void material_vertex(NativeVertex v, inout NativeVaryings o);

// The vertex's normal in eye space through its draw's normal rows (unit length, or zero): what a lit material lights.
float3 native_lit_normal(NativeVertex v) {
    const uint nq = v.pn_row;
    const float3 nt = float3(dot(native_normal_row(nq).xyz, v.nrm), dot(native_normal_row(nq + 1u).xyz, v.nrm),
                             dot(native_normal_row(nq + 2u).xyz, v.nrm));
    return dot(nt, nt) > 0.0 ? normalize(nt) : nt;
}

#if NATIVE_WIDE
// A spreading line's or point's texture coordinates move a whole texel-space unit on its far edges.
void native_texcoord_spread(inout NativeVaryings v, bool right, bool bottom) {
    [unroll] for (uint i = 0u; i < (NATIVE_COORDS < 3 ? NATIVE_COORDS : 3); ++i) {
        v.uv[i].x += right ? 1.0 : 0.0;
        v.uv[i].y += bottom ? 1.0 : 0.0;
    }
}
#endif

NativeVaryings native_vs(NativeVertexIn input) {
    NativeVertex v;
    v.pos = input.pos;
    v.nrm = input.nrm;
    v.binormal = input.binormal;
#if !NATIVE_WIDE
    v.tangent = input.tangent;
#else
    v.tangent = float3(0.0, 0.0, 0.0);
#endif
#if NATIVE_PARTICLE
    // A particle's corner: where it was a game frame ago (in the normal's place) and where it is, at this frame's time.
    const float3 here = lerp(input.nrm, input.pos, view_scale.y);
    const float3 before = lerp(input.nrm, input.pos, view_scale.z);
#elif NATIVE_SHELL && !NATIVE_WIDE
    // Fur: a shell's corner along its growth, by its length; a corner with no fur sinks under the skin.
    const float3 here = input.pos + input.binormal * (input.tangent.x == 0.0 ? -1.0 : material.shell.x * input.tangent.x);
    const float3 before = here;
#else
    const float3 here = input.pos;
#if NATIVE_MOTION
    const float3 before = input.before;
#else
    const float3 before = input.pos;
#endif
#endif
    const Place place = native_place_moved(here, before, input.draw_matrix, input.instance);
    g_light_base = place.light;
    v.pn_row = place.row;
    v.eye = place.eye;
    const float3 n = row_times_direction(place.row, input.nrm);
    v.eye_nrm = dot(n, n) > 0.0 ? normalize(n) : n;
    v.color0 = input.color0;
    v.color1 = input.color1;
    [unroll] for (uint i = 0u; i < NATIVE_COORDS; ++i) {
        v.uv[i] = input.uv[i];
    }
    NativeVaryings o = (NativeVaryings)0;
    PLACE_OUT(o, place);
    material_vertex(v, o);
#if NATIVE_LIT && NATIVE_TRACED
    o.lit_eye = v.eye;
    o.lit_nrm = native_lit_normal(v);
    o.lit_light = place.light;
    o.lit_color0 = v.color0;
#endif
#if NATIVE_WIDE
    // A corner of a widened point or line: moved out by the draw's size in pixels. A line widens across its major axis
    // (a steep one across x, a flat one across y); the second edge's coordinates move.
    const bool right = (input.widen & 4u) != 0u;
    const bool bottom = (input.widen & 8u) != 0u;
    if ((input.widen & 1u) != 0u) {
        const float2 signs = float2(right ? 1.0 : -1.0, bottom ? -1.0 : 1.0);
        o.pos.xy += signs * draw_half_point / line_point_raster.xy * o.pos.w;
        if ((draw_flags & 2u) != 0u) {
            native_texcoord_spread(o, right, bottom);
        }
    } else {
        const float4 other = mul(projection, float4(row_times_point(place.row, float4(input.other, 1.0)), 1.0));
        const float2 delta = abs(o.pos.xy / o.pos.w - other.xy / other.w);
        const bool tall = line_point_raster.y * delta.y > line_point_raster.x * delta.x;
        const float2 offset = tall ? float2(draw_half_line / line_point_raster.x, 0.0)
                                   : float2(0.0, -draw_half_line / line_point_raster.y);
        o.pos.xy += (right ? offset : -offset) * o.pos.w;
        if ((draw_flags & 1u) != 0u) {
            native_texcoord_spread(o, right, false);
        }
    }
#endif
    return o;
}
#endif

#if NATIVE_LIT && NATIVE_TRACED
// A lit written material's lights at the pixel (`nrm` unit length or zero): its varyings' lit colours set again; true
// when it lit them (false: none of its channels is lit here, its colour is painted).
bool material_relight(inout NativeVaryings i, float3 nrm);
#endif

NativeOut native_ps(NativeVaryings i) {
#if NATIVE_LIT && NATIVE_TRACED
    // The material's lights summed again here, each by how much of it reaches the pixel (the light pass's shares).
    if ((view_flags & 8u) != 0u) {
        g_light_base = i.lit_light;
        g_traced = traced_at(i.pos);
        bool coloured = false;
        g_traced_o = traced_o(g_traced, i.pos, coloured);  // O a channel at a time (FB 22.10), as enh_begin_traced
        traced_o_held();  // FB b96
        g_traced_lit = material_relight(i, dot(i.lit_nrm, i.lit_nrm) > 0.0 ? normalize(i.lit_nrm) : i.lit_nrm);
    }
#endif
    // The material's colour as it is; the target rounds it once.
    const float4 c = saturate(material_color(i));
    PROGRAM_RETURN(c);
}
