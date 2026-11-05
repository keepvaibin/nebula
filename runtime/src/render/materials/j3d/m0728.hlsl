// J3D material 728 (tools/dev/gen_j3d_materials.py from the game's material set; E-04)
// constants 0x3006008b
struct MaterialConstants {
    float4 reg1;
    float4 reg2;
    float4 kcolor0;
    float4 mat_color0;
    float4 fog;
    float4 fog_color;
    float4 ovr;
    float4 ovr_coord;
};
#include "j3d_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color.rgb = material.mat_color0.rgb;
    o.color.a = material.mat_color0.a;
    o.color1 = v.color1;
    j3d_vertex_override(v, o);
}
float4 material_color(NativeVaryings i) {
    [branch] if (material.ovr.x != 0.0) {
        const float4 t = float4(0.0, 0.0, 0.0, 0.0);
        const float4 ras = float4(0.0, 0.0, 0.0, 0.0);
        const float4 prev = float4(1.0, 1.0, 1.0, 1.0);
        const float4 r0 = material.reg1;
        const float ka = material.kcolor0.a;
        return j3d_override(t, i.color, material.kcolor0.a, saturate((0.0 + lerp(j3d_in(r0.a), 0.0, 0.0))), i.pos);
    }
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    float4 r0 = material.reg1;
    float4 r1 = material.reg2;
    {  // stage 0
        const float4 t = tex[0].Sample(samp[0], j3d_coord(i.uv[0])).rgba;
        const float4 ras = i.color.rgba;
        const float3 rc = (ras.rgb + lerp(j3d_in(r0.rgb), j3d_in(r1.rgb), t.rgb) - 128.0 / 255.0);
        const float ra = saturate((0.0 + lerp(j3d_in(r0.a), 0.0, 0.0)));
        prev.rgb = rc;
        prev.a = ra;
    }
    prev = j3d_out(float4(prev.rgb, prev.a));
    prev.rgb = j3d_fog(prev.rgb, i.pos);
    return prev;
}
