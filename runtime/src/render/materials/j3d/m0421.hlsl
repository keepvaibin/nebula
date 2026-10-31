// J3D material 421 (tools/dev/gen_j3d_materials.py from the game's material set; E-04)
#include "j3d_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    {  // coordinate 0: type 1, source 4, matrix 30
        const float4 c = float4(v.uv[0], 1.0, 1.0);
        float3 res = float3(dot(c, inline_tex_matrices[0]), dot(c, inline_tex_matrices[1]), 1.0);
        res = float3(dot(float4(res, 1.0), inline_post_matrices[0]), dot(float4(res, 1.0), inline_post_matrices[1]), dot(float4(res, 1.0), inline_post_matrices[2]));
        o.uv[0] = float4(res, 1.0);
    }
    {  // coordinate 1: type 1, source 4, matrix 33
        const float4 c = float4(v.uv[0], 1.0, 1.0);
        float3 res = float3(dot(c, inline_tex_matrices[3]), dot(c, inline_tex_matrices[4]), 1.0);
        res = float3(dot(float4(res, 1.0), inline_post_matrices[3]), dot(float4(res, 1.0), inline_post_matrices[4]), dot(float4(res, 1.0), inline_post_matrices[5]));
        o.uv[1] = float4(res, 1.0);
    }
    const uint nq = v.pn_row & 31u;
    const float3 nt = float3(dot(native_normal_row(nq).xyz, v.nrm), dot(native_normal_row(nq + 1u).xyz, v.nrm), dot(native_normal_row(nq + 2u).xyz, v.nrm));
    const float3 nrm = dot(nt, nt) > 0.0 ? normalize(nt) : nt;
    o.color.rgb = v.color0.rgb;
    o.color.a = material.mat_color[0].a * saturate((material.amb_color[0].a * 255.0 + j3d_light(2u, 3u, 2u, v.eye, nrm).a) / 255.0);
    o.color1 = v.color1;
}
float4 material_color(NativeVaryings i) {
    float4 prev = material.reg[0];
    float4 r0 = material.reg[1];
    float4 r1 = material.reg[2];
    float4 r2 = material.reg[3];
    float2 tevcoord = float2(0.0, 0.0);
    const float3 ind0 = round(tex[1].Sample(samp[1], j3d_coord(i.uv[1]) / float2(1.0, 1.0)).abg * 255.0);
    {  // stage 0
        float tw, th;
        tex[0].GetDimensions(tw, th);
        float2 texel = j3d_coord(i.uv[0]) * float2(tw, th);
        float3 sv = floor(ind0 / 1.0);
        sv.x += -128.0;
        sv.y += -128.0;
        sv.z += -128.0;
        const float2 off = float2(dot(material.ind_mtx[0].xyz, sv), dot(material.ind_mtx[1].xyz, sv));
        tevcoord = texel + off;
        const float4 t = tex[0].Sample(samp[0], tevcoord / float2(tw, th)).rgba;
        const float4 ras = i.color.rrra;
        const float ka = material.konst[0].a;
        const float3 rc = saturate((r2.rgb + lerp(float3(0.0, 0.0, 0.0), t.rgb, ras.rgb)));
        const float ra = (0.0 + lerp(ka, 0.0, ras.a));
        prev.rgb = rc;
        prev.a = ra;
    }
    {  // stage 1
        const float4 t = float4(0.0, 0.0, 0.0, 0.0);
        const float4 ras = i.color.ggga;
        const float3 rc = saturate((prev.aaa + lerp(float3(0.0, 0.0, 0.0), r1.rgb, prev.rgb)));
        const float ra = (0.0 + lerp(0.0, 0.0, 0.0));
        prev.rgb = rc;
        prev.a = ra;
    }
    return saturate(prev);
}
