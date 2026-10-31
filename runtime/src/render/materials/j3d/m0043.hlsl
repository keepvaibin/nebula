// J3D material 43 (tools/dev/gen_j3d_materials.py from the game's material set; E-04)
#include "j3d_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    {  // coordinate 0: type 1, source 4, matrix 30
        const float4 c = float4(v.uv[0], 1.0, 1.0);
        float3 res = float3(dot(c, inline_tex_matrices[0]), dot(c, inline_tex_matrices[1]), 1.0);
        o.uv[0] = float4(res, 0.0);
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
        const float4 t = tex[0].Sample(samp[0], j3d_coord(i.uv[0])).rrrr;
        const float4 ras = i.color.rrrr;
        const float ka = 1.0;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(r0.rgb, float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0))));
        const float ra = saturate(0.0 + (round(t.a * 255.0) > round(ka * 255.0) ? r0.a : 0.0));
        prev.rgb = rc;
        prev.a = ra;
    }
    const float a8 = round(saturate(prev.a) * 255.0);
    if (!((a8 > round(material.alpha_ref.x * 255.0)) && (true))) {
        discard;
    }
    return saturate(prev);
}
