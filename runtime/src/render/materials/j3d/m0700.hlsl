// J3D material 700 (tools/dev/gen_j3d_materials.py from the game's material set; E-04)
#include "j3d_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    {  // coordinate 0: type 1, source 4, matrix 30
        const float4 c = float4(v.uv[0], 1.0, 1.0);
        float3 res = float3(dot(c, inline_tex_matrices[0]), dot(c, inline_tex_matrices[1]), 1.0);
        res = float3(dot(float4(res, 1.0), inline_post_matrices[0]), dot(float4(res, 1.0), inline_post_matrices[1]), dot(float4(res, 1.0), inline_post_matrices[2]));
        o.uv[0] = float4(res, 1.0);
    }
    {  // coordinate 1: type 0, source 1, matrix 33
        const float4 c = float4(v.nrm, 1.0);
        float3 res = float3(dot(c, inline_tex_matrices[3]), dot(c, inline_tex_matrices[4]), dot(c, inline_tex_matrices[5]));
        res = float3(dot(float4(res, 1.0), inline_post_matrices[3]), dot(float4(res, 1.0), inline_post_matrices[4]), dot(float4(res, 1.0), inline_post_matrices[5]));
        o.uv[1] = float4(res, 1.0);
    }
    {  // coordinate 2: type 0, source 1, matrix 36
        const float4 c = float4(v.nrm, 1.0);
        float3 res = float3(dot(c, inline_tex_matrices[6]), dot(c, inline_tex_matrices[7]), dot(c, inline_tex_matrices[8]));
        res = float3(dot(float4(res, 1.0), inline_post_matrices[6]), dot(float4(res, 1.0), inline_post_matrices[7]), dot(float4(res, 1.0), inline_post_matrices[8]));
        o.uv[2] = float4(res, 1.0);
    }
    o.color.rgb = v.color0.rgb;
    o.color.a = material.mat_color[0].a;
    o.color1 = v.color1;
}
float4 material_color(NativeVaryings i) {
    float4 prev = material.reg[0];
    float4 r0 = material.reg[1];
    float4 r1 = material.reg[2];
    float4 r2 = material.reg[3];
    float2 tevcoord = float2(0.0, 0.0);
    const float3 ind0 = round(tex[2].Sample(samp[2], j3d_coord(i.uv[2]) / float2(1.0, 1.0)).abg * 255.0);
    {  // stage 0
        const float4 t = tex[0].Sample(samp[0], j3d_coord(i.uv[0])).rgba;
        const float4 ras = i.color.rgba;
        const float ka = 0.125;
        const float3 rc = saturate((r0.rgb + lerp(r1.rgb, t.rgb, t.rgb)));
        const float ra = saturate((r0.a + lerp(0.0, ka, ras.a)) * 4.0);
        prev.rgb = rc;
        prev.a = ra;
    }
    {  // stage 1
        const float4 t = tex[0].Sample(samp[0], j3d_coord(i.uv[0])).rgba;
        const float4 ras = i.color.rgba;
        const float ka = 0.875;
        const float3 rc = saturate((prev.rgb + lerp(float3(0.0, 0.0, 0.0), t.rgb, prev.aaa)) * 2.0);
        const float ra = saturate((prev.a + lerp(0.0, ka, t.a)));
        prev.rgb = rc;
        prev.a = ra;
    }
    {  // stage 2
        float tw, th;
        tex[1].GetDimensions(tw, th);
        float2 texel = j3d_coord(i.uv[1]) * float2(tw, th);
        float3 sv = floor(ind0 / 1.0);
        sv.x += -128.0;
        sv.y += -128.0;
        sv.z += -128.0;
        const float2 off = float2(dot(material.ind_mtx[0].xyz, sv), dot(material.ind_mtx[1].xyz, sv));
        tevcoord = texel + off;
        const float4 t = tex[1].Sample(samp[1], tevcoord / float2(tw, th)).rgba;
        const float4 ras = i.color.rgba;
        const float3 rc = saturate((r0.rgb + lerp(float3(1.0, 1.0, 1.0), r0.rgb, t.rgb)));
        const float ra = saturate((0.0 + lerp(0.0, r0.a, prev.a)));
        prev.rgb = rc;
        prev.a = ra;
    }
    return saturate(prev);
}
