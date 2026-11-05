// J3D material 680 (tools/dev/gen_j3d_materials.py from the game's material set; E-04)
// constants 0x30161a89
struct MaterialConstants {
    float4 reg1;
    float4 kcolor0;
    float4 mat_color0;
    float4 amb_color0;
    float4 ind_mtx0;
    float4 ind_mtx1;
    float4 fog;
    float4 fog_color;
    float4 tex_scale0;
    float4 ovr;
    float4 ovr_coord;
    float4 coord[5];
};
#include "j3d_material.hlsli"

void j3d_channels(float3 eye, float3 nrm, float4 vtx0, float4 vtx1, out float4 lit0, out float4 lit1) {
    lit0 = vtx0;
    lit1 = vtx1;
    lit0.rgb = material.mat_color0.rgb * saturate((material.amb_color0.rgb * 255.0 + j3d_light(0u, 3u, 2u, eye, nrm).rgb) / 255.0);
    lit0.a = material.mat_color0.a;
    lit1 = vtx1;
}
#if NATIVE_PIXEL_LIT
void j3d_channels_enhanced(float3 eye, float3 nrm, float4 vtx0, float4 vtx1, out float4 lit0, out float4 lit1) {
    lit0 = vtx0;
    lit1 = vtx1;
    {
        const float3 enh_mat = material.mat_color0.rgb;
        const float3 enh_amb = material.amb_color0.rgb;
        const float3 enh_amb_lin = enh_lin(enh_amb);
        float3 enh_acc = enh_amb_lin * enh_sky(g_enh);
        enh_add_light(0u, true, 2u, true, true, eye, enh_amb, enh_amb_lin, enh_acc);
        enh_acc *= enh_sky_reflection(g_enh, enh_amb_lin);
        lit0.rgb = enh_mat * enh_srgb(enh_shoulder(max(enh_acc, 0.0)));
    }
    lit0.a = material.mat_color0.a;
    lit1 = vtx1;
}
#endif
void material_vertex(NativeVertex v, inout NativeVaryings o) {
    const uint nq = v.pn_row;
    const float3 nt = float3(dot(native_normal_row(nq).xyz, v.nrm), dot(native_normal_row(nq + 1u).xyz, v.nrm), dot(native_normal_row(nq + 2u).xyz, v.nrm));
    const float3 nrm = dot(nt, nt) > 0.0 ? normalize(nt) : nt;
#if NATIVE_PIXEL_LIT
    o.color = v.color0;
    o.color1 = v.color1;
    o.lit_eye = v.eye;
    o.lit_nrm = nrm;
#else
    j3d_channels(v.eye, nrm, v.color0, v.color1, o.color, o.color1);
#endif
    {  // coordinate 0: kind 0, projective 1, row 0
        const float4 c = float4(v.eye, 1.0);
        float3 res = float3(dot(c, material.coord[0]), dot(c, material.coord[1]), dot(c, material.coord[2]));
        if (res.z == 0.0) res.xy = clamp(res.xy * 0.5, -1.0, 1.0);
        o.uv[0] = float4(res, 1.0);
    }
    {  // coordinate 1: kind 0, projective 0, row 1
        const float4 c = float4(j3d_eye_normal(v), 1.0);
        o.uv[1] = float4(dot(c, material.coord[3]), dot(c, material.coord[4]), 1.0, 0.0);
    }
    j3d_vertex_override(v, o);
}
float4 material_color(NativeVaryings i) {
#if NATIVE_PIXEL_LIT
    j3d_pixel_begin(i);
    j3d_pixel_channels(i);
#endif
    [branch] if (material.ovr.x != 0.0) {
        const float4 t = tex[0].Sample(samp[0], (j3d_coord(i.uv[0]) * material.tex_scale0.xy) / material.tex_scale0.zw);
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
        float2 texel = (j3d_coord(i.uv[0]) * material.tex_scale0.xy);
        float3 sv = floor(ind0 / 1.0);
        sv.x += -128.0;
        sv.y += -128.0;
        sv.z += -128.0;
        const float2 off = float2(dot(material.ind_mtx0.xyz, sv), dot(material.ind_mtx1.xyz, sv));
        tevcoord = texel + off;
        const float4 t = tex[0].Sample(samp[0], tevcoord / material.tex_scale0.zw).rgba;
        const float4 ras = i.color.rgba;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(float3(0.0, 0.0, 0.0), t.rgb, j3d_in(r0.rgb))));
        const float ra = saturate((0.0 + lerp(0.0, t.a, ras.a)));
        prev.rgb = rc;
        prev.a = ra;
    }
    prev = (float4(prev.rgb, prev.a));
#if NATIVE_PIXEL_LIT
    if (j3d_enhanced()) prev.rgb = enh_finish(prev.rgb, g_enh);
#endif
    prev.rgb = j3d_fog(prev.rgb, i.pos);
    return prev;
}
