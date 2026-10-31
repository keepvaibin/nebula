// J3D material 2 (tools/dev/gen_j3d_materials.py from the game's material set; E-04)
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
    o.color.rgb = material.mat_color[0].rgb;
    o.color.a = material.mat_color[0].a;
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
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(float3(0.5, 0.5, 0.5), float3(1.0, 1.0, 1.0), t.rgb) - 0.5) * 2.0);
        const float ra = saturate((t.a + lerp(0.0, 0.0, 0.0)) * 2.0);
        prev.rgb = rc;
        prev.a = ra;
    }
    {  // stage 1
        const float4 t = tex[1].Sample(samp[1], j3d_coord(i.uv[1])).rgba;
        const float4 ras = i.color.rgba;
        const float3 rc = saturate((prev.rgb + lerp(float3(0.0, 0.0, 0.0), r0.rgb, t.rgb)));
        const float ra = saturate((0.0 + lerp(0.0, t.a, prev.a)));
        prev.rgb = rc;
        prev.a = ra;
    }
    {  // stage 2
        const float4 t = float4(0.0, 0.0, 0.0, 0.0);
        const float4 ras = i.color.rgba;
        const float ka = material.konst[0].a;
        const float3 rc = saturate((prev.rgb + lerp(float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0))));
        const float ra = saturate((0.0 + lerp(0.0, ka, prev.a)));
        prev.rgb = rc;
        prev.a = ra;
    }
    const float a8 = round(saturate(prev.a) * 255.0);
    if (!((a8 >= round(material.alpha_ref.x * 255.0)) && (a8 <= round(material.alpha_ref.y * 255.0)))) {
        discard;
    }
    return saturate(prev);
}
