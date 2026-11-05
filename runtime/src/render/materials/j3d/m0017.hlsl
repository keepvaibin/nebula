// J3D material 17 (tools/dev/gen_j3d_materials.py from the game's material set; E-04)
// constants 0x3006009b
struct MaterialConstants {
    float4 reg1;
    float4 reg2;
    float4 kcolor0;
    float4 kcolor1;
    float4 mat_color0;
    float4 fog;
    float4 fog_color;
    float4 ovr;
    float4 ovr_coord;
    float4 coord[5];
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
    {  // coordinate 1: kind 0, projective 0, row 5
        const float4 c = float4(v.uv[0], 1.0, 1.0);
        o.uv[1] = float4(dot(c, material.coord[3]), dot(c, material.coord[4]), 1.0, 0.0);
    }
    j3d_vertex_override(v, o);
}
float4 material_color(NativeVaryings i) {
    [branch] if (material.ovr.x != 0.0) {
        const float4 t = tex[0].Sample(samp[0], j3d_coord(i.uv[0]));
        const float4 ras = float4(0.0, 0.0, 0.0, 0.0);
        const float4 prev = float4(1.0, 1.0, 1.0, 1.0);
        const float ka = material.kcolor0.a;
        return j3d_override(t, i.color, material.kcolor0.a, saturate((0.0 + lerp(0.0, ka, t.a))), i.pos);
    }
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    float4 r0 = material.reg1;
    float4 r1 = material.reg2;
    {  // stage 0
        const float4 t = tex[0].Sample(samp[0], j3d_coord(i.uv[0])).rgba;
        const float4 ras = i.color.rgba;
        const float3 k = float4(material.kcolor0.rgb, 0.0).rgb;
        const float ka = material.kcolor0.a;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(float3(0.0, 0.0, 0.0), k.rgb, t.rgb)));
        const float ra = saturate((0.0 + lerp(0.0, ka, t.a)));
        prev.rgb = rc;
        prev.a = ra;
    }
    {  // stage 1
        const float4 t = tex[1].Sample(samp[1], j3d_coord(i.uv[1])).rgba;
        const float4 ras = i.color.rgba;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(float3(0.0, 0.0, 0.0), prev.rgb, t.rgb)) * 2.0);
        const float ra = (0.0 + lerp(0.0, prev.a, t.a)) * 2.0;
        prev.rgb = rc;
        prev.a = ra;
    }
    {  // stage 2
        const float4 t = float4(0.0, 0.0, 0.0, 0.0);
        const float4 ras = i.color.rgba;
        const float3 rc = prev.rgb + ((round((prev.rgb).r * 255.0)) > (round((j3d_in(r0.aaa)).r * 255.0)) ? j3d_in(r0.rgb) : float3(0.0, 0.0, 0.0));
        const float ra = (0.0 + lerp(ras.a, 0.0, 0.0));
        prev.rgb = rc;
        prev.a = ra;
    }
    {  // stage 3
        const float4 t = float4(0.0, 0.0, 0.0, 0.0);
        const float4 ras = i.color.rgba;
        const float3 k = float4(material.kcolor1.rgb, 0.0).rgb;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(j3d_in(r1.rgb), k.rgb, j3d_in(prev.rgb))));
        const float ra = (0.0 + lerp(ras.a, 0.0, 0.0));
        prev.rgb = rc;
        prev.a = ra;
    }
    prev = j3d_out(float4(prev.rgb, prev.a));
    prev.rgb = j3d_fog(prev.rgb, i.pos);
    return prev;
}
