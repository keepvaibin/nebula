// J3D material 1054 (tools/dev/gen_j3d_materials.py from the game's material set; E-04)
#include "j3d_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color.rgb = v.color0.rgb;
    o.color.a = material.mat_color[0].a;
    o.color1 = v.color1;
    {  // coordinate 0: kind 0, projective 0, row 5
        const float4 c = float4(v.uv[0], 1.0, 1.0);
        float3 res;
        if (((vs_header.w >> 0u) & 1u) != 0u) {
            const uint tm = native_tex_matrix(v, 0u);
            res = float3(dot(c, native_matrix_row(tm + 0u)), dot(c, native_matrix_row(tm + 1u)), 1.0);
        } else {
            res = float3(dot(c, inline_tex_matrices[0]), dot(c, inline_tex_matrices[1]), 1.0);
        }
        const uint pt = native_post(0u);
        const bool posted = (pt & 0x3Fu) != 61u;
        if (posted) {
            if ((pt & 0x100u) != 0u) res = dot(res, res) > 0.0 ? normalize(res) : res;
            res = float3(dot(float4(res, 1.0), inline_post_matrices[0]), dot(float4(res, 1.0), inline_post_matrices[1]), dot(float4(res, 1.0), inline_post_matrices[2]));
        }
        if (posted && res.z == 0.0) res.xy = clamp(res.xy * 0.5, -1.0, 1.0);
        o.uv[0] = float4(res, posted ? 1.0 : 0.0);
    }
    {  // coordinate 1: kind 0, projective 1, row 1
        const float4 c = float4(v.nrm, 1.0);
        float3 res;
        if (((vs_header.w >> 1u) & 1u) != 0u) {
            const uint tm = native_tex_matrix(v, 1u);
            res = float3(dot(c, native_matrix_row(tm + 0u)), dot(c, native_matrix_row(tm + 1u)), dot(c, native_matrix_row(tm + 2u)));
        } else {
            res = float3(dot(c, inline_tex_matrices[3]), dot(c, inline_tex_matrices[4]), dot(c, inline_tex_matrices[5]));
        }
        const uint pt = native_post(1u);
        const bool posted = (pt & 0x3Fu) != 61u;
        if (posted) {
            if ((pt & 0x100u) != 0u) res = dot(res, res) > 0.0 ? normalize(res) : res;
            res = float3(dot(float4(res, 1.0), inline_post_matrices[3]), dot(float4(res, 1.0), inline_post_matrices[4]), dot(float4(res, 1.0), inline_post_matrices[5]));
        }
        if (true && res.z == 0.0) res.xy = clamp(res.xy * 0.5, -1.0, 1.0);
        o.uv[1] = float4(res, true ? 1.0 : 0.0);
    }
    {  // coordinate 2: kind 0, projective 1, row 1
        const float4 c = float4(v.nrm, 1.0);
        float3 res;
        if (((vs_header.w >> 2u) & 1u) != 0u) {
            const uint tm = native_tex_matrix(v, 2u);
            res = float3(dot(c, native_matrix_row(tm + 0u)), dot(c, native_matrix_row(tm + 1u)), dot(c, native_matrix_row(tm + 2u)));
        } else {
            res = float3(dot(c, inline_tex_matrices[6]), dot(c, inline_tex_matrices[7]), dot(c, inline_tex_matrices[8]));
        }
        const uint pt = native_post(2u);
        const bool posted = (pt & 0x3Fu) != 61u;
        if (posted) {
            if ((pt & 0x100u) != 0u) res = dot(res, res) > 0.0 ? normalize(res) : res;
            res = float3(dot(float4(res, 1.0), inline_post_matrices[6]), dot(float4(res, 1.0), inline_post_matrices[7]), dot(float4(res, 1.0), inline_post_matrices[8]));
        }
        if (true && res.z == 0.0) res.xy = clamp(res.xy * 0.5, -1.0, 1.0);
        o.uv[2] = float4(res, true ? 1.0 : 0.0);
    }
    j3d_vertex_override(v, o);
}
float4 material_color(NativeVaryings i) {
    [branch] if (material.ovr.x != 0.0) {
        const float4 t = tex[0].Sample(samp[0], j3d_texel(i.uv[0], 0u) / material.tex_scale[0].zw);
        const float4 ras = float4(0.0, 0.0, 0.0, 0.0);
        const float4 prev = material.reg[0];
        const float4 r0 = material.reg[1];
        const float4 r1 = material.reg[2];
        const float4 r2 = material.reg[3];
        const float ka = 128.0 / 255.0;
        return j3d_override(t, i.color, 128.0 / 255.0, saturate((r0.a + lerp(0.0, ka, ras.a)) * 4.0), i.pos);
    }
    float4 prev = material.reg[0];
    float4 r0 = material.reg[1];
    float4 r1 = material.reg[2];
    float4 r2 = material.reg[3];
    float2 tevcoord = float2(0.0, 0.0);
    const float3 ind0 = round(tex[2].Sample(samp[2], j3d_texel(i.uv[2], 2u) / float2(1.0, 1.0) / material.tex_scale[2].zw).abg * 255.0);
    {  // stage 0
        const float4 t = tex[0].Sample(samp[0], j3d_texel(i.uv[0], 0u) / material.tex_scale[0].zw).rgba;
        const float4 ras = i.color.rgba;
        const float ka = 128.0 / 255.0;
        const float3 rc = saturate((r0.rgb + lerp(j3d_in(r1.rgb), t.rgb, t.rgb)));
        const float ra = saturate((r0.a + lerp(0.0, ka, ras.a)) * 4.0);
        prev.rgb = rc;
        prev.a = ra;
    }
    {  // stage 1
        const float4 t = tex[0].Sample(samp[0], j3d_texel(i.uv[0], 0u) / material.tex_scale[0].zw).rgba;
        const float4 ras = i.color.rgba;
        const float ka = 223.0 / 255.0;
        const float3 rc = saturate((prev.rgb + lerp(float3(0.0, 0.0, 0.0), t.rgb, j3d_in(prev.aaa))) * 2.0);
        const float ra = saturate((prev.a + lerp(0.0, ka, t.a)));
        prev.rgb = rc;
        prev.a = ra;
    }
    {  // stage 2
        float2 texel = j3d_texel(i.uv[1], 1u);
        float3 sv = floor(ind0 / 1.0);
        sv.x += -128.0;
        sv.y += -128.0;
        sv.z += -128.0;
        const float2 off = float2(dot(material.ind_mtx[0].xyz, sv), dot(material.ind_mtx[1].xyz, sv));
        tevcoord = texel + off;
        const float4 t = tex[1].Sample(samp[1], tevcoord / material.tex_scale[1].zw).rgba;
        const float4 ras = i.color.rgba;
        const float3 k = float4(float3(191.0 / 255.0, 191.0 / 255.0, 191.0 / 255.0), 0.0).rgb;
        const float3 rc = saturate((r0.rgb + lerp(float3(1.0, 1.0, 1.0), k.rgb, t.rgb)));
        const float ra = saturate((prev.a + lerp(0.0, 0.0, 0.0)));
        prev.rgb = rc;
        prev.a = ra;
    }
    prev = j3d_out(float4(prev.rgb, prev.a));
    prev.rgb = j3d_fog(prev.rgb, i.pos);
    return prev;
}
