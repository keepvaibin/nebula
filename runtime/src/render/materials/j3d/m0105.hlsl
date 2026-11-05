// J3D material 105 (tools/dev/gen_j3d_materials.py from the game's material set; E-04)
// constants 0x30060081
struct MaterialConstants {
    float4 reg1;
    float4 mat_color0;
    float4 fog;
    float4 fog_color;
    float4 ovr;
    float4 ovr_coord;
    float4 coord[2];
};
#include "j3d_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color.rgb = material.mat_color0.rgb;
    o.color.a = material.mat_color0.a;
    o.color1 = v.color1;
    {  // coordinate 0: kind 0, projective 0, row 5
        const float4 c = float4(v.uv[0], 1.0, 1.0);
        o.uv[0] = float4(dot(c, material.coord[0]), dot(c, material.coord[1]), 1.0, 0.0);
    }
    j3d_vertex_override(v, o);
}
float4 material_color(NativeVaryings i) {
    [branch] if (material.ovr.x != 0.0) {
        const float4 t = tex[0].Sample(samp[0], j3d_coord(i.uv[0]));
        const float4 ras = float4(0.0, 0.0, 0.0, 0.0);
        const float4 prev = float4(1.0, 1.0, 1.0, 1.0);
        const float4 r0 = material.reg1;
        const float ka = 1.0;
        return j3d_override(t, i.color, 1.0, saturate(0.0 + (round(t.a * 255.0) > round(ka * 255.0) ? j3d_in(r0.a) : 0.0)), i.pos);
    }
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    float4 r0 = material.reg1;
    {  // stage 0
        const float4 t = tex[0].Sample(samp[0], j3d_coord(i.uv[0])).rrrr;
        const float4 ras = i.color.rrrr;
        const float3 k = float4(float3(1.0, 1.0, 1.0), 0.0).rgb;
        const float ka = 1.0;
        const float3 rc = saturate(float3(0.0, 0.0, 0.0) + float3(round(t.aaa * 255.0) > round(k.rgb * 255.0)) * float3(0.0, 0.0, 0.0));
        const float ra = saturate(0.0 + (round(t.a * 255.0) > round(ka * 255.0) ? j3d_in(r0.a) : 0.0));
        prev.rgb = rc;
        prev.a = ra;
    }
    prev = (float4(prev.rgb, prev.a));
    prev.rgb = j3d_fog(prev.rgb, i.pos);
    return prev;
}
