// J3D material 30 (tools/dev/gen_j3d_materials.py from the game's material set; E-04)
#include "j3d_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    {  // coordinate 0: type 1, source 4, matrix 60
        const float4 c = float4(v.uv[0], 1.0, 1.0);
        float3 res = float3(c.xy, 1.0);
        o.uv[0] = float4(res, 0.0);
    }
    {  // coordinate 1: type 1, source 4, matrix 60
        const float4 c = float4(v.uv[0], 1.0, 1.0);
        float3 res = float3(c.xy, 1.0);
        o.uv[1] = float4(res, 0.0);
    }
    {  // coordinate 2: type 1, source 4, matrix 60
        const float4 c = float4(v.uv[0], 1.0, 1.0);
        float3 res = float3(c.xy, 1.0);
        o.uv[2] = float4(res, 0.0);
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
    float2 tevcoord = float2(0.0, 0.0);
    const float3 ind0 = round(tex[2].Sample(samp[2], j3d_coord(i.uv[2]) / float2(1.0, 1.0)).abg * 255.0);
    {  // stage 0
        const float4 t = tex[0].Sample(samp[0], j3d_coord(i.uv[0])).rrrr;
        const float4 ras = i.color.rrrr;
        const float ka = 1.0;
        const float3 rc = saturate((t.rgb + lerp(float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0))));
        const float ra = saturate((ka + lerp(0.0, 0.0, 0.0)));
        prev.rgb = rc;
        prev.a = ra;
    }
    {  // stage 1
        float tw, th;
        tex[1].GetDimensions(tw, th);
        float2 texel = j3d_coord(i.uv[1]) * float2(tw, th);
        texel.x = 0.0;
        texel.y = 0.0;
        float3 sv = floor(ind0 / 1.0);
        sv.x += -128.0;
        sv.y += -128.0;
        const float2 off = float2(dot(material.ind_mtx[0].xyz, sv), dot(material.ind_mtx[1].xyz, sv));
        tevcoord = texel + off;
        const float4 t = tex[1].Sample(samp[1], tevcoord / float2(tw, th)).rrrr;
        const float4 ras = i.color.rrrr;
        const float3 rc = saturate((prev.rgb + lerp(float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0))));
        const float ra = saturate((0.0 + lerp(0.0, t.a, ras.a)));
        prev.rgb = rc;
        prev.a = ra;
    }
    return saturate(prev);
}
