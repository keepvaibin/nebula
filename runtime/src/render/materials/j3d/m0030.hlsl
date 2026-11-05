// J3D material 30 (tools/dev/gen_j3d_materials.py from the game's material set; E-04)
// constants 0x300e0099
struct MaterialConstants {
    float4 reg1;
    float4 kcolor0;
    float4 kcolor1;
    float4 mat_color0;
    float4 fog;
    float4 fog_color;
    float4 alpha_ref;
    float4 ovr;
    float4 ovr_coord;
    float4 coord[8];
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
    {  // coordinate 2: kind 0, projective 0, row 5
        const float4 c = float4(v.uv[0], 1.0, 1.0);
        o.uv[2] = float4(dot(c, material.coord[6]), dot(c, material.coord[7]), 1.0, 0.0);
    }
    j3d_vertex_override(v, o);
}
float4 material_color(NativeVaryings i) {
    [branch] if (material.ovr.x != 0.0) {
        const float4 t = tex[0].Sample(samp[0], j3d_coord(i.uv[0]));
        const float4 ras = float4(0.0, 0.0, 0.0, 0.0);
        const float4 prev = float4(1.0, 1.0, 1.0, 1.0);
        const float ka = material.kcolor0.a;
        return j3d_override(t, i.color, material.kcolor0.a, saturate((t.a + lerp(0.0, 0.0, 0.0))), i.pos);
    }
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    float4 r0 = material.reg1;
    {  // stage 0
        const float4 t = tex[0].Sample(samp[0], j3d_coord(i.uv[0])).rgba;
        const float4 ras = i.color.rgba;
        const float3 rc = saturate((t.rgb + lerp(float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0))));
        const float ra = saturate((t.a + lerp(0.0, 0.0, 0.0)));
        prev.rgb = rc;
        prev.a = ra;
    }
    {  // stage 1
        const float4 t = tex[1].Sample(samp[1], j3d_coord(i.uv[1])).rgba;
        const float4 ras = i.color.rgba;
        const float ka = 1.0;
        const float3 rc = saturate((prev.rgb + lerp(float3(0.0, 0.0, 0.0), float3(1.0, 1.0, 1.0), t.rgb)));
        const float ra = saturate((prev.a + lerp(0.0, ka, t.a)));
        prev.rgb = rc;
        prev.a = ra;
    }
    {  // stage 2
        const float4 t = tex[2].Sample(samp[2], j3d_coord(i.uv[2])).rgba;
        const float4 ras = i.color.rgba;
        const float ka = material.kcolor0.a;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(j3d_in(r0.rgb), float3(1.0, 1.0, 1.0), prev.rgb)));
        const float ra = saturate((0.0 + lerp(ka, t.a, prev.a)));
        prev.rgb = rc;
        prev.a = ra;
    }
    {  // stage 3
        const float4 t = float4(0.0, 0.0, 0.0, 0.0);
        const float4 ras = i.color.rgba;
        const float ka = material.kcolor1.a;
        const float3 rc = saturate((prev.rgb + lerp(float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0))));
        const float ra = saturate((0.0 + lerp(0.0, ka, prev.a)));
        prev.rgb = rc;
        prev.a = ra;
    }
    prev = (float4(prev.rgb, prev.a));
    const float a8 = round(prev.a * 255.0);
    if (!((a8 >= round(material.alpha_ref.x * 255.0)) && (true))) {
        discard;
    }
    prev.rgb = j3d_fog(prev.rgb, i.pos);
    return prev;
}
