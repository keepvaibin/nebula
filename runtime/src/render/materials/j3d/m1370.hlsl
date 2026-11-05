// J3D material 1370 (tools/dev/gen_j3d_materials.py from the game's material set; E-04)
// constants 0x30060009
struct MaterialConstants {
    float4 reg1;
    float4 kcolor0;
    float4 fog;
    float4 fog_color;
    float4 ovr;
    float4 ovr_coord;
};
#include "j3d_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color.rgb = v.color0.rgb;
    o.color.a = v.color0.a;
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
        return j3d_override(t, i.color, material.kcolor0.a, saturate((0.0 + lerp(0.0, ras.a, j3d_in(r0.a))) * 2.0), i.pos);
    }
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    float4 r0 = material.reg1;
    {  // stage 0
        const float4 t = float4(0.0, 0.0, 0.0, 0.0);
        const float4 ras = i.color.rgba;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(float3(0.0, 0.0, 0.0), ras.rgb, j3d_in(r0.rgb))) * 2.0);
        const float ra = saturate((0.0 + lerp(0.0, ras.a, j3d_in(r0.a))) * 2.0);
        prev.rgb = rc;
        prev.a = ra;
    }
    prev = (float4(prev.rgb, prev.a));
    prev.rgb = j3d_fog(prev.rgb, i.pos);
    return prev;
}
