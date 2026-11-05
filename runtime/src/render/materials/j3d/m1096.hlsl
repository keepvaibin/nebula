// J3D material 1096 (tools/dev/gen_j3d_materials.py from the game's material set; E-04)
// constants 0x302e1881
struct MaterialConstants {
    float4 reg1;
    float4 mat_color0;
    float4 ind_mtx0;
    float4 ind_mtx1;
    float4 fog;
    float4 fog_color;
    float4 alpha_ref;
    float4 tex_scale1;
    float4 ovr;
    float4 ovr_coord;
    float4 coord[6];
};
#include "j3d_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color.rgb = material.mat_color0.rgb;
    o.color.a = material.mat_color0.a;
    o.color1 = v.color1;
    {  // coordinate 0: kind 0, projective 1, row 5
        const float4 c = float4(v.uv[0], 1.0, 1.0);
        float3 res = float3(dot(c, material.coord[0]), dot(c, material.coord[1]), dot(c, material.coord[2]));
        if (res.z == 0.0) res.xy = clamp(res.xy * 0.5, -1.0, 1.0);
        o.uv[0] = float4(res, 1.0);
    }
    {  // coordinate 1: kind 0, projective 1, row 6
        const float4 c = float4(v.uv[1], 1.0, 1.0);
        float3 res = float3(dot(c, material.coord[3]), dot(c, material.coord[4]), dot(c, material.coord[5]));
        if (res.z == 0.0) res.xy = clamp(res.xy * 0.5, -1.0, 1.0);
        o.uv[1] = float4(res, 1.0);
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
        return j3d_override(t, i.color, 1.0, saturate((0.0 + lerp(j3d_in(r0.a), 0.0, 0.0))), i.pos);
    }
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    float4 r0 = material.reg1;
    float2 tevcoord = float2(0.0, 0.0);
    const float3 ind0 = round(tex[0].Sample(samp[0], j3d_coord(i.uv[0]) / float2(1.0, 1.0)).abg * 255.0);
    {  // stage 0
        float2 texel = (j3d_coord(i.uv[1]) * material.tex_scale1.xy);
        float3 sv = floor(ind0 / 1.0);
        sv.x += -128.0;
        sv.y += -128.0;
        sv.z += -128.0;
        const float2 off = float2(dot(material.ind_mtx0.xyz, sv), dot(material.ind_mtx1.xyz, sv));
        tevcoord = texel + off;
        const float4 t = tex[1].Sample(samp[1], tevcoord / material.tex_scale1.zw).rrrr;
        const float4 ras = float4(0.0, 0.0, 0.0, 0.0).rrrr;
        const float3 rc = saturate((t.rgb - lerp(j3d_in(r0.rgb), float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0))));
        const float ra = saturate((0.0 + lerp(j3d_in(r0.a), 0.0, 0.0)));
        prev.rgb = rc;
        prev.a = ra;
    }
    prev = (float4(prev.rgb, prev.a));
    const float a8 = round(prev.a * 255.0);
    if (!((a8 > round(material.alpha_ref.x * 255.0)) || (a8 > round(material.alpha_ref.y * 255.0)))) {
        discard;
    }
    prev.rgb = j3d_fog(prev.rgb, i.pos);
    return prev;
}
