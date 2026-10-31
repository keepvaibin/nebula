// The material vertex shader: one source, compiled when Nebula is built (material_shaders.h lists the variants). A
// draw's vertex program (its texture coordinate generators, colour channels and matrix slots) is data in
// VertexConstants (apply_uber_vs_key's fields), read here per draw; the variant fixes only the shader's interface: the
// lighting outputs, motion, curved surfaces and interpolated redraw's vertex blending.
//
// Variant defines (0 or 1): MAT_LIGHT_OUT per-pixel or enhanced lighting's outputs, MAT_MOTION motion vectors,
// MAT_CURVED curved surfaces' outputs, MAT_BLEND interpolated redraw's vertex blending.
//
// Behaviour references (clean-room, behaviour only): texture coordinates, post-matrices, q == 0: Dolphin
// VertexShaderGen.cpp; lighting: Dolphin LightingShaderGen.cpp.

#ifndef MAT_LIGHT_OUT
#define MAT_LIGHT_OUT 0
#endif
#ifndef MAT_MOTION
#define MAT_MOTION 0
#endif
#ifndef MAT_CURVED
#define MAT_CURVED 0
#endif
#ifndef MAT_BLEND
#define MAT_BLEND 0
#endif

// VertexConstants (uber_constants.h), member for member.
cbuffer VertexConstants : register(b0) {
    float4x4 projection;
    uint4    texgen_config[4];
    float4   chan_ambient[2];
    float4   chan_material[2];
    uint     num_texgens;
    uint     num_chans;
    uint     flags;
    uint     pos_base;
    uint     matrix_index_a;
    uint     matrix_index_b;
    uint     sky_up;
    uint     sky_up_known;
    float4   inline_pos_matrix[3];
    float4   inline_tex_matrices[24];
    float4   inline_post_matrices[24];
    float4   line_point_raster;
    uint4    line_point_tex_offsets;
    // The vertex program: x coordinates, y channels, z key flags, w matrix slots | features << 8.
    uint4    program_header;
    uint4    program_coords[2];
    uint4    program_post[2];
    uint4    program_channels;
    // A..D, then E, F, type (0 perspective) of the draw's previous projection.
    float4   motion_prev_proj[2];
    // Previous position matrix (inline draws), or the camera delta that maps a current eye-space position to its
    // previous one.
    float4   motion_prev_pos[3];
    // xy: this draw's projection jitter (NDC); zw: half viewport, pixels.
    float4   motion_params;
    // x: 0 previous palette rows at y, 1 motion_prev_pos * current eye.
    uint4    motion_ctrl;
    // Physical ray tracing (flags bit 6): the model's baked light by facing.
    float4   debake_sh[7];
    float4   debake_peak;
    // The first rows of the draw's normal, texture, post-transform and light blocks (draw_matrices.h).
    uint4    row_bases;
};
StructuredBuffer<float4> mtx_rows : register(t3, space1);  // the matrix ring, or a replay's output
// Position and texture rows 0-63 (positions from pos_base, textures from row_bases.y), normal rows from row_bases.x,
// light n's four rows at row_bases.w + 4n.
float4 matrix_row(uint r) { return mtx_rows[r < 30u ? pos_base + r : row_bases.y + (r - 30u)]; }
float4 normal_row(uint q) { return mtx_rows[row_bases.x + q]; }
float4 light_row(uint n, uint w) { return mtx_rows[row_bases.w + 4u * n + w]; }
// Position row r at the frame before (motion vectors): the rows from `base`, or kMotionOwnRows: the draw's own.
float4 matrix_row_prev(uint base, uint r) { return base == 0xFFFFFFFFu ? matrix_row(r) : mtx_rows[base + r]; }

struct VSIn {
    float3   pos      : POSITION;
    float3   nrm      : NORMAL;
    float3   tangent  : TANGENT;
    float3   binormal : BINORMAL;
    uint     color0   : COLOR0;
    uint     color1   : COLOR1;
    float2   uv[8]    : TEXCOORD;
    uint4    mtx_idx  : BLENDINDICES;
#if MAT_CURVED
    uint3    curve    : BEND;
#endif
};
struct VSOut {
    float4   pos       : SV_Position;
    float4   col0      : COLOR0;
    float4   col1      : COLOR1;
    float3   uv[8]     : TEXCOORD;
#if MAT_LIGHT_OUT
    float3   ppl_pos   : TEXCOORD8;
    float3   ppl_nrm   : TEXCOORD9;
    float4   ppl_vcol0 : TEXCOORD10;
    float4   ppl_vcol1 : TEXCOORD11;
#endif
#if MAT_MOTION
    float4   mv_cur    : TEXCOORD12;
    float4   mv_prev   : TEXCOORD13;
#endif
#if MAT_CURVED
    float4   curve_pos : TEXCOORD14;
    float4   curve_nrm : TEXCOORD15;
    uint4    curve_data : TEXCOORD16;
#endif
};

#if MAT_CURVED
// Curved surfaces' packed normals (curved_surfaces.h's pack_normal): octahedral, two 16-bit halves, zero for none.
float3 curve_unpack(uint packed) {
    if (packed == 0u) {
        return float3(0.0, 0.0, 0.0);
    }
    const float2 p = (float2(float(packed >> 16u), float(packed & 0xFFFFu)) - 32768.0) / 32767.0;
    float3 n = float3(p, 1.0 - abs(p.x) - abs(p.y));
    if (n.z < 0.0) {
        n.xy = (1.0 - abs(p.yx)) * float2(p.x >= 0.0 ? 1.0 : -1.0, p.y >= 0.0 ? 1.0 : -1.0);
    }
    return normalize(n);
}
uint curve_pack(float3 n) {
    const float sum = abs(n.x) + abs(n.y) + abs(n.z);
    if (!(sum > 0.0)) {
        return 0u;
    }
    float2 p = n.xy / sum;
    if (n.z < 0.0) {
        p = (1.0 - abs(p.yx)) * float2(p.x >= 0.0 ? 1.0 : -1.0, p.y >= 0.0 ? 1.0 : -1.0);
    }
    const uint2 h = uint2(round(clamp(p, -1.0, 1.0) * 32767.0) + 32768.0);
    return (h.x << 16u) | h.y;
}
#endif

#if MAT_BLEND
// Interpolated redraw: a sub-frame blends a CPU-written vertex buffer with the matching draw's vertices of the frame
// before (RenderVertex, 144-byte stride; the renderer binds them at t1, space1 and the blend at b2).
cbuffer VertexBlend : register(b2) {
    uint  vb_mode;   // 0: the draw's own vertices only
    uint  vb_frame;  // element of the presented frame's values (a zero element in game frames)
    uint  vb_slot;   // the pair's verdict
    uint  vb_last;   // the last vertex of the earlier draw
    uint  vb_base;   // its first vertex's byte offset in vb_prev (0 where the view starts there)
};
ByteAddressBuffer vb_prev : register(t1, space1);
// Per pair: vertices, positions unchanged, largest move, extent (the GPU check of the pairing, renderer_d3d12.cpp
// kBlendValidateCs). Element 8192: the presented frame's t, the t of the frame presented before it and its jitter
// (render pixels); element 8193 stays zero.
StructuredBuffer<uint4> vb_verdict : register(t2, space1);
static float vb_t;
static float vb_t_prev;
static float2 vb_jitter;
// The vertex's object-space position in the frame presented before (motion vectors).
static float3 vb_pos_prev;
// A colour that turns visible or invisible (its alpha, the low byte, leaves or reaches zero) shows the new colour,
// and a channel that jumps by more than half its range its new value, as a coordinate step does.
uint vb_color(uint a, uint b) {
    if (((a & 0xFFu) == 0u) != ((b & 0xFFu) == 0u)) {
        return b;
    }
    uint c = 0u;
    [unroll] for (uint s = 0u; s < 32u; s += 8u) {
        const float x = float((a >> s) & 0xFFu);
        const float y = float((b >> s) & 0xFFu);
        c |= uint(abs(y - x) > 128.0 ? y : round(lerp(x, y, vb_t))) << s;
    }
    return c;
}
// A whole-texture jump is a wrap; a step of more than an eighth (an atlas frame) shows the new coordinate.
float vb_coord(float a, float b) {
    const float d = b - a;
    const float w = abs(d) > 0.5 ? a + round(d) : a;
    return abs(b - w) > 0.125 ? b : lerp(w, b, vb_t);
}
void vertex_blend(inout VSIn v, uint id) {
    // Root views have no bounds: every read stays inside its buffer even if a compiler moves it above the checks.
    const uint4 frame = vb_verdict[min(vb_frame, 8193u)];
    vb_t = asfloat(frame.x);
    vb_t_prev = asfloat(frame.y);
    vb_jitter = asfloat(frame.zw);
    vb_pos_prev = v.pos;
    if (vb_mode == 0u) {
        return;
    }
    // The same geometry one frame earlier: positions unchanged, all moved by at most a quarter of its extent, or any
    // by at most 2% (a reordered or different vertex set shows this frame's vertices).
    const uint4 verdict = vb_verdict[min(vb_slot, 8191u)];
    const float worst = asfloat(verdict.z);
    const float extent = asfloat(verdict.w);
    if (verdict.y != verdict.x && !(verdict.y == 0u && worst <= 0.25 * extent) &&
        !(worst <= 0.02 * extent)) {
        return;
    }
    const uint at = vb_base + min(id, vb_last) * 144u;
    const float3 earlier = asfloat(vb_prev.Load3(at));
    vb_pos_prev = lerp(earlier, v.pos, vb_t_prev);
    if (vb_t >= 1.0) {
        return;  // the game frame's own vertices
    }
    v.pos = lerp(earlier, v.pos, vb_t);
    const float3 n = lerp(asfloat(vb_prev.Load3(at + 12u)), v.nrm, vb_t);
    const float nn = dot(n, n);
    if (nn > 0.0) {
        v.nrm = n * sqrt(dot(v.nrm, v.nrm) / nn);
    }
    const uint2 c = vb_prev.Load2(at + 48u);
    v.color0 = vb_color(c.x, v.color0);
    v.color1 = vb_color(c.y, v.color1);
    [unroll] for (uint i = 0u; i < 8u; ++i) {
        const float2 uv = asfloat(vb_prev.Load2(at + 56u + 8u * i));
        v.uv[i] = float2(vb_coord(uv.x, v.uv[i].x), vb_coord(uv.y, v.uv[i].y));
    }
}
#endif

// Physical ray tracing (flags bit 6): what a de-baked vertex colour is raised by: the model's baked light at its most
// lit facing (debake_peak: its light at the player's up, RenderCore::flush_draw_state) over the light at this vertex's
// object-space normal `nrm` (nine spherical harmonics a channel, debake_sh; RenderCore::fit_debake_light), 1/4 to 4.
// No normal: 1.
float3 nebula_debake_gain(float3 nrm) {
    const float len2 = dot(nrm, nrm);
    if (!(len2 > 0.25)) {
        return float3(1.0, 1.0, 1.0);
    }
    const float3 n = nrm * rsqrt(len2);
    const float y[9] = {0.282095, 0.488603 * n.y, 0.488603 * n.z, 0.488603 * n.x, 1.092548 * n.x * n.y,
                        1.092548 * n.y * n.z, 0.315392 * (3.0 * n.z * n.z - 1.0), 1.092548 * n.x * n.z,
                        0.546274 * (n.x * n.x - n.y * n.y)};
    float3 light = float3(0.0, 0.0, 0.0);
    [unroll] for (uint i = 0u; i < 9u; ++i) {
        light.r += debake_sh[i >> 2u][i & 3u] * y[i];
        light.g += debake_sh[(9u + i) >> 2u][(9u + i) & 3u] * y[i];
        light.b += debake_sh[(18u + i) >> 2u][(18u + i) & 3u] * y[i];
    }
    const float3 peak = max(debake_peak.rgb, 1e-3);
    return clamp(peak / max(light, peak * 0.25), 0.25, 4.0);
}

// A vertex colour with its baked light removed: its hue at full brightness (black stays black).
float3 nebula_debake(float3 c) {
    const float m = max(c.r, max(c.g, c.b));
    return m > (1.0 / 255.0) ? c / m : c;
}

// One light's contribution to a channel: `ctrl` its channel word (bits 7-8 diffuse, 9-10 attenuation). With `finite`
// the attenuation operands stay at most 1e18 (squares 1e36), so a zero coefficient nulls an out-of-range cosine.
void add_light(uint n, uint ctrl, bool alpha_part, bool finite, float3 wpos, float3 nrm,
               inout float3 lacc, inout float lacc_a) {
    uint  lcw  = asuint(light_row(n, 0u).w);
    float4 le1 = light_row(n, 1u);
    float4 le2 = light_row(n, 2u);
    float4 le3 = light_row(n, 3u);
    float3 lpos  = float3(le2.z, le2.w, le3.x);
    float3 ldvec = float3(le3.y, le3.z, le3.w);
    float3 cosA  = le1.xyz;
    float3 distA = float3(le1.w, le2.x, le2.y);
    const uint atten_func = (ctrl >> 9u) & 3u;
    const uint diffuse_func = (ctrl >> 7u) & 3u;
    float3 ldir;
    float attn;
    if (atten_func == 1u) {
        float3 ldelta = lpos - wpos;
        float ldd = dot(ldelta, ldelta);
        ldir = (ldd > 0.0) ? ldelta * rsqrt(ldd) : nrm;
        attn = (dot(nrm, ldir) >= 0.0) ? max(0.0, dot(nrm, ldvec)) : 0.0;
        if (finite) {
            attn = min(attn, 1.0e18);
        }
        float3 distAttn = distA;
        if (diffuse_func != 0u) {
            float dad = dot(distA, distA);
            distAttn = (dad > 0.0) ? distA * rsqrt(dad) : distA;
        }
        float denom = dot(distAttn, float3(1.0, attn, attn*attn));
        attn = (denom != 0.0)
             ? max(0.0, dot(cosA, float3(1.0, attn, attn*attn))) / denom
             : 0.0;
    } else if (atten_func == 3u) {
        float3 ldelta = lpos - wpos;
        float dist2 = dot(ldelta, ldelta);
        float dist  = sqrt(dist2);
        ldir = (dist > 0.0) ? ldelta / dist : nrm;
        attn = max(0.0, dot(ldir, ldvec));
        if (finite) {
            attn = min(attn, 1.0e18);
            dist = min(dist, 1.0e18);
            dist2 = min(dist2, 1.0e36);
        }
        float denom = dot(distA, float3(1.0, dist, dist2));
        attn = (denom != 0.0)
             ? max(0.0, cosA.x + cosA.y*attn + cosA.z*attn*attn) / denom
             : 0.0;
    } else {
        float3 ldelta = lpos - wpos;
        float ldd = dot(ldelta, ldelta);
        ldir = (ldd > 0.0) ? ldelta * rsqrt(ldd) : nrm;
        attn = 1.0;
    }
    if (alpha_part) {
        float lcol = float(lcw & 255u);
        if (diffuse_func == 1u) {
            lacc_a += attn * dot(ldir, nrm) * lcol;
        } else if (diffuse_func == 2u) {
            lacc_a += attn * max(0.0, dot(ldir, nrm)) * lcol;
        } else {
            lacc_a += attn * lcol;
        }
    } else {
        float3 lcol = float3(
            float((lcw >> 24) & 255u),
            float((lcw >> 16) & 255u),
            float((lcw >>  8) & 255u));
        if (diffuse_func == 1u) {
            lacc += attn * dot(ldir, nrm) * lcol;
        } else if (diffuse_func == 2u) {
            lacc += attn * max(0.0, dot(ldir, nrm)) * lcol;
        } else {
            lacc += attn * lcol;
        }
    }
}

uint coord_word(uint i) { return program_coords[i >> 2u][i & 3u]; }
uint post_word(uint i) { return program_post[i >> 2u][i & 3u]; }

#if MAT_BLEND
VSOut main(VSIn v, uint vertex_id : SV_VertexID) {
    vertex_blend(v, vertex_id);
#else
VSOut main(VSIn v) {
#if MAT_MOTION
    // A game frame's values (the vertex blend's with the frame's zero element): no jitter of a presented frame, the
    // position before the vertex's own.
    const float2 vb_jitter = float2(0.0, 0.0);
    const float3 vb_pos_prev = v.pos;
#endif
#endif
    const uint n_coords = program_header.x;
    const uint n_chans = program_header.y;
    const uint key_flags = program_header.z;
    const uint matrix_slots = program_header.w & 0xFFu;
    const uint features = program_header.w >> 8u;
    const bool per_vertex_position = (key_flags & 0x01u) != 0u;
    const bool finite_attn = (key_flags & 0x20u) != 0u;
    const bool enhanced = (key_flags & 0x40u) != 0u;

    // The position matrix's place: the vertex's own where the stream carries one, else the draw's. Clamped to the
    // 64-row position region so a bad index cannot read past the draw's rows.
    const uint pn_row = min((per_vertex_position ? v.mtx_idx.x : matrix_index_a) & 0x3Fu, 61u);
    const float3 pos_in = v.pos;
    float4 wpos;
    if (per_vertex_position) {
        wpos = float4(dot(matrix_row(pn_row + 0u), float4(pos_in, 1.0)),
                      dot(matrix_row(pn_row + 1u), float4(pos_in, 1.0)),
                      dot(matrix_row(pn_row + 2u), float4(pos_in, 1.0)),
                      1.0);
    } else {
        wpos = float4(dot(inline_pos_matrix[0], float4(pos_in, 1.0)),
                      dot(inline_pos_matrix[1], float4(pos_in, 1.0)),
                      dot(inline_pos_matrix[2], float4(pos_in, 1.0)),
                      1.0);
    }

    VSOut o = (VSOut)0;
    o.pos = mul(projection, wpos);
#if MAT_MOTION
    // Sub-frames take the presented frame's jitter here (perspective draws); game frames carry theirs in the
    // projection (motion_params.xy).
    const float2 jitter_ndc = projection[3][3] == 0.0 && motion_params.z != 0.0 &&
        motion_params.w != 0.0 ? vb_jitter / motion_params.zw : float2(0.0, 0.0);
    o.pos.xy += jitter_ndc * o.pos.w;
    float3 prev_eye;
    if (per_vertex_position) {
        if (motion_ctrl.x == 0u) {
            prev_eye = float3(dot(matrix_row_prev(motion_ctrl.y, pn_row+0u), float4(vb_pos_prev, 1.0)),
                              dot(matrix_row_prev(motion_ctrl.y, pn_row+1u), float4(vb_pos_prev, 1.0)),
                              dot(matrix_row_prev(motion_ctrl.y, pn_row+2u), float4(vb_pos_prev, 1.0)));
        } else {
            prev_eye = float3(dot(motion_prev_pos[0], wpos),
                              dot(motion_prev_pos[1], wpos),
                              dot(motion_prev_pos[2], wpos));
        }
    } else {
        prev_eye = float3(dot(motion_prev_pos[0], float4(vb_pos_prev, 1.0)),
                          dot(motion_prev_pos[1], float4(vb_pos_prev, 1.0)),
                          dot(motion_prev_pos[2], float4(vb_pos_prev, 1.0)));
    }
    float4 pp0 = motion_prev_proj[0];
    float3 prev_clip = (motion_prev_proj[1].z == 0.0)
        ? float3(pp0.x * prev_eye.x + pp0.y * prev_eye.z,
                 pp0.z * prev_eye.y + pp0.w * prev_eye.z, -prev_eye.z)
        : float3(pp0.x * prev_eye.x + pp0.y, pp0.z * prev_eye.y + pp0.w, 1.0);
    o.mv_cur = float4(o.pos.xy - (motion_params.xy + jitter_ndc) * o.pos.w, motion_params.z, o.pos.w);
    o.mv_prev = float4(prev_clip.xy, motion_params.w, prev_clip.z);
#endif

    // The vertex colours (zero when the stream carries none).
    float4 vtx_col0 = float4(
        float((v.color0 >> 24u) & 0xFFu) / 255.0,
        float((v.color0 >> 16u) & 0xFFu) / 255.0,
        float((v.color0 >>  8u) & 0xFFu) / 255.0,
        float((v.color0       ) & 0xFFu) / 255.0);
    float4 vtx_col1 = float4(
        float((v.color1 >> 24u) & 0xFFu) / 255.0,
        float((v.color1 >> 16u) & 0xFFu) / 255.0,
        float((v.color1 >>  8u) & 0xFFu) / 255.0,
        float((v.color1       ) & 0xFFu) / 255.0);
    if ((features & 0x2u) != 0u) {
        // Physical ray tracing (flags bits 2-3): an unlit channel's vertex colour is the art's baked light. Bit 6: by
        // the model's facing fit (nebula_debake_gain); else each colour over its brightest channel.
        if ((flags & 64u) != 0u) {
            const float3 debake_gain = nebula_debake_gain(v.nrm);
            if ((flags & 4u) != 0u) vtx_col0.rgb = saturate(vtx_col0.rgb * debake_gain);
            if ((flags & 8u) != 0u) vtx_col1.rgb = saturate(vtx_col1.rgb * debake_gain);
        } else {
            if ((flags & 4u) != 0u) vtx_col0.rgb = nebula_debake(vtx_col0.rgb) * (float((flags >> 8u) & 0xFFu) / 255.0);
            if ((flags & 8u) != 0u) vtx_col1.rgb = nebula_debake(vtx_col1.rgb) * (float((flags >> 16u) & 0xFFu) / 255.0);
        }
    }

    // Whether any active channel lights.
    bool lighting_used = false;
    [unroll] for (uint lc = 0u; lc < 2u; ++lc) {
        if (lc < n_chans && (((program_channels[lc] >> 1u) & 1u) != 0u || ((program_channels[2u + lc] >> 1u) & 1u) != 0u)) {
            lighting_used = true;
        }
    }
    float3 nrm = float3(0.0, 0.0, 0.0);
    if (lighting_used) {
        // The lighting-space normal: the normal matrix's rows at the position matrix's place (pn_row & 31),
        // renormalized; the zero guard keeps draws without a normal attribute NaN-free.
        const uint nrm_q = pn_row & 31u;
        const float3 nrm_r0 = normal_row(nrm_q).xyz;
        const float3 nrm_r1 = normal_row(nrm_q + 1u).xyz;
        const float3 nrm_r2 = normal_row(nrm_q + 2u).xyz;
        const float3 nrm_t = float3(dot(nrm_r0, v.nrm), dot(nrm_r1, v.nrm), dot(nrm_r2, v.nrm));
        nrm = (dot(nrm_t, nrm_t) > 0.0) ? normalize(nrm_t) : nrm_t;
    } else if (enhanced) {
        // Unlit draws may leave the normal matrices unloaded: the position matrix turns the normal (exact for
        // rotation and uniform scale).
        const float3 nrm_row0 = (per_vertex_position ? matrix_row(pn_row + 0u) : inline_pos_matrix[0]).xyz;
        const float3 nrm_row1 = (per_vertex_position ? matrix_row(pn_row + 1u) : inline_pos_matrix[1]).xyz;
        const float3 nrm_row2 = (per_vertex_position ? matrix_row(pn_row + 2u) : inline_pos_matrix[2]).xyz;
        const float3 nrm_t = float3(dot(nrm_row0, v.nrm), dot(nrm_row1, v.nrm), dot(nrm_row2, v.nrm));
        nrm = (dot(nrm_t, nrm_t) > 0.0) ? normalize(nrm_t) : nrm_t;
    }

    // The colour channels: the material colour (register or vertex) times the light (ambient plus each light, 0..255
    // clamped), colour and alpha each by its own channel word.
    [unroll] for (uint ch = 0u; ch < 2u; ++ch) {
        const float4 vtx = ch == 0u ? vtx_col0 : vtx_col1;
        float4 result = vtx;
        if (ch < n_chans) {
            const uint cc = program_channels[ch];
            const uint ca = program_channels[2u + ch];
            const float3 mat_c = ((cc & 1u) != 0u ? vtx : chan_material[ch]).rgb;
            if (((cc >> 1u) & 1u) != 0u) {
                float3 lacc = (((cc >> 6u) & 1u) != 0u ? vtx : chan_ambient[ch]).rgb * 255.0;
                float unused_a = 0.0;
                const uint mask = ((cc >> 2u) & 0xFu) | (((cc >> 11u) & 0xFu) << 4u);
                [loop] for (uint n = 0u; n < 8u; ++n) {
                    if (((mask >> n) & 1u) != 0u) {
                        add_light(n, cc, false, finite_attn, wpos.xyz, nrm, lacc, unused_a);
                    }
                }
                result.rgb = mat_c * (clamp(lacc, 0.0, 255.0) / 255.0);
            } else {
                result.rgb = mat_c;
            }
            const float mat_a = (ca & 1u) != 0u ? vtx.a : chan_material[ch].a;
            if (((ca >> 1u) & 1u) != 0u) {
                float lacc_a = (((ca >> 6u) & 1u) != 0u ? vtx.a : chan_ambient[ch].a) * 255.0;
                float3 unused_c = float3(0.0, 0.0, 0.0);
                const uint mask = ((ca >> 2u) & 0xFu) | (((ca >> 11u) & 0xFu) << 4u);
                [loop] for (uint n = 0u; n < 8u; ++n) {
                    if (((mask >> n) & 1u) != 0u) {
                        add_light(n, ca, true, finite_attn, wpos.xyz, nrm, unused_c, lacc_a);
                    }
                }
                result.a = mat_a * (clamp(lacc_a, 0.0, 255.0) / 255.0);
            } else {
                result.a = mat_a;
            }
        }
        if (ch == 0u) {
            o.col0 = result;
        } else {
            o.col1 = result;
        }
    }
#if MAT_LIGHT_OUT
    o.ppl_pos = wpos.xyz;
    o.ppl_nrm = nrm;
    o.ppl_vcol0 = vtx_col0;
    o.ppl_vcol1 = vtx_col1;
#endif

    // Texture coordinates: the input row (position, normal, binormals, coordinates 0-7, raw), its 2x4 or 3x4 matrix
    // (per vertex or the draw's), the optional normalize and post-matrix, the q == 0 rule; or the lit colour
    // (types 2, 3); or an emboss offset of an earlier coordinate along the light (type 1). The fragment divides by q.
    float3 uvs[8] = {float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0),
                     float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0)};
    [loop] for (uint i = 0u; i < n_coords && i < 8u; ++i) {
        const uint tg = coord_word(i);
        const uint type = (tg >> 4u) & 7u;
        float3 res;
        if (type == 2u) {
            res = float3(o.col0.rg, 1.0);
        } else if (type == 3u) {
            res = float3(o.col1.rg, 1.0);
        } else if (type == 1u) {
            const uint emboss_q = pn_row & 31u;
            const float3x3 emboss_matrix = float3x3(normal_row(emboss_q).xyz, normal_row(emboss_q + 1u).xyz,
                                                    normal_row(emboss_q + 2u).xyz);
            const float3 emboss_tangent = mul(emboss_matrix, v.tangent);
            const float3 emboss_binormal = mul(emboss_matrix, v.binormal);
            const uint emboss_light = (tg >> 15u) & 7u;
            const float4 emboss_l2 = light_row(emboss_light, 2u);
            const float4 emboss_l3 = light_row(emboss_light, 3u);
            const float3 emboss_delta = float3(emboss_l2.zw, emboss_l3.x) - wpos.xyz;
            const float emboss_dist2 = dot(emboss_delta, emboss_delta);
            const float3 emboss_ldir = (emboss_dist2 > 0.0) ? emboss_delta * rsqrt(emboss_dist2) : float3(0,0,0);
            res = uvs[(tg >> 12u) & 7u] +
                  float3(dot(emboss_ldir, emboss_tangent), dot(emboss_ldir, emboss_binormal), 0.0);
        } else {
            const uint source_row = (tg >> 7u) & 31u;
            float4 coord = float4(0.0, 0.0, 1.0, 1.0);
            if (source_row == 0u) {
                coord.xyz = v.pos;
            } else if (source_row == 1u) {
                coord.xyz = v.nrm;
            } else if (source_row == 3u) {
                coord.xyz = v.tangent;
            } else if (source_row == 4u) {
                coord.xyz = v.binormal;
            } else if (source_row >= 5u && source_row <= 12u) {
                coord = float4(v.uv[source_row - 5u], 1.0, 1.0);
            }
            if (((tg >> 2u) & 1u) == 0u) {
                coord.z = 1.0;
            }
            const bool stq = ((tg >> 1u) & 1u) != 0u;
            if (((matrix_slots >> i) & 1u) != 0u) {
                const uint packed = i < 3u ? v.mtx_idx.x : i < 7u ? v.mtx_idx.y : v.mtx_idx.z;
                const uint shift = i < 3u ? 8u + 8u * i : i < 7u ? 8u * (i - 3u) : 0u;
                const uint tm = min((packed >> shift) & 0xFFu, 61u);
                res = float3(dot(coord, matrix_row(tm + 0u)), dot(coord, matrix_row(tm + 1u)),
                             stq ? dot(coord, matrix_row(tm + 2u)) : 1.0);
            } else {
                res = float3(dot(coord, inline_tex_matrices[i * 3u]), dot(coord, inline_tex_matrices[i * 3u + 1u]),
                             stq ? dot(coord, inline_tex_matrices[i * 3u + 2u]) : 1.0);
            }
            if ((key_flags & 0x04u) != 0u) {
                // Guarded: normalize(0) is NaN, and a degenerate input (zero normal or position) must not poison
                // the coordinate.
                if (((post_word(i) >> 8u) & 1u) != 0u) {
                    res = (dot(res, res) > 0.0) ? normalize(res) : res;
                }
                const float4 P0 = inline_post_matrices[i * 3u];
                const float4 P1 = inline_post_matrices[i * 3u + 1u];
                const float4 P2 = inline_post_matrices[i * 3u + 2u];
                res = float3(dot(P0.xyz, res) + P0.w, dot(P1.xyz, res) + P1.w, dot(P2.xyz, res) + P2.w);
            }
            if (res.z == 0.0) res.xy = clamp(res.xy * 0.5, -1.0, 1.0);
        }
        uvs[i] = res;
    }
    [unroll] for (uint u = 0u; u < 8u; ++u) {
        o.uv[u] = uvs[u];
    }
#if MAT_CURVED
    // The displacement normal turned as normals are, by the position matrix's cofactors (right under any scale; its
    // sign does not matter to the Phong planes).
    const float3 curve_r0 = (per_vertex_position ? matrix_row(pn_row + 0u) : inline_pos_matrix[0]).xyz;
    const float3 curve_r1 = (per_vertex_position ? matrix_row(pn_row + 1u) : inline_pos_matrix[1]).xyz;
    const float3 curve_r2 = (per_vertex_position ? matrix_row(pn_row + 2u) : inline_pos_matrix[2]).xyz;
    const float3x3 curve_cofactor = float3x3(cross(curve_r1, curve_r2), cross(curve_r2, curve_r0),
                                             cross(curve_r0, curve_r1));
    float3 curve_n = mul(curve_cofactor, curve_unpack(v.curve.x));
    float3 curve_c = mul(curve_cofactor, curve_unpack(v.curve.y));
    curve_n = dot(curve_n, curve_n) > 0.0 ? normalize(curve_n) : curve_n;
    curve_c = dot(curve_c, curve_c) > 0.0 ? normalize(curve_c) : curve_c;
#if MAT_MOTION
    o.curve_pos = float4(wpos.xyz, jitter_ndc.x);
    o.curve_nrm = float4(curve_n, jitter_ndc.y);
#else
    o.curve_pos = float4(wpos.xyz, 0.0);
    o.curve_nrm = float4(curve_n, 0.0);
#endif
    o.curve_data = uint4(curve_pack(curve_c), v.curve.z, v.mtx_idx.z >> 16u, 0u);
#endif
    return o;
}
