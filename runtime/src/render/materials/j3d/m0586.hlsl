// J3D material 586 (tools/dev/gen_j3d_materials.py from the game's material set; E-04)
// constants 0x3006008c
struct MaterialConstants {
    float4 reg3;
    float4 kcolor0;
    float4 mat_color0;
    float4 fog;
    float4 fog_color;
    float4 ovr;
    float4 ovr_coord;
    float4 coord[6];
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
    {  // coordinate 0: kind 0, projective 0, row 1
        const float4 c = float4(j3d_eye_normal(v), 1.0);
        o.uv[0] = float4(dot(c, material.coord[0]), dot(c, material.coord[1]), 1.0, 0.0);
    }
    {  // coordinate 1: kind 0, projective 1, row 1
        const float4 c = float4(j3d_eye_normal(v), 1.0);
        float3 res = float3(dot(c, material.coord[3]), dot(c, material.coord[4]), dot(c, material.coord[5]));
        if (res.z == 0.0) res.xy = clamp(res.xy * 0.5, -1.0, 1.0);
        o.uv[1] = float4(res, 1.0);
    }
    j3d_vertex_override(v, o);
}
float4 material_color(NativeVaryings i) {
#if NATIVE_PIXEL_LIT
    j3d_pixel_begin(i);
    if (j3d_enhanced()) enh_key_highlight(g_enh, i.lit_eye);
#endif
    [branch] if (material.ovr.x != 0.0) {
        const float4 t = J3D_ENV(tex[0].Sample(samp[0], j3d_coord(i.uv[0])));
        const float4 ras = float4(0.0, 0.0, 0.0, 0.0);
        const float4 prev = float4(1.0, 1.0, 1.0, 1.0);
        const float4 r2 = material.reg3;
        const float ka = material.kcolor0.a;
        return j3d_override(t, i.color, material.kcolor0.a, saturate((0.0 + lerp(j3d_in(r2.a), 0.0, 0.0))), i.pos);
    }
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    float4 r2 = material.reg3;
    {  // stage 0
        const float4 t = J3D_ENV(tex[0].Sample(samp[0], j3d_coord(i.uv[0])).rgba);
        const float4 ras = i.color.rgba;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(float3(0.0, 0.0, 0.0), t.rgb, ras.rgb)) * 4.0);
        const float ra = saturate((0.0 + lerp(j3d_in(r2.a), 0.0, 0.0)));
        prev.rgb = rc;
        prev.a = ra;
    }
    {  // stage 1
        const float4 t = J3D_ENV(tex[1].Sample(samp[1], j3d_coord(i.uv[1])).rgba);
        const float4 ras = i.color.rgba;
        const float3 rc = saturate((prev.rgb + lerp(t.rgb, float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0))));
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
