// J3D material 928 (tools/dev/gen_j3d_materials.py from the game's material set; E-04)
// constants 0x30060283
struct MaterialConstants {
    float4 reg1;
    float4 reg2;
    float4 mat_color0;
    float4 amb_color0;
    float4 fog;
    float4 fog_color;
    float4 ovr;
    float4 ovr_coord;
};
#include "j3d_material.hlsli"

void j3d_channels(float3 eye, float3 nrm, float4 vtx0, float4 vtx1, out float4 lit0, out float4 lit1) {
    lit0 = vtx0;
    lit1 = vtx1;
    lit0.rgb = vtx0.rgb * saturate((material.amb_color0.rgb * 255.0 + j3d_light(0u, 3u, 2u, eye, nrm).rgb + j3d_light(1u, 3u, 2u, eye, nrm).rgb) / 255.0);
    lit0.a = material.mat_color0.a;
    lit1 = vtx1;
}
#if NATIVE_PIXEL_LIT
void j3d_channels_enhanced(float3 eye, float3 nrm, float4 vtx0, float4 vtx1, out float4 lit0, out float4 lit1) {
    lit0 = vtx0;
    lit1 = vtx1;
    {
        const float3 enh_mat = vtx0.rgb;
        const float3 enh_amb = material.amb_color0.rgb;
        const float3 enh_amb_lin = enh_lin(enh_amb);
        float3 enh_acc = enh_amb_lin * enh_sky(g_enh);
        enh_add_light(0u, true, 2u, true, false, eye, enh_amb, enh_amb_lin, enh_acc);
        enh_add_light(1u, true, 2u, false, false, eye, enh_amb, enh_amb_lin, enh_acc);
        enh_acc *= enh_sky_reflection(g_enh, enh_amb_lin);
        lit0.rgb = enh_mat * enh_srgb(enh_shoulder(max(enh_acc, 0.0)));
    }
    lit0.a = material.mat_color0.a;
    lit1 = vtx1;
}
#endif
void material_vertex(NativeVertex v, inout NativeVaryings o) {
    const uint nq = v.pn_row & 31u;
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
    {  // coordinate 0: kind 0, projective 0, row 5
        const float4 c = float4(v.uv[0], 1.0, 1.0);
        float3 res;
        if (((vs_header.w >> 0u) & 1u) != 0u) {
            const uint tm = native_tex_matrix(v, 0u);
            res = float3(dot(c, native_matrix_row(tm + 0u)), dot(c, native_matrix_row(tm + 1u)), 1.0);
        } else {
            res = float3(dot(c, native_tex_row(0)), dot(c, native_tex_row(1)), 1.0);
        }
        const uint pt = native_post(0u);
        const bool posted = (pt & 0x3Fu) != 61u;
        if (posted) {
            if ((pt & 0x100u) != 0u) res = dot(res, res) > 0.0 ? normalize(res) : res;
            res = float3(dot(float4(res, 1.0), native_coord_post_row(0)), dot(float4(res, 1.0), native_coord_post_row(1)), dot(float4(res, 1.0), native_coord_post_row(2)));
        }
        if (posted && res.z == 0.0) res.xy = clamp(res.xy * 0.5, -1.0, 1.0);
        o.uv[0] = float4(res, posted ? 1.0 : 0.0);
    }
    j3d_vertex_override(v, o);
}
float4 material_color(NativeVaryings i) {
#if NATIVE_PIXEL_LIT
    j3d_pixel_begin(i);
    j3d_pixel_channels(i);
#endif
    [branch] if (material.ovr.x != 0.0) {
        const float4 t = tex[0].Sample(samp[0], j3d_coord(i.uv[0]));
        const float4 ras = float4(0.0, 0.0, 0.0, 0.0);
        const float4 prev = float4(1.0, 1.0, 1.0, 1.0);
        const float4 r0 = material.reg1;
        const float ka = 64.0 / 255.0;
        return j3d_override(t, i.color, 64.0 / 255.0, saturate((r0.a + lerp(t.a, ka, ras.a)) * 2.0), i.pos);
    }
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    float4 r0 = material.reg1;
    float4 r1 = material.reg2;
    {  // stage 0
        const float4 t = tex[0].Sample(samp[0], j3d_coord(i.uv[0])).rgba;
        const float4 ras = i.color.rgba;
        const float ka = 64.0 / 255.0;
        const float3 rc = saturate((r0.rgb + lerp(j3d_in(r1.rgb), t.rgb, ras.rgb)));
        const float ra = saturate((r0.a + lerp(t.a, ka, ras.a)) * 2.0);
        prev.rgb = rc;
        prev.a = ra;
    }
    {  // stage 1
        const float4 t = float4(0.0, 0.0, 0.0, 0.0);
        const float4 ras = i.color.rgba;
        const float ka = 64.0 / 255.0;
        const float3 rc = saturate((prev.rgb + lerp(float3(0.0, 0.0, 0.0), ras.rgb, prev.aaa)) * 2.0);
        const float ra = saturate((r0.a + lerp(0.0, ka, ras.a)) * 2.0);
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
