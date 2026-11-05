// J3D material 537 (tools/dev/gen_j3d_materials.py from the game's material set; E-04)
// constants 0x30461889
struct MaterialConstants {
    float4 reg1;
    float4 kcolor0;
    float4 mat_color0;
    float4 ind_mtx0;
    float4 ind_mtx1;
    float4 fog;
    float4 fog_color;
    float4 tex_scale2;
    float4 ovr;
    float4 ovr_coord;
    float4 coord[8];
};
#include "j3d_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
#if NATIVE_PIXEL_LIT
    const uint nq = v.pn_row;
    const float3 nt = float3(dot(native_normal_row(nq).xyz, v.nrm), dot(native_normal_row(nq + 1u).xyz, v.nrm), dot(native_normal_row(nq + 2u).xyz, v.nrm));
    const float3 nrm = dot(nt, nt) > 0.0 ? normalize(nt) : nt;
    o.lit_eye = v.eye;
    o.lit_nrm = nrm;
#endif
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
    {  // coordinate 2: kind 0, projective 0, row 1
        const float4 c = float4(j3d_eye_normal(v), 1.0);
        o.uv[2] = float4(dot(c, material.coord[6]), dot(c, material.coord[7]), 1.0, 0.0);
    }
    j3d_vertex_override(v, o);
}
float4 material_color(NativeVaryings i) {
#if NATIVE_PIXEL_LIT
    j3d_pixel_begin(i);
    if (j3d_enhanced()) enh_key_highlight(g_enh, i.lit_eye);
#endif
    [branch] if (material.ovr.x != 0.0) {
        const float4 t = tex[0].Sample(samp[0], j3d_coord(i.uv[0]));
        const float4 ras = float4(0.0, 0.0, 0.0, 0.0);
        const float4 prev = float4(1.0, 1.0, 1.0, 1.0);
        const float ka = material.kcolor0.a;
        return j3d_override(t, i.color, material.kcolor0.a, saturate((0.0 + lerp(0.0, t.a, ras.a))), i.pos);
    }
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    float4 r0 = material.reg1;
    float2 tevcoord = float2(0.0, 0.0);
    const float3 ind0 = round(tex[1].Sample(samp[1], j3d_coord(i.uv[1]) / float2(1.0, 1.0)).abg * 255.0);
    {  // stage 0
        const float4 t = tex[0].Sample(samp[0], j3d_coord(i.uv[0])).rgba;
        const float4 ras = i.color.rgba;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(float3(0.0, 0.0, 0.0), t.rgb, ras.rgb)));
        const float ra = saturate((0.0 + lerp(0.0, t.a, ras.a)));
        prev.rgb = rc;
        prev.a = ra;
    }
    {  // stage 1
        float2 texel = (j3d_coord(i.uv[2]) * material.tex_scale2.xy);
        float3 sv = floor(ind0 / 1.0);
        sv.x += -128.0;
        sv.y += -128.0;
        sv.z += -128.0;
        const float2 off = float2(dot(material.ind_mtx0.xyz, sv), dot(material.ind_mtx1.xyz, sv));
        tevcoord = texel + off;
        const float4 t = tex[2].Sample(samp[2], tevcoord / material.tex_scale2.zw).rgba;
        const float4 ras = i.color.rgba;
        const float3 rc = saturate((prev.rgb + lerp(float3(0.0, 0.0, 0.0), ras.rgb, t.rgb)));
        const float ra = (0.0 + lerp(ras.a, 0.0, 0.0));
        prev.rgb = rc;
        prev.a = ra;
    }
    {  // stage 2
        const float4 t = float4(0.0, 0.0, 0.0, 0.0);
        const float4 ras = i.color.rgba;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(float3(0.0, 0.0, 0.0), prev.rgb, j3d_in(r0.rgb))) * 2.0);
        const float ra = (0.0 + lerp(ras.a, 0.0, 0.0));
        prev.rgb = rc;
        prev.a = ra;
    }
    prev = j3d_out(float4(prev.rgb, prev.a));
#if NATIVE_PIXEL_LIT
    if (j3d_enhanced()) prev.rgb = enh_finish(prev.rgb, g_enh);
#endif
    prev.rgb = j3d_fog(prev.rgb, i.pos);
    return prev;
}
