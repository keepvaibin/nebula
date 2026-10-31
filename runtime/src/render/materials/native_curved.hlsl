// SPDX-License-Identifier: GPL-3.0-only
// Curved surfaces' patch stages for every native material (curved_surfaces.h): compiled once per output layout (with
// or without motion vectors) and paired with any native material's vertex and pixel shaders. The hull stage gives
// each edge as many segments as keep its arc within a quarter pixel of its chord (an edge's count depends only on its
// two vertices, so the triangles sharing it split it alike); the domain stage blends the three vertices at the point
// and moves it half way to the blend of its projections onto the corners' tangent planes (Phong tessellation, shape
// factor one half), each edge's ends projecting by the normal they bend that edge by.
struct MaterialConstants {
    float4 unused;
};

#include "native_material.hlsli"

struct CurvePatch {
    float edges[3] : SV_TessFactor;
    float inside : SV_InsideTessFactor;
};

// The normal corner `a` bends the edge to corner `b` by: its crease normal when `b` is one of its crease neighbours,
// else its displacement normal. Both triangles holding an edge pick alike.
float3 curve_edge_normal(NativeVsOut a, NativeVsOut b) {
    const uint place = b.curve_data.y & 0xFFFFu;
    if (place != 0xFFFFu && (place == (a.curve_data.y >> 16u) || place == (a.curve_data.z & 0xFFFFu))) {
        return curve_unpack(a.curve_data.x);
    }
    return a.curve_nrm.xyz;
}

// An edge's segments squared, before the clamp to 1-16: its chord on screen (pixels) times its turn, halved.
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

float curve_segments(NativeVsOut a, NativeVsOut b) {
    return clamp(sqrt(curve_value(a.pos, b.pos, a.curve_pos.xyz, b.curve_pos.xyz, curve_edge_normal(a, b),
                                  curve_edge_normal(b, a), 0.5 * line_point_raster.xy)), 1.0, 16.0);
}

// Whether the patch lies wholly past one of the view's side planes or behind the eye, however far its curve bulges.
bool curve_outside(InputPatch<NativeVsOut, 3> p) {
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

CurvePatch curve_patch(InputPatch<NativeVsOut, 3> p) {
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
NativeVsOut native_hs(InputPatch<NativeVsOut, 3> p, uint i : SV_OutputControlPointID) {
    return p[i];
}

[domain("tri")]
NativeVaryings native_ds(CurvePatch c, float3 b : SV_DomainLocation, const OutputPatch<NativeVsOut, 3> p) {
    NativeVaryings o;
    o.pos = b.x * p[0].pos + b.y * p[1].pos + b.z * p[2].pos;
    o.color = b.x * p[0].color + b.y * p[1].color + b.z * p[2].color;
    o.color1 = b.x * p[0].color1 + b.y * p[1].color1 + b.z * p[2].color1;
    [unroll] for (uint k = 0u; k < 8u; ++k) {
        o.uv[k] = b.x * p[0].uv[k] + b.y * p[1].uv[k] + b.z * p[2].uv[k];
    }
#if NATIVE_MOTION
    o.mv_cur = b.x * p[0].mv_cur + b.y * p[1].mv_cur + b.z * p[2].mv_cur;
    o.mv_prev = b.x * p[0].mv_prev + b.y * p[1].mv_prev + b.z * p[2].mv_prev;
#endif
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
#if NATIVE_MOTION
    o.mv_cur += float4(dc.xy - motion_params.xy * dc.w, 0.0, dc.w);
    const float4 pp0 = motion_prev_proj[0];
    o.mv_prev.xy += motion_prev_proj[1].z == 0.0 ? float2(pp0.x * d.x + pp0.y * d.z, pp0.z * d.y + pp0.w * d.z)
                                                 : float2(pp0.x * d.x, pp0.z * d.y);
    o.mv_prev.w += motion_prev_proj[1].z == 0.0 ? -d.z : 0.0;
#endif
    return o;
}
