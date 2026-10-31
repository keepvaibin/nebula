// J3D material 768 (tools/dev/gen_j3d_materials.py from the game's material set; E-04)
#include "j3d_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color.rgb = v.color0.rgb;
    o.color.a = material.mat_color[0].a;
    o.color1 = v.color1;
}
float4 material_color(NativeVaryings i) {
    float4 prev = material.reg[0];
    float4 r0 = material.reg[1];
    float4 r1 = material.reg[2];
    float4 r2 = material.reg[3];
    {  // stage 0
        const float4 t = float4(0.0, 0.0, 0.0, 0.0);
        const float4 ras = i.color.rgba;
        const float ka = 0.25;
        const float3 rc = saturate((r0.rgb + lerp(r1.rgb, float3(1.0, 1.0, 1.0), r0.rgb)) * 2.0);
        const float ra = saturate((r0.a + lerp(0.0, ka, 0.0)) * 2.0);
        prev.rgb = rc;
        prev.a = ra;
    }
    return saturate(prev);
}
