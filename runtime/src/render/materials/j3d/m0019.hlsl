// J3D material 19 (tools/dev/gen_j3d_materials.py from the game's material set; E-04)
#include "j3d_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    {  // coordinate 0: type 1, source 4, matrix 60
        const float4 c = float4(v.uv[0], 1.0, 1.0);
        float3 res = float3(c.xy, 1.0);
        res = float3(dot(float4(res, 1.0), inline_post_matrices[0]), dot(float4(res, 1.0), inline_post_matrices[1]), dot(float4(res, 1.0), inline_post_matrices[2]));
        o.uv[0] = float4(res, 1.0);
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
        const float3 k = float4(material.konst[0].rgb, 0.0).rgb;
        const float ka = material.konst[0].a;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(float3(0.0, 0.0, 0.0), k.rgb, t.rgb)) * 2.0);
        const float ra = saturate((0.0 + lerp(0.0, t.a, ka)));
        prev.rgb = rc;
        prev.a = ra;
    }
    return saturate(prev);
}
