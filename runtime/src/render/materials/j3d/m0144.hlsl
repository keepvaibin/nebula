// J3D material 144 (tools/dev/gen_j3d_materials.py from the game's material set; E-04)
// constants 0x30060088
struct MaterialConstants {
    float4 kcolor0;
    float4 mat_color0;
    float4 fog;
    float4 fog_color;
    float4 ovr;
    float4 ovr_coord;
};
#include "j3d_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color.rgb = material.mat_color0.rgb;
    o.color.a = material.mat_color0.a;
    o.color1 = v.color1;
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
    [branch] if (material.ovr.x != 0.0) {
        const float4 t = tex[0].Sample(samp[0], j3d_coord(i.uv[0]));
        const float4 ras = float4(0.0, 0.0, 0.0, 0.0);
        const float4 prev = float4(1.0, 1.0, 1.0, 1.0);
        const float ka = material.kcolor0.a;
        return j3d_override(t, i.color, material.kcolor0.a, saturate((0.0 + lerp(ras.a, 0.0, 0.0))), i.pos);
    }
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    {  // stage 0
        const float4 t = tex[0].Sample(samp[0], j3d_coord(i.uv[0])).rgba;
        const float4 ras = i.color.rgba;
        const float3 k = float4(material.kcolor0.rgb, 0.0).rgb;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(k.rgb, ras.rgb, t.rgb)) * 4.0);
        const float ra = saturate((0.0 + lerp(ras.a, 0.0, 0.0)));
        prev.rgb = rc;
        prev.a = ra;
    }
    prev = (float4(prev.rgb, prev.a));
    prev.rgb = j3d_fog(prev.rgb, i.pos);
    return prev;
}
