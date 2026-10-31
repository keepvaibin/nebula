// J3D material 933 (tools/dev/gen_j3d_materials.py from the game's material set; E-04)
#include "j3d_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
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
        const float4 t = float4(0.0, 0.0, 0.0, 0.0);
        const float4 ras = i.color.rgba;
        const float ka = material.konst[0].a;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(r1.rgb, r0.rgb, ras.rgb)) * 2.0);
        const float ra = saturate((0.0 + lerp(0.0, ka, ras.a)));
        prev.rgb = rc;
        prev.a = ra;
    }
    {  // stage 1
        const float4 t = float4(0.0, 0.0, 0.0, 0.0);
        const float4 ras = i.color.rgba;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(float3(0.0, 0.0, 0.0), prev.rgb, float3(1.0, 1.0, 1.0))) * 2.0);
        const float ra = (0.0 + lerp(ras.a, 0.0, 0.0));
        prev.rgb = rc;
        prev.a = ra;
    }
    return saturate(prev);
}
