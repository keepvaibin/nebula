// J3D material 712 (tools/dev/gen_j3d_materials.py from the game's material set; E-04)
#include "j3d_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    {  // coordinate 0: type 0, source 0, matrix 30
        const float4 c = float4(v.pos, 1.0);
        float3 res = float3(dot(c, inline_tex_matrices[0]), dot(c, inline_tex_matrices[1]), dot(c, inline_tex_matrices[2]));
        res = float3(dot(float4(res, 1.0), inline_post_matrices[0]), dot(float4(res, 1.0), inline_post_matrices[1]), dot(float4(res, 1.0), inline_post_matrices[2]));
        o.uv[0] = float4(res, 1.0);
    }
    {  // coordinate 1: type 0, source 0, matrix 33
        const float4 c = float4(v.pos, 1.0);
        float3 res = float3(dot(c, inline_tex_matrices[3]), dot(c, inline_tex_matrices[4]), dot(c, inline_tex_matrices[5]));
        res = float3(dot(float4(res, 1.0), inline_post_matrices[3]), dot(float4(res, 1.0), inline_post_matrices[4]), dot(float4(res, 1.0), inline_post_matrices[5]));
        o.uv[1] = float4(res, 1.0);
    }
    {  // coordinate 2: type 0, source 0, matrix 36
        const float4 c = float4(v.pos, 1.0);
        float3 res = float3(dot(c, inline_tex_matrices[6]), dot(c, inline_tex_matrices[7]), dot(c, inline_tex_matrices[8]));
        res = float3(dot(float4(res, 1.0), inline_post_matrices[6]), dot(float4(res, 1.0), inline_post_matrices[7]), dot(float4(res, 1.0), inline_post_matrices[8]));
        o.uv[2] = float4(res, 1.0);
    }
    {  // coordinate 3: type 0, source 0, matrix 39
        const float4 c = float4(v.pos, 1.0);
        float3 res = float3(dot(c, inline_tex_matrices[9]), dot(c, inline_tex_matrices[10]), dot(c, inline_tex_matrices[11]));
        res = float3(dot(float4(res, 1.0), inline_post_matrices[9]), dot(float4(res, 1.0), inline_post_matrices[10]), dot(float4(res, 1.0), inline_post_matrices[11]));
        o.uv[3] = float4(res, 1.0);
    }
    o.color.rgb = v.color0.rgb;
    o.color.a = v.color0.a;
    o.color1 = v.color1;
}
float4 material_color(NativeVaryings i) {
    float4 prev = material.reg[0];
    float4 r0 = material.reg[1];
    float4 r1 = material.reg[2];
    float4 r2 = material.reg[3];
    {  // stage 0
        const float4 t = tex[0].Sample(samp[0], j3d_coord(i.uv[0])).rgba;
        const float4 ras = i.color.rgba;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(t.rgb, float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0))));
        const float ra = saturate((0.0 + lerp(0.0, t.a, ras.a)));
        r2.rgb = rc;
        prev.a = ra;
    }
    {  // stage 1
        const float4 t = tex[2].Sample(samp[2], j3d_coord(i.uv[2])).rgba;
        const float4 ras = i.color.rgba;
        const float3 rc = (float3(0.0, 0.0, 0.0) + lerp(t.rgb, float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0)));
        const float ra = (0.0 + lerp(ras.a, 0.0, 0.0));
        prev.rgb = rc;
        prev.a = ra;
    }
    {  // stage 2
        const float4 t = tex[1].Sample(samp[1], j3d_coord(i.uv[1])).rgba;
        const float4 ras = i.color.rgba;
        const float3 rc = (float3(0.0, 0.0, 0.0) + lerp(r2.rgb, prev.rgb, t.rgb));
        const float ra = (0.0 + lerp(ras.a, 0.0, 0.0));
        prev.rgb = rc;
        prev.a = ra;
    }
    {  // stage 3
        const float4 t = tex[3].Sample(samp[3], j3d_coord(i.uv[3])).rgba;
        const float4 ras = i.color.rgba;
        const float3 k = float4(material.konst[0].rgb, 0.0).rgb;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(prev.rgb, k.rgb, t.rgb)));
        const float ra = (0.0 + lerp(ras.a, 0.0, 0.0));
        prev.rgb = rc;
        prev.a = ra;
    }
    return saturate(prev);
}
