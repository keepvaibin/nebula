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

struct NativeVertexIn {
    float3 pos : POSITION;
    float3 nrm : NORMAL;
    uint color0 : COLOR0;
    uint color1 : COLOR1;
    float2 uv[8] : TEXCOORD;
};

// What a material's vertex function sees: the vertex in the space its draw sent it (object), its position in the eye
// space of the draw's position matrix, its colours (0-1) and texture coordinates.
struct NativeVertex {
    float3 pos;
    float3 eye;
    float3 nrm;
    float4 color0;
    float4 color1;
    float2 uv[8];
};

struct NativeVaryings {
    float4 pos : SV_Position;
    float4 color : COLOR0;
    float4 uv[4] : TEXCOORD;
#if NATIVE_MOTION
    float4 mv_cur : TEXCOORD12;
    float4 mv_prev : TEXCOORD13;
#endif
};

float4 native_unpack_color(uint c) {
    return float4((c >> 24u) & 0xFFu, (c >> 16u) & 0xFFu, (c >> 8u) & 0xFFu, c & 0xFFu) / 255.0;
}

void material_vertex(NativeVertex v, inout NativeVaryings o);
float4 material_color(NativeVaryings i);

NativeVaryings native_vs(NativeVertexIn input) {
    NativeVertex v;
    v.pos = input.pos;
    v.nrm = input.nrm;
    // The draw's position matrix (its current place; immediate draws carry none per vertex).
    const float4 p = float4(input.pos, 1.0);
    v.eye = float3(dot(inline_pos_matrix[0], p), dot(inline_pos_matrix[1], p), dot(inline_pos_matrix[2], p));
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
    const float3 prev_eye = float3(dot(motion_prev_pos[0], p), dot(motion_prev_pos[1], p), dot(motion_prev_pos[2], p));
    const float4 pp0 = motion_prev_proj[0];
    const float3 prev_clip = motion_prev_proj[1].z == 0.0
        ? float3(pp0.x * prev_eye.x + pp0.y * prev_eye.z, pp0.z * prev_eye.y + pp0.w * prev_eye.z, -prev_eye.z)
        : float3(pp0.x * prev_eye.x + pp0.y, pp0.z * prev_eye.y + pp0.w, 1.0);
    o.mv_cur = float4(o.pos.xy - motion_params.xy * o.pos.w, motion_params.z, o.pos.w);
    o.mv_prev = float4(prev_clip.xy, motion_params.w, prev_clip.z);  // .w the previous clip w
#endif
    material_vertex(v, o);
    return o;
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
