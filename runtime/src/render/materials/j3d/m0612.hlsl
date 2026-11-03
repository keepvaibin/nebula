// J3D material 612 (tools/dev/gen_j3d_materials.py from the game's material set; E-04)
#include "j3d_material.hlsli"

void j3d_channels(float3 eye, float3 nrm, float4 vtx0, float4 vtx1, out float4 lit0, out float4 lit1) {
    lit0 = vtx0;
    lit1 = vtx1;
    lit0.rgb = vtx0.rgb;
    lit0.a = vtx0.a * saturate((vtx0.a * 255.0 + j3d_light(2u, 3u, 1u, eye, nrm).a) / 255.0);
    lit1 = vtx1;
}
#if NATIVE_PIXEL_LIT
void j3d_channels_enhanced(float3 eye, float3 nrm, float4 vtx0, float4 vtx1, out float4 lit0, out float4 lit1) {
    lit0 = vtx0;
    lit1 = vtx1;
    lit0.rgb = vtx0.rgb;
    lit0.a = vtx0.a * saturate((vtx0.a * 255.0 + j3d_light(2u, 3u, 1u, eye, nrm).a) / 255.0);
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
            res = float3(dot(c, inline_tex_matrices[0]), dot(c, inline_tex_matrices[1]), 1.0);
        }
        const uint pt = native_post(0u);
        const bool posted = (pt & 0x3Fu) != 61u;
        if (posted) {
            if ((pt & 0x100u) != 0u) res = dot(res, res) > 0.0 ? normalize(res) : res;
            res = float3(dot(float4(res, 1.0), inline_post_matrices[0]), dot(float4(res, 1.0), inline_post_matrices[1]), dot(float4(res, 1.0), inline_post_matrices[2]));
        }
        if (posted && res.z == 0.0) res.xy = clamp(res.xy * 0.5, -1.0, 1.0);
        o.uv[0] = float4(res, posted ? 1.0 : 0.0);
    }
    {  // coordinate 1: kind 0, projective 0, row 5
        const float4 c = float4(v.uv[0], 1.0, 1.0);
        float3 res;
        if (((vs_header.w >> 1u) & 1u) != 0u) {
            const uint tm = native_tex_matrix(v, 1u);
            res = float3(dot(c, native_matrix_row(tm + 0u)), dot(c, native_matrix_row(tm + 1u)), 1.0);
        } else {
            res = float3(dot(c, inline_tex_matrices[3]), dot(c, inline_tex_matrices[4]), 1.0);
        }
        const uint pt = native_post(1u);
        const bool posted = (pt & 0x3Fu) != 61u;
        if (posted) {
            if ((pt & 0x100u) != 0u) res = dot(res, res) > 0.0 ? normalize(res) : res;
            res = float3(dot(float4(res, 1.0), inline_post_matrices[3]), dot(float4(res, 1.0), inline_post_matrices[4]), dot(float4(res, 1.0), inline_post_matrices[5]));
        }
        if (posted && res.z == 0.0) res.xy = clamp(res.xy * 0.5, -1.0, 1.0);
        o.uv[1] = float4(res, posted ? 1.0 : 0.0);
    }
    j3d_vertex_override(v, o);
}
float4 material_color(NativeVaryings i) {
#if NATIVE_PIXEL_LIT
    j3d_pixel_begin(i);
    j3d_pixel_channels(i);
#endif
    [branch] if (material.ovr.x != 0.0) {
        const float4 t = tex[0].Sample(samp[0], j3d_texel(i.uv[0], 0u) / material.tex_scale[0].zw);
        const float4 ras = float4(0.0, 0.0, 0.0, 0.0);
        const float4 prev = material.reg[0];
        const float4 r0 = material.reg[1];
        const float4 r1 = material.reg[2];
        const float4 r2 = material.reg[3];
        const float ka = 64.0 / 255.0;
        return j3d_override(t, i.color, 64.0 / 255.0, saturate((r0.a + lerp(t.a, ka, ras.a)) * 2.0), i.pos);
    }
    float4 prev = material.reg[0];
    float4 r0 = material.reg[1];
    float4 r1 = material.reg[2];
    float4 r2 = material.reg[3];
    {  // stage 0
        const float4 t = tex[0].Sample(samp[0], j3d_texel(i.uv[0], 0u) / material.tex_scale[0].zw).rgba;
        const float4 ras = i.color.rgba;
        const float ka = 64.0 / 255.0;
        const float3 rc = saturate((r0.rgb + lerp(float3(0.0, 0.0, 0.0), t.rgb, ras.rgb)));
        const float ra = saturate((r0.a + lerp(t.a, ka, ras.a)) * 2.0);
        prev.rgb = rc;
        prev.a = ra;
    }
    {  // stage 1
        const float4 t = tex[1].Sample(samp[1], j3d_texel(i.uv[1], 1u) / material.tex_scale[1].zw).rgba;
        const float4 ras = i.color.rgba;
        const float3 rc = saturate((prev.rgb + lerp(float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0))));
        const float ra = saturate((ras.a - lerp(0.0, j3d_in(r0.a), t.a)));
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
