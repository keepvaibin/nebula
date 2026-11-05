// J3D material 603 (tools/dev/gen_j3d_materials.py from the game's material set; E-04)
// constants 0x3036188b
struct MaterialConstants {
    float4 reg1;
    float4 reg2;
    float4 kcolor0;
    float4 mat_color0;
    float4 ind_mtx0;
    float4 ind_mtx1;
    float4 fog;
    float4 fog_color;
    float4 tex_scale0;
    float4 tex_scale1;
    float4 ovr;
    float4 ovr_coord;
};
#include "j3d_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color.rgb = v.color0.rgb;
    o.color.a = material.mat_color0.a;
    o.color1 = v.color1;
    {  // coordinate 0: kind 0, projective 0, row 5
        const float4 c = float4(v.uv[0], 1.0, 1.0);
        float3 res;
        if (((vs_header.w >> 0u) & 1u) != 0u) {
            const uint tm = native_tex_matrix(v, 0u);
            res = float3(dot(c, native_matrix_row(tm + 0u)), dot(c, native_matrix_row(tm + 1u)), 1.0);
        } else {
            res = float3(dot(c, native_tex_row(0)), dot(c, native_tex_row(1)), 1.0);
        }
        const uint pt = native_post(0u);
        const bool posted = (pt & 0x3Fu) != 61u;
        if (posted) {
            if ((pt & 0x100u) != 0u) res = dot(res, res) > 0.0 ? normalize(res) : res;
            res = float3(dot(float4(res, 1.0), native_coord_post_row(0)), dot(float4(res, 1.0), native_coord_post_row(1)), dot(float4(res, 1.0), native_coord_post_row(2)));
        }
        if (posted && res.z == 0.0) res.xy = clamp(res.xy * 0.5, -1.0, 1.0);
        o.uv[0] = float4(res, posted ? 1.0 : 0.0);
    }
    {  // coordinate 1: kind 0, projective 0, row 5
        const float4 c = float4(v.uv[0], 1.0, 1.0);
        float3 res;
        if (((vs_header.w >> 1u) & 1u) != 0u) {
            const uint tm = native_tex_matrix(v, 1u);
            res = float3(dot(c, native_matrix_row(tm + 0u)), dot(c, native_matrix_row(tm + 1u)), 1.0);
        } else {
            res = float3(dot(c, native_tex_row(3)), dot(c, native_tex_row(4)), 1.0);
        }
        const uint pt = native_post(1u);
        const bool posted = (pt & 0x3Fu) != 61u;
        if (posted) {
            if ((pt & 0x100u) != 0u) res = dot(res, res) > 0.0 ? normalize(res) : res;
            res = float3(dot(float4(res, 1.0), native_coord_post_row(3)), dot(float4(res, 1.0), native_coord_post_row(4)), dot(float4(res, 1.0), native_coord_post_row(5)));
        }
        if (posted && res.z == 0.0) res.xy = clamp(res.xy * 0.5, -1.0, 1.0);
        o.uv[1] = float4(res, posted ? 1.0 : 0.0);
    }
    {  // coordinate 2: kind 0, projective 1, row 0
        const float4 c = float4(v.pos, 1.0);
        float3 res;
        if (((vs_header.w >> 2u) & 1u) != 0u) {
            const uint tm = native_tex_matrix(v, 2u);
            res = float3(dot(c, native_matrix_row(tm + 0u)), dot(c, native_matrix_row(tm + 1u)), dot(c, native_matrix_row(tm + 2u)));
        } else {
            res = float3(dot(c, native_tex_row(6)), dot(c, native_tex_row(7)), dot(c, native_tex_row(8)));
        }
        const uint pt = native_post(2u);
        const bool posted = (pt & 0x3Fu) != 61u;
        if (posted) {
            if ((pt & 0x100u) != 0u) res = dot(res, res) > 0.0 ? normalize(res) : res;
            res = float3(dot(float4(res, 1.0), native_coord_post_row(6)), dot(float4(res, 1.0), native_coord_post_row(7)), dot(float4(res, 1.0), native_coord_post_row(8)));
        }
        if (true && res.z == 0.0) res.xy = clamp(res.xy * 0.5, -1.0, 1.0);
        o.uv[2] = float4(res, true ? 1.0 : 0.0);
    }
    j3d_vertex_override(v, o);
}
float4 material_color(NativeVaryings i) {
    [branch] if (material.ovr.x != 0.0) {
        const float4 t = tex[0].Sample(samp[0], (j3d_coord(i.uv[0]) * material.tex_scale0.xy) / material.tex_scale0.zw);
        const float4 ras = float4(0.0, 0.0, 0.0, 0.0);
        const float4 prev = float4(1.0, 1.0, 1.0, 1.0);
        const float ka = material.kcolor0.a;
        return j3d_override(t, i.color, material.kcolor0.a, saturate((0.0 + lerp(0.0, 0.0, 0.0))), i.pos);
    }
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    float4 r0 = material.reg1;
    float4 r1 = material.reg2;
    float2 tevcoord = float2(0.0, 0.0);
    const float3 ind0 = round(tex[2].Sample(samp[2], j3d_coord(i.uv[2]) / float2(1.0, 1.0)).abg * 255.0);
    {  // stage 0
        float2 texel = (j3d_coord(i.uv[0]) * material.tex_scale0.xy);
        float3 sv = floor(ind0 / 1.0);
        sv.x += -128.0;
        sv.y += -128.0;
        sv.z += -128.0;
        const float2 off = float2(dot(material.ind_mtx0.xyz, sv), dot(material.ind_mtx1.xyz, sv));
        tevcoord = texel + off;
        const float4 t = tex[0].Sample(samp[0], tevcoord / material.tex_scale0.zw).rgba;
        const float4 ras = i.color.rgba;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(float3(0.0, 0.0, 0.0), t.rgb, ras.rgb) - 128.0 / 255.0));
        const float ra = saturate((0.0 + lerp(0.0, 0.0, 0.0)));
        prev.rgb = rc;
        prev.a = ra;
    }
    {  // stage 1
        float2 texel = (j3d_coord(i.uv[1]) * material.tex_scale1.xy);
        float3 sv = floor(ind0 / 1.0);
        sv.x += -128.0;
        sv.y += -128.0;
        sv.z += -128.0;
        const float2 off = float2(dot(material.ind_mtx0.xyz, sv), dot(material.ind_mtx1.xyz, sv));
        tevcoord = texel + off;
        const float4 t = tex[1].Sample(samp[1], tevcoord / material.tex_scale1.zw).rgba;
        const float4 ras = i.color.rgba;
        const float3 k = float4(material.kcolor0.rgb, 0.0).rgb;
        const float3 rc = saturate((r0.rgb + lerp(prev.rgb, k.rgb, t.rgb)) * 2.0);
        const float ra = (0.0 + lerp(j3d_in(r1.a), 0.0, 0.0));
        prev.rgb = rc;
        prev.a = ra;
    }
    prev = j3d_out(float4(prev.rgb, prev.a));
    prev.rgb = j3d_fog(prev.rgb, i.pos);
    return prev;
}
