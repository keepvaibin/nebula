// SPDX-License-Identifier: GPL-3.0-only
// The native materials' framework (E-04): each runtime/src/render/materials/<name>.hlsl includes this, defines
// MaterialConstants (its NebulaNativeMaterial::constants, floats in order), `void material_vertex(NativeVertex v,
// inout NativeVaryings o)` (its texture coordinates) and `float4 material_color(NativeVaryings i)` (its colour, alpha
// tested by the material itself), and is compiled at build for D3D12 (DXIL) and Vulkan (SPIR-V) in the output variants
// below. Bindings are the draw's: b0 the draw constants (draw_constants.h VertexConstants, the fields read here), b1
// the material's constants, t0-t7/s0-s7 its textures, t3 space1 the matrix rows.
//
// Output variants (defines): NATIVE_REACTIVE (SV_Target1: the reactive mask), NATIVE_LAYER (with NATIVE_REACTIVE: the
// effects layer's colour and transmission at SV_Target2-3), NATIVE_MOTION (SV_Target1: the motion vector), NATIVE_SAMPLE
// (inputs per sample: an alpha test decides each sample of a multisampled target), NATIVE_PIXEL_LIT (the eye position and
// normal reach the pixel stage, which lights: j3d_material.hlsli).

cbuffer NativeDraw : register(b0) {
    row_major float4x4 projection : packoffset(c0);  // row-major, as every draw constant (VertexConstants)
    uint4 draw_counts : packoffset(c12);      // num_texgens, num_chans, flags, pos_base
    uint4 draw_rows[3] : packoffset(c4);      // the draw's matrix rows: [0].x position, then coordinate i's at 1 + i
    uint4 draw_indices : packoffset(c13);     // zw: sky_up, sky_up_known
    float4 inline_pos_matrix[3] : packoffset(c14);
    float4 inline_tex_matrices[24] : packoffset(c17);   // texture coordinate i's matrix rows at 3i (the draw's places)
    float4 inline_post_matrices[24] : packoffset(c41);  // and its post-transform rows
    float4 line_point_raster : packoffset(c65);  // the viewport's width and height in pixels (xy), the half line
                                                 // width and half point size (zw, native_lines.hlsl)
    uint4 line_point_tex_offsets : packoffset(c66);  // lines' and points' texture coordinate offsets (native_lines.hlsl)
    uint4 vs_header : packoffset(c67);           // y: the frame scale; z: the PE word (native_ps); w: the shape's
                                                 // per-vertex texture matrix indices (texgen n's bit)
    uint4 vs_post[2] : packoffset(c70);          // texgen i's post-transform row (61 the identity), | 0x100 normalize
    float4 motion_prev_proj[2] : packoffset(c73);
    float4 motion_prev_pos[3] : packoffset(c75);
    float4 motion_params : packoffset(c78);
    uint4 motion_ctrl : packoffset(c79);
    uint4 row_bases : packoffset(c88);
};

cbuffer MaterialBlock : register(b1) {
    MaterialConstants material;
};

Texture2D tex[8] : register(t0);
SamplerState samp[8] : register(s0);

// How much of this pixel lies on the contour where `value` rounds to `level` (both 0-255): a line of the same share of the
// picture at every resolution, its edge antialiased.
float level_line(float value, float level) {
    return saturate((0.5 - abs(value - level)) / max(abs(ddx_fine(value)) + abs(ddy_fine(value)), 0.001) + 0.5);
}

// The draw's matrix rows (draw_matrices.h): position rows from pos_base (place k at 3k), texture rows from
// row_bases.y (rows 30-63), normal rows from row_bases.x, light n's four rows at row_bases.w + 4n.
StructuredBuffer<float4> mtx_rows : register(t3, space1);
float4 native_matrix_row(uint r) { return mtx_rows[r < 30u ? draw_counts.w + r : row_bases.y + (r - 30u)]; }
float4 native_normal_row(uint q) { return mtx_rows[row_bases.x + q]; }
float4 native_light_row(uint n, uint w) { return mtx_rows[row_bases.w + 4u * n + w]; }

// Light n's contribution to a lit channel, in 0-255 units (rgb, and a for an alpha channel): its colour by its
// attenuation (1 specular, 2 directional, 3 spot) and diffuse function (0 none, 1 signed, 2 clamped), at the eye-space
// position and normal. The light rows hold the colour (raw RGBA8 in row 0's w), the cosine and distance attenuation
// coefficients, the position and direction.
float4 j3d_light(uint n, uint atten, uint diffuse, float3 pos, float3 nrm) {
    const uint lcw = asuint(native_light_row(n, 0u).w);
    const float4 le1 = native_light_row(n, 1u);
    const float4 le2 = native_light_row(n, 2u);
    const float4 le3 = native_light_row(n, 3u);
    const float3 lpos = float3(le2.z, le2.w, le3.x);
    const float3 ldvec = float3(le3.y, le3.z, le3.w);
    const float3 cos_a = le1.xyz;
    const float3 dist_a = float3(le1.w, le2.x, le2.y);
    const float4 lcol = float4((lcw >> 24u) & 255u, (lcw >> 16u) & 255u, (lcw >> 8u) & 255u, lcw & 255u);
    const float3 delta = lpos - pos;
    const float dd = dot(delta, delta);
    float3 ldir = dd > 0.0 ? delta * rsqrt(dd) : nrm;
    float attn = 1.0;
    if (atten == 1u) {
        attn = dot(nrm, ldir) >= 0.0 ? min(max(0.0, dot(nrm, ldvec)), 1.0e18) : 0.0;
        const float dad = dot(dist_a, dist_a);
        const float3 dist_attn = diffuse == 0u ? dist_a : (dad > 0.0 ? dist_a * rsqrt(dad) : dist_a);
        const float denom = dot(dist_attn, float3(1.0, attn, attn * attn));
        attn = denom != 0.0 ? max(0.0, dot(cos_a, float3(1.0, attn, attn * attn))) / denom : 0.0;
    } else if (atten == 3u) {
        const float dist = min(sqrt(dd), 1.0e18);
        const float dist2 = min(dd, 1.0e36);
        ldir = dist > 0.0 ? delta / dist : nrm;
        attn = min(max(0.0, dot(ldir, ldvec)), 1.0e18);
        const float denom = dot(dist_a, float3(1.0, dist, dist2));
        attn = denom != 0.0 ? max(0.0, cos_a.x + cos_a.y * attn + cos_a.z * attn * attn) / denom : 0.0;
    }
    const float facing = diffuse == 1u ? dot(ldir, nrm) : diffuse == 2u ? max(0.0, dot(ldir, nrm)) : 1.0;
    return attn * facing * lcol;
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
    return lerp(c, fog_color.rgb, f);
}
float4 native_post_row(uint q) { return mtx_rows[row_bases.z + q]; }
uint native_post(uint i) { return vs_post[i >> 2u][i & 3u]; }

struct NativeVertexIn {
    float3 pos : POSITION;
    float3 nrm : NORMAL;
    float3 tangent : TANGENT;
    float3 binormal : BINORMAL;
    uint color0 : COLOR0;
    uint color1 : COLOR1;
    float2 uv[8] : TEXCOORD;
    uint3 mtx_idx : BLENDINDICES;  // x: position place, then texture coordinates' (draw_matrices.h packing)
    uint3 curve : BEND;            // curved surfaces: displacement and crease normals (packed), the crease place
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
    float2 uv[8];
    uint3 mtx_idx;
    uint pn_row;     // the position matrix's first row
};

#if NATIVE_MOTION
#define NATIVE_MOTION_MEMBERS float4 mv_cur : TEXCOORD12; float4 mv_prev : TEXCOORD13;
#else
#define NATIVE_MOTION_MEMBERS
#endif
#if NATIVE_SAMPLE
#define NATIVE_PER_SAMPLE sample
#else
#define NATIVE_PER_SAMPLE
#endif
#if NATIVE_PIXEL_LIT
#define NATIVE_PIXEL_LIT_MEMBERS NATIVE_PER_SAMPLE float3 lit_eye : TEXCOORD8; NATIVE_PER_SAMPLE float3 lit_nrm : TEXCOORD9;
#else
#define NATIVE_PIXEL_LIT_MEMBERS
#endif
#define NATIVE_VARYING_MEMBERS \
    NATIVE_PIXEL_LIT_MEMBERS \
    float4 pos : SV_Position; NATIVE_PER_SAMPLE float4 color : COLOR0; NATIVE_PER_SAMPLE float4 color1 : COLOR1; NATIVE_PER_SAMPLE float4 uv[8] : TEXCOORD; \
    NATIVE_MOTION_MEMBERS

// What the vertex stage writes and the pixel stage reads.
struct NativeVaryings {
    NATIVE_VARYING_MEMBERS
};

float4 native_unpack_color(uint c) {
    return float4((c >> 24u) & 0xFFu, (c >> 16u) & 0xFFu, (c >> 8u) & 0xFFu, c & 0xFFu) / 255.0;
}

void material_vertex(NativeVertex v, inout NativeVaryings o);

// Texture coordinate i's matrix: the vertex's own index where the shape carries one (vs_header.w bit i: skinned
// shapes' per-vertex texture matrices), else the draw's (draw_rows: the rows the shape loads, as
// refresh_vs_inline_matrices reads them); its rows through native_matrix_row (60: the identity).
uint native_tex_matrix(NativeVertex v, uint i) {
    if (((vs_header.w >> i) & 1u) == 0u) {
        return draw_rows[(i + 1u) >> 2u][(i + 1u) & 3u];
    }
    const uint packed = i < 3u ? v.mtx_idx.x : i < 7u ? v.mtx_idx.y : v.mtx_idx.z;
    const uint shift = i < 3u ? 8u + 8u * i : i < 7u ? 8u * (i - 3u) : 0u;
    return min((packed >> shift) & 0xFFu, 61u);
}
float4 material_color(NativeVaryings i);

// The texture coordinates `mask` names moved by 1 / divisor in x (and in y when `bottom`).
void native_texcoord_offset(inout NativeVaryings v, uint mask, uint divisor, bool right, bool bottom) {
    if (divisor == 0u) {
        return;
    }
    const float offset = 1.0 / float(divisor);
    [unroll] for (uint i = 0u; i < 8u; ++i) {
        if ((mask & (1u << i)) != 0u) {
            v.uv[i].x += right ? offset : 0.0;
            v.uv[i].y += bottom ? offset : 0.0;
        }
    }
}

NativeVaryings native_vs(NativeVertexIn input) {
    NativeVertex v;
    v.pos = input.pos;
    v.nrm = input.nrm;
    v.tangent = input.tangent;
    v.binormal = input.binormal;
    v.mtx_idx = input.mtx_idx;
    // The position matrix: the vertex's place where it carries one (flags bit 0, skinned and multi-matrix models),
    // else the draw's (inline).
    const float4 p = float4(input.pos, 1.0);
    float4 m0, m1, m2;
    if ((draw_counts.z & 1u) != 0u) {
        v.pn_row = min(input.mtx_idx.x & 0x3Fu, 61u);
        m0 = native_matrix_row(v.pn_row);
        m1 = native_matrix_row(v.pn_row + 1u);
        m2 = native_matrix_row(v.pn_row + 2u);
    } else {
        v.pn_row = min(draw_rows[0].x, 61u);
        m0 = inline_pos_matrix[0];
        m1 = inline_pos_matrix[1];
        m2 = inline_pos_matrix[2];
    }
    v.eye = float3(dot(m0, p), dot(m1, p), dot(m2, p));
    const float3 n = float3(dot(m0.xyz, input.nrm), dot(m1.xyz, input.nrm), dot(m2.xyz, input.nrm));
    v.eye_nrm = dot(n, n) > 0.0 ? normalize(n) : n;
    v.color0 = native_unpack_color(input.color0);
    v.color1 = native_unpack_color(input.color1);
    [unroll] for (uint i = 0u; i < 8u; ++i) {
        v.uv[i] = input.uv[i];
    }
    NativeVaryings o = (NativeVaryings)0;
    o.pos = mul(projection, float4(v.eye, 1.0));
#if NATIVE_MOTION
    // Motion vectors (temporal scenes): this frame's clip position without the jitter, and the previous frame's (the
    // camera delta applied to the eye position).
    float3 prev_eye;
    if ((draw_counts.z & 1u) != 0u && motion_ctrl.x == 0u) {
        // The vertex's position rows of the previous frame (motion_ctrl.y: their base, or kMotionOwnRows).
        const uint base = motion_ctrl.y;
        const uint r = v.pn_row;
        prev_eye = float3(dot(base == 0xFFFFFFFFu ? native_matrix_row(r) : mtx_rows[base + r], p),
                          dot(base == 0xFFFFFFFFu ? native_matrix_row(r + 1u) : mtx_rows[base + r + 1u], p),
                          dot(base == 0xFFFFFFFFu ? native_matrix_row(r + 2u) : mtx_rows[base + r + 2u], p));
    } else {
        // The camera's move applied to the eye position (palette draws), or the previous position matrix (inline).
        const float4 q = (draw_counts.z & 1u) != 0u ? float4(v.eye, 1.0) : p;
        prev_eye = float3(dot(motion_prev_pos[0], q), dot(motion_prev_pos[1], q), dot(motion_prev_pos[2], q));
    }
    const float4 pp0 = motion_prev_proj[0];
    const float3 prev_clip = motion_prev_proj[1].z == 0.0
        ? float3(pp0.x * prev_eye.x + pp0.y * prev_eye.z, pp0.z * prev_eye.y + pp0.w * prev_eye.z, -prev_eye.z)
        : float3(pp0.x * prev_eye.x + pp0.y, pp0.z * prev_eye.y + pp0.w, 1.0);
    o.mv_cur = float4(o.pos.xy - motion_params.xy * o.pos.w, motion_params.z, o.pos.w);
    o.mv_prev = float4(prev_clip.xy, motion_params.w, prev_clip.z);  // .w the previous clip w
#endif
    material_vertex(v, o);
    // A corner of a widened point or line (render_vertex.h kVertexWiden*): moved out by the draw's size in pixels. A line
    // widens across its major axis (a steep one across x, a flat one across y); the second edge's coordinates move.
    const uint widen = (input.mtx_idx.z >> 8u) & 15u;
    if (widen != 0u) {
        const bool right = (widen & 4u) != 0u;
        const bool bottom = (widen & 8u) != 0u;
        if ((widen & 1u) != 0u) {
            const float2 signs = float2(right ? 1.0 : -1.0, bottom ? -1.0 : 1.0);
            o.pos.xy += signs * line_point_raster.ww / line_point_raster.xy * o.pos.w;
            native_texcoord_offset(o, line_point_tex_offsets.y, line_point_tex_offsets.w, right, bottom);
        } else {
            const float4 q = float4(input.tangent, 1.0);
            float4 other_eye;
            if ((draw_counts.z & 1u) != 0u) {
                const uint r = min(input.curve.x & 0x3Fu, 61u);
                other_eye = float4(dot(native_matrix_row(r), q), dot(native_matrix_row(r + 1u), q),
                                   dot(native_matrix_row(r + 2u), q), 1.0);
            } else {
                other_eye = float4(dot(m0, q), dot(m1, q), dot(m2, q), 1.0);
            }
            const float4 other = mul(projection, other_eye);
            const float2 delta = abs(o.pos.xy / o.pos.w - other.xy / other.w);
            const bool tall = line_point_raster.y * delta.y > line_point_raster.x * delta.x;
            const float2 offset = tall ? float2(line_point_raster.z / line_point_raster.x, 0.0)
                                       : float2(0.0, -line_point_raster.z / line_point_raster.y);
            o.pos.xy += (right ? offset : -offset) * o.pos.w;
            native_texcoord_offset(o, line_point_tex_offsets.x, line_point_tex_offsets.z, right, false);
        }
    }
    return o;
}

#if NATIVE_LAYER
struct NativeOut { float4 color : SV_Target0; float reactive : SV_Target1; float4 layer : SV_Target2;
                   float4 trans : SV_Target3; };
#elif NATIVE_REACTIVE
struct NativeOut { float4 color : SV_Target0; float reactive : SV_Target1; };
#elif NATIVE_MOTION
struct NativeOut { float4 color : SV_Target0; float2 motion : SV_Target1; };
#elif NATIVE_DUAL
struct NativeOut { float4 color : SV_Target0; float4 color1 : SV_Target1; };
#else
struct NativeOut { float4 color : SV_Target0; };
#endif

NativeOut native_ps(NativeVaryings i) {
    // The material's colour as it is; the target rounds it once. A constant destination alpha replaces the alpha
    // written; the blender reads the material's alpha (SV_Target1 for dual-source blending).
#if NATIVE_PIXEL_LIT
    // A wide scene target keeps what the enhanced lighting adds past white; an 8-bit one rounds it to white.
    const float4 lit_color = material_color(i);
    const float4 c0 = float4(max(lit_color.rgb, 0.0), saturate(lit_color.a));
#else
    const float4 c0 = saturate(material_color(i));
#endif
    const uint pe = vs_header.z;
    const float stage_alpha = c0.a;
    float4 c = c0;
#if !NATIVE_REACTIVE
    if ((pe & 0x100u) != 0u) {
        c.a = float((pe >> 9u) & 0xFFu) / 255.0;
    }
#endif
    NativeOut o;
    o.color = c;
#if NATIVE_DUAL
    o.color1 = float4(0.0, 0.0, 0.0, stage_alpha);
#endif
#if NATIVE_REACTIVE
    o.reactive = min(saturate(c.a), 0.9);
#endif
#if NATIVE_LAYER
    o.layer = c;
    o.trans = c;
#endif
#if NATIVE_MOTION
    // The move since the previous frame in pixels (mv_cur.z and mv_prev.z: the half viewport).
    o.motion = float2(0.0, 0.0);
    if (i.mv_cur.w > 0.0 && i.mv_prev.w > 0.0) {
        o.motion = (i.mv_prev.xy / i.mv_prev.w - i.mv_cur.xy / i.mv_cur.w) * float2(i.mv_cur.z, i.mv_prev.z);
    }
#endif
    return o;
}
