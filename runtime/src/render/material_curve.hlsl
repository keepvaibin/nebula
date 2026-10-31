// Curved surfaces' hull and domain stages (Phong tessellation, Boubekeur and Alexa 2008), compiled when Nebula is
// built (material_shaders.h): MAT_DOMAIN 0 the hull stage, 1 the domain stage; MAT_LIGHT_OUT and MAT_MOTION the
// material vertex stage's outputs they pass on (material_vs.hlsl).

#ifndef MAT_DOMAIN
#define MAT_DOMAIN 0
#endif
#ifndef MAT_LIGHT_OUT
#define MAT_LIGHT_OUT 0
#endif
#ifndef MAT_MOTION
#define MAT_MOTION 0
#endif

// VertexConstants (uber_constants.h) to the motion fields; line_point_raster at c65.
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
#if MAT_MOTION
    float4   curve_skip_[7];
    float4   motion_prev_proj[2];
    float4   motion_prev_pos[3];
    float4   motion_params;
#endif
};
// The vertex stage's output (its texture coordinates as members), and the same less the curve's for the pixel stage.
struct VSOut {
    float4   pos       : SV_Position;
    float4   col0      : COLOR0;
    float4   col1      : COLOR1;
    float3   uv0      : TEXCOORD0;
    float3   uv1      : TEXCOORD1;
    float3   uv2      : TEXCOORD2;
    float3   uv3      : TEXCOORD3;
    float3   uv4      : TEXCOORD4;
    float3   uv5      : TEXCOORD5;
    float3   uv6      : TEXCOORD6;
    float3   uv7      : TEXCOORD7;
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
    float4   curve_pos : TEXCOORD14;
    float4   curve_nrm : TEXCOORD15;
    uint4    curve_data : TEXCOORD16;
};
struct DSOut {
    float4   pos       : SV_Position;
    float4   col0      : COLOR0;
    float4   col1      : COLOR1;
    float3   uv0      : TEXCOORD0;
    float3   uv1      : TEXCOORD1;
    float3   uv2      : TEXCOORD2;
    float3   uv3      : TEXCOORD3;
    float3   uv4      : TEXCOORD4;
    float3   uv5      : TEXCOORD5;
    float3   uv6      : TEXCOORD6;
    float3   uv7      : TEXCOORD7;
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
};

struct CurvePatch {
    float edges[3] : SV_TessFactor;
    float inside   : SV_InsideTessFactor;
};

// Packed normals (curved_surfaces.h's pack_normal): octahedral, two 16-bit halves, zero for none.
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

// The normal corner `a` bends the edge to corner `b` by (curved_surfaces.h): its place's crease normal when `b` is one
// of its crease neighbours, else its corner's displacement normal. Both triangles holding an edge pick alike.
float3 curve_edge_normal(VSOut a, VSOut b) {
    const uint place = b.curve_data.y & 0xFFFFu;
    if (place != 0xFFFFu && (place == (a.curve_data.y >> 16u) || place == (a.curve_data.z & 0xFFFFu))) {
        return curve_unpack(a.curve_data.x);
    }
    return a.curve_nrm.xyz;
}

#if !MAT_DOMAIN
// An edge's segments: about the square root of its curve's pixel length (chord times the angle its normals turn).
float curve_value(float4 a, float4 b, float3 qa, float3 qb, float3 na, float3 nb, float2 half_viewport) {
    const float3 edge = qb - qa;
    const float edge_length = length(edge);
    if (a.w <= 0.0 || b.w <= 0.0 || !(edge_length > 0.0)) {
        return 0.0;
    }
    const float3 along = edge / edge_length;
    const float tilt = max(abs(dot(along, na)), abs(dot(along, nb)));
    if (!(tilt > 0.0)) {
        return 0.0;
    }
    const float angle = 2.0 * asin(min(tilt, 1.0));
    const float chord = length((a.xy / a.w - b.xy / b.w) * half_viewport);
    return chord * angle * 0.5;
}

float curve_segments(VSOut a, VSOut b) {
    return clamp(sqrt(curve_value(a.pos, b.pos, a.curve_pos.xyz, b.curve_pos.xyz, curve_edge_normal(a, b),
                                  curve_edge_normal(b, a), 0.5 * line_point_raster.xy)), 1.0, 16.0);
}

// Whether the patch lies wholly past one of the view's side planes or behind the eye, however far its curve bulges:
// the domain stage moves a point at most three quarters of its distance to the farthest corner, so the eye-space
// sphere about the corners' centre through the farthest corner, widened by 0.75 of the longest edge, bounds it.
bool curve_outside(InputPatch<VSOut, 3> p) {
    const float3 q0 = p[0].curve_pos.xyz;
    const float3 q1 = p[1].curve_pos.xyz;
    const float3 q2 = p[2].curve_pos.xyz;
    const float3 centre = (q0 + q1 + q2) / 3.0;
    const float edge = sqrt(max(dot(q1 - q0, q1 - q0), max(dot(q2 - q1, q2 - q1), dot(q0 - q2, q0 - q2))));
    const float radius = sqrt(max(dot(q0 - centre, q0 - centre),
                                  max(dot(q1 - centre, q1 - centre), dot(q2 - centre, q2 - centre)))) +
                         0.75 * edge;
    const float4 planes[5] = {projection[3] - projection[0], projection[3] + projection[0],
                              projection[3] - projection[1], projection[3] + projection[1], projection[3]};
    [unroll] for (uint i = 0u; i < 5u; ++i) {
        if (dot(planes[i].xyz, centre) + planes[i].w + radius * length(planes[i].xyz) < 0.0) {
            return true;
        }
    }
    return false;
}

CurvePatch curve_patch(InputPatch<VSOut, 3> p) {
    CurvePatch c;
    if (curve_outside(p)) {
        c.edges[0] = 0.0;
        c.edges[1] = 0.0;
        c.edges[2] = 0.0;
        c.inside = 0.0;
        return c;
    }
    c.edges[0] = curve_segments(p[1], p[2]);
    c.edges[1] = curve_segments(p[2], p[0]);
    c.edges[2] = curve_segments(p[0], p[1]);
    c.inside = max(c.edges[0], max(c.edges[1], c.edges[2]));
    return c;
}

[domain("tri")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("curve_patch")]
[maxtessfactor(16.0)]
VSOut main(InputPatch<VSOut, 3> p, uint i : SV_OutputControlPointID) {
    return p[i];
}
#else
[domain("tri")]
DSOut main(CurvePatch c, float3 b : SV_DomainLocation, const OutputPatch<VSOut, 3> p) {
    DSOut o;
    o.pos = b.x * p[0].pos + b.y * p[1].pos + b.z * p[2].pos;
    o.col0 = b.x * p[0].col0 + b.y * p[1].col0 + b.z * p[2].col0;
    o.col1 = b.x * p[0].col1 + b.y * p[1].col1 + b.z * p[2].col1;
    o.uv0 = b.x * p[0].uv0 + b.y * p[1].uv0 + b.z * p[2].uv0;
    o.uv1 = b.x * p[0].uv1 + b.y * p[1].uv1 + b.z * p[2].uv1;
    o.uv2 = b.x * p[0].uv2 + b.y * p[1].uv2 + b.z * p[2].uv2;
    o.uv3 = b.x * p[0].uv3 + b.y * p[1].uv3 + b.z * p[2].uv3;
    o.uv4 = b.x * p[0].uv4 + b.y * p[1].uv4 + b.z * p[2].uv4;
    o.uv5 = b.x * p[0].uv5 + b.y * p[1].uv5 + b.z * p[2].uv5;
    o.uv6 = b.x * p[0].uv6 + b.y * p[1].uv6 + b.z * p[2].uv6;
    o.uv7 = b.x * p[0].uv7 + b.y * p[1].uv7 + b.z * p[2].uv7;
#if MAT_LIGHT_OUT
    o.ppl_pos = b.x * p[0].ppl_pos + b.y * p[1].ppl_pos + b.z * p[2].ppl_pos;
    o.ppl_nrm = b.x * p[0].ppl_nrm + b.y * p[1].ppl_nrm + b.z * p[2].ppl_nrm;
    o.ppl_vcol0 = b.x * p[0].ppl_vcol0 + b.y * p[1].ppl_vcol0 + b.z * p[2].ppl_vcol0;
    o.ppl_vcol1 = b.x * p[0].ppl_vcol1 + b.y * p[1].ppl_vcol1 + b.z * p[2].ppl_vcol1;
#endif
#if MAT_MOTION
    o.mv_cur = b.x * p[0].mv_cur + b.y * p[1].mv_cur + b.z * p[2].mv_cur;
    o.mv_prev = b.x * p[0].mv_prev + b.y * p[1].mv_prev + b.z * p[2].mv_prev;
#endif
    // Phong's move, edge by edge: -alpha times the sum over edges (i, k) of b_i b_k ((e.n_ik) n_ik - (e.n_ki) n_ki),
    // e the edge from i to k and n_ik the normal i bends it by.
    float3 move = float3(0.0, 0.0, 0.0);
    [unroll] for (uint i = 0u; i < 3u; ++i) {
        const uint k = (i + 1u) % 3u;
        const float3 e = p[k].curve_pos.xyz - p[i].curve_pos.xyz;
        const float3 ni = curve_edge_normal(p[i], p[k]);
        const float3 nk = curve_edge_normal(p[k], p[i]);
        move -= b[i] * b[k] * (dot(e, ni) * ni - dot(e, nk) * nk);
    }
    const float3 d = 0.5 * move;
    const float4 dc = mul(projection, float4(d, 0.0));
    const float2 jitter = float2(p[0].curve_pos.w, p[0].curve_nrm.w);
    o.pos += float4(dc.xy + jitter * dc.w, dc.z, dc.w);
#if MAT_LIGHT_OUT
    o.ppl_pos += d;
#endif
#if MAT_MOTION
    o.mv_cur += float4(dc.xy - motion_params.xy * dc.w, 0.0, dc.w);
    const float4 pp0 = motion_prev_proj[0];
    o.mv_prev.xy += motion_prev_proj[1].z == 0.0
        ? float2(pp0.x * d.x + pp0.y * d.z, pp0.z * d.y + pp0.w * d.z)
        : float2(pp0.x * d.x, pp0.z * d.y);
    o.mv_prev.w += motion_prev_proj[1].z == 0.0 ? -d.z : 0.0;
#endif
    return o;
}
#endif
