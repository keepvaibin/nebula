// J3D material 1224 (tools/dev/gen_j3d_materials.py from the game's material set; E-04)
#include "j3d_material.hlsli"

void j3d_channels(float3 eye, float3 nrm, float4 vtx0, float4 vtx1, out float4 lit0, out float4 lit1) {
    lit0 = vtx0;
    lit1 = vtx1;
    lit0.rgb = vtx0.rgb * saturate((material.amb_color[0].rgb * 255.0 + j3d_light(0u, 3u, 2u, eye, nrm).rgb + j3d_light(1u, 3u, 2u, eye, nrm).rgb) / 255.0);
    lit0.a = vtx0.a;
    lit1 = vtx1;
}
#if NATIVE_PIXEL_LIT
void j3d_channels_enhanced(float3 eye, float3 nrm, float4 vtx0, float4 vtx1, out float4 lit0, out float4 lit1) {
    lit0 = vtx0;
    lit1 = vtx1;
    {
        const float3 enh_mat = vtx0.rgb;
        const float3 enh_amb = material.amb_color[0].rgb;
        const float3 enh_amb_lin = enh_lin(enh_amb);
        float3 enh_acc = enh_amb_lin * enh_sky(g_enh);
        enh_add_light(0u, true, 2u, true, false, eye, enh_amb, enh_amb_lin, enh_acc);
        enh_add_light(1u, true, 2u, false, false, eye, enh_amb, enh_amb_lin, enh_acc);
        enh_acc *= enh_sky_reflection(g_enh, enh_amb_lin);
        lit0.rgb = enh_mat * enh_srgb(enh_shoulder(max(enh_acc, 0.0)));
    }
    lit0.a = vtx0.a;
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
    j3d_vertex_override(v, o);
}
float4 material_color(NativeVaryings i) {
#if NATIVE_PIXEL_LIT
    j3d_pixel_begin(i);
    j3d_pixel_channels(i);
#endif
    [branch] if (material.ovr.x != 0.0) {
        const float4 t = float4(0.0, 0.0, 0.0, 0.0);
        const float4 ras = float4(0.0, 0.0, 0.0, 0.0);
        const float4 prev = material.reg[0];
        const float4 r0 = material.reg[1];
        const float4 r1 = material.reg[2];
        const float4 r2 = material.reg[3];
        const float ka = material.kcolor[0].a;
        return j3d_override(t, i.color, material.kcolor[0].a, saturate((0.0 + lerp(ras.a, 0.0, 0.0))), i.pos);
    }
    float4 prev = material.reg[0];
    float4 r0 = material.reg[1];
    float4 r1 = material.reg[2];
    float4 r2 = material.reg[3];
    {  // stage 0
        const float4 t = float4(0.0, 0.0, 0.0, 0.0);
        const float4 ras = i.color.rgba;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(ras.rgb, j3d_in(r0.rgb), float3(0.0, 0.0, 0.0))) * 2.0);
        const float ra = saturate((0.0 + lerp(ras.a, 0.0, 0.0)));
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
