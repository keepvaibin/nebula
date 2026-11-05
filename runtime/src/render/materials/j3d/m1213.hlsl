// J3D material 1213 (tools/dev/gen_j3d_materials.py from the game's material set; E-04)
// constants 0x30060001
struct MaterialConstants {
    float4 reg1;
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
        const float ka = 1.0;
        return j3d_override(t, i.color, 1.0, saturate((ras.a - lerp(ka, 0.0, j3d_in(r0.a)))), i.pos);
    }
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    float4 r0 = material.reg1;
    {  // stage 0
        const float4 t = float4(0.0, 0.0, 0.0, 0.0);
        const float4 ras = i.color.rgba;
        const float ka = 1.0;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(ras.rgb, float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0))));
        const float ra = saturate((ras.a - lerp(ka, 0.0, j3d_in(r0.a))));
        prev.rgb = rc;
        prev.a = ra;
    }
    prev = (float4(prev.rgb, prev.a));
    prev.rgb = j3d_fog(prev.rgb, i.pos);
    return prev;
}
