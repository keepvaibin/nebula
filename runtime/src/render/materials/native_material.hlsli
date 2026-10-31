// SPDX-License-Identifier: GPL-3.0-only
// The native materials' framework (E-04): each runtime/src/render/materials/<name>.hlsl includes this, defines
// MaterialConstants (its NebulaNativeMaterial::constants, floats in order), `void material_vertex(NativeVertex v,
// inout NativeVaryings o)` (its texture coordinates) and `float4 material_color(NativeVaryings i)` (its colour, alpha
// tested by the material itself), and is compiled at build for D3D12 (DXIL) and Vulkan (SPIR-V) in the output variants
// below. Bindings are the draw's: b0 the draw constants (uber_constants.h VertexConstants, the fields read here), b1
// the material's constants, t0-t7/s0-s7 its textures, t3 space1 the matrix rows.
//
// Output variants (defines): NATIVE_REACTIVE (SV_Target1: the reactive mask), NATIVE_LAYER (with NATIVE_REACTIVE: the
// effects layer's colour and transmission at SV_Target2-3), NATIVE_MOTION (SV_Target1: the motion vector).

cbuffer NativeDraw : register(b0) {
    row_major float4x4 projection : packoffset(c0);  // row-major, as every draw constant (VertexConstants)
    uint4 draw_counts : packoffset(c12);      // num_texgens, num_chans, flags, pos_base
    uint4 draw_indices : packoffset(c13);     // matrix_index_a, matrix_index_b, sky_up, sky_up_known
    float4 inline_pos_matrix[3] : packoffset(c14);
    float4 inline_tex_matrices[24] : packoffset(c17);   // texture coordinate i's matrix rows at 3i (the draw's places)
    float4 inline_post_matrices[24] : packoffset(c41);  // and its post-transform rows
    float4 line_point_raster : packoffset(c65);  // the viewport's width and height in pixels (xy)
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

// The draw's matrix rows (draw_matrices.h): position rows from pos_base (place k at 3k), texture rows from
// row_bases.y (rows 30-63), normal rows from row_bases.x, light n's four rows at row_bases.w + 4n.
StructuredBuffer<float4> mtx_rows : register(t3, space1);
float4 native_matrix_row(uint r) { return mtx_rows[r < 30u ? draw_counts.w + r : row_bases.y + (r - 30u)]; }
float4 native_normal_row(uint q) { return mtx_rows[row_bases.x + q]; }
float4 native_light_row(uint n, uint w) { return mtx_rows[row_bases.w + 4u * n + w]; }

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
#define NATIVE_VARYING_MEMBERS \
    float4 pos : SV_Position; float4 color : COLOR0; float4 color1 : COLOR1; float4 uv[8] : TEXCOORD; \
    NATIVE_MOTION_MEMBERS

// What the pixel stage reads (and the curved surfaces' domain stage writes).
struct NativeVaryings {
    NATIVE_VARYING_MEMBERS
};

// The vertex stage's output: the same, and the curved surfaces' data for their patch stages (native_curved.hlsl): the
// eye position and the displacement normal (w: the jitter, zero here), the crease normal packed and its places.
struct NativeVsOut {
    NATIVE_VARYING_MEMBERS
    float4 curve_pos : TEXCOORD14;
    float4 curve_nrm : TEXCOORD15;
    uint4 curve_data : TEXCOORD16;
};

// Curved surfaces' packed normals (curved_surfaces.h pack_normal): octahedral, two 16-bit halves, zero for none.
float3 curve_unpack(uint packed) {
    if (packed == 0u) {
        return float3(0.0, 0.0, 0.0);
    }
    const float2 q = (float2(float(packed >> 16u), float(packed & 0xFFFFu)) - 32768.0) / 32767.0;
    float3 n = float3(q, 1.0 - abs(q.x) - abs(q.y));
    if (n.z < 0.0) {
        n.xy = (1.0 - abs(q.yx)) * float2(q.x >= 0.0 ? 1.0 : -1.0, q.y >= 0.0 ? 1.0 : -1.0);
    }
    return normalize(n);
}
uint curve_pack(float3 n) {
    const float sum = abs(n.x) + abs(n.y) + abs(n.z);
    if (!(sum > 0.0)) {
        return 0u;
    }
    float2 q = n.xy / sum;
    if (n.z < 0.0) {
        q = (1.0 - abs(q.yx)) * float2(q.x >= 0.0 ? 1.0 : -1.0, q.y >= 0.0 ? 1.0 : -1.0);
    }
    const uint2 h = uint2(round(clamp(q, -1.0, 1.0) * 32767.0) + 32768.0);
    return (h.x << 16u) | h.y;
}

float4 native_unpack_color(uint c) {
    return float4((c >> 24u) & 0xFFu, (c >> 16u) & 0xFFu, (c >> 8u) & 0xFFu, c & 0xFFu) / 255.0;
}

void material_vertex(NativeVertex v, inout NativeVaryings o);
float4 material_color(NativeVaryings i);

NativeVsOut native_vs(NativeVertexIn input) {
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
        v.pn_row = min(draw_indices.x & 0x3Fu, 61u);
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
    NativeVsOut out_ = (NativeVsOut)0;
    out_.pos = o.pos;
    out_.color = o.color;
    out_.color1 = o.color1;
    [unroll] for (uint k = 0u; k < 8u; ++k) {
        out_.uv[k] = o.uv[k];
    }
#if NATIVE_MOTION
    out_.mv_cur = o.mv_cur;
    out_.mv_prev = o.mv_prev;
#endif
    // Curved surfaces: the normals turned as normals are, by the position matrix's cofactors.
    const float3x3 cofactor = float3x3(cross(m1.xyz, m2.xyz), cross(m2.xyz, m0.xyz), cross(m0.xyz, m1.xyz));
    float3 cn = mul(cofactor, curve_unpack(input.curve.x));
    float3 cc = mul(cofactor, curve_unpack(input.curve.y));
    cn = dot(cn, cn) > 0.0 ? normalize(cn) : cn;
    cc = dot(cc, cc) > 0.0 ? normalize(cc) : cc;
    out_.curve_pos = float4(v.eye, 0.0);
    out_.curve_nrm = float4(cn, 0.0);
    out_.curve_data = uint4(curve_pack(cc), input.curve.z, input.mtx_idx.z >> 16u, 0u);
    return out_;
}

#if NATIVE_LAYER
struct NativeOut { float4 color : SV_Target0; float reactive : SV_Target1; float4 layer : SV_Target2;
                   float4 trans : SV_Target3; };
#elif NATIVE_REACTIVE
struct NativeOut { float4 color : SV_Target0; float reactive : SV_Target1; };
#elif NATIVE_MOTION
struct NativeOut { float4 color : SV_Target0; float2 motion : SV_Target1; };
#else
struct NativeOut { float4 color : SV_Target0; };
#endif

NativeOut native_ps(NativeVaryings i) {
    float4 c = material_color(i);
    // The EFB holds eight bits a colour channel and six of alpha (a reactive draw blends at the eight-bit alpha).
    const uint4 efb = uint4(round(saturate(c) * 255.0));
    c.rgb = float3(efb.rgb) / 255.0;
#if NATIVE_REACTIVE
    c.a = float(efb.a) / 255.0;
#else
    c.a = float(efb.a >> 2u) / 63.0;
#endif
    NativeOut o;
    o.color = c;
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
