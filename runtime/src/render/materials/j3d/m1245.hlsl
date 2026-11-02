// J3D material 1245 (tools/dev/gen_j3d_materials.py from the game's material set; E-04)
#include "j3d_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    const uint nq = v.pn_row & 31u;
    const float3 nt = float3(dot(native_normal_row(nq).xyz, v.nrm), dot(native_normal_row(nq + 1u).xyz, v.nrm), dot(native_normal_row(nq + 2u).xyz, v.nrm));
    const float3 nrm = dot(nt, nt) > 0.0 ? normalize(nt) : nt;
    o.color.rgb = v.color0.rgb;
    o.color.a = v.color0.a * saturate((material.amb_color[0].a * 255.0 + j3d_light(2u, 3u, 2u, v.eye, nrm).a) / 255.0);
    o.color1 = v.color1;
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
    j3d_vertex_override(v, o);
}
float4 material_color(NativeVaryings i) {
    [branch] if (material.ovr.x != 0.0) {
        const float4 t = tex[0].Sample(samp[0], j3d_texel(i.uv[0], 0u) / material.tex_scale[0].zw);
        const float4 ras = float4(0.0, 0.0, 0.0, 0.0);
        const float4 prev = material.reg[0];
        const float4 r0 = material.reg[1];
        const float4 r1 = material.reg[2];
        const float4 r2 = material.reg[3];
        const float ka = material.kcolor[0].a;
        return j3d_override(t, i.color, material.kcolor[0].a, saturate((0.0 + lerp(ka, 0.0, ras.a)) * 2.0), i.pos);
    }
    float4 prev = material.reg[0];
    float4 r0 = material.reg[1];
    float4 r1 = material.reg[2];
    float4 r2 = material.reg[3];
    {  // stage 0
        const float4 t = float4(0.0, 0.0, 0.0, 0.0);
        const float4 ras = i.color.rrra;
        const float3 k = float4(material.kcolor[0].rgb, 0.0).rgb;
        const float ka = material.kcolor[0].a;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(k.rgb, j3d_in(r0.rgb), ras.rgb)));
        const float ra = saturate((0.0 + lerp(ka, 0.0, ras.a)) * 2.0);
        prev.rgb = rc;
        prev.a = ra;
    }
    {  // stage 1
        const float4 t = tex[0].Sample(samp[0], j3d_texel(i.uv[0], 0u) / material.tex_scale[0].zw).rrra;
        const float4 ras = i.color.rrra;
        const float3 rc = saturate((prev.aaa + lerp(float3(0.0, 0.0, 0.0), j3d_in(prev.rgb), t.rgb)));
        const float ra = saturate((0.0 + lerp(0.0, j3d_in(r0.a), t.a)));
        prev.rgb = rc;
        prev.a = ra;
    }
    {  // stage 2
        const float4 t = float4(0.0, 0.0, 0.0, 0.0);
        const float4 ras = i.color.bbba;
        const float3 rc = saturate((ras.rgb + lerp(j3d_in(prev.rgb), float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0))));
        const float ra = (0.0 + lerp(j3d_in(r0.a), 0.0, 0.0));
        prev.rgb = rc;
        prev.a = ra;
    }
    prev = j3d_out(float4(prev.rgb, prev.a));
    prev.rgb = j3d_fog(prev.rgb, i.pos);
    return prev;
}
