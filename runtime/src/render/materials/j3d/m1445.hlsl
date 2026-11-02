// J3D material 1445 (tools/dev/gen_j3d_materials.py from the game's material set; E-04)
#include "j3d_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    const uint nq = v.pn_row & 31u;
    const float3 nt = float3(dot(native_normal_row(nq).xyz, v.nrm), dot(native_normal_row(nq + 1u).xyz, v.nrm), dot(native_normal_row(nq + 2u).xyz, v.nrm));
    const float3 nrm = dot(nt, nt) > 0.0 ? normalize(nt) : nt;
    o.color.rgb = v.color0.rgb * saturate((material.amb_color[0].rgb * 255.0 + j3d_light(0u, 3u, 2u, v.eye, nrm).rgb + j3d_light(1u, 3u, 2u, v.eye, nrm).rgb) / 255.0);
    o.color.a = material.mat_color[0].a * saturate((material.amb_color[0].a * 255.0 + j3d_light(2u, 3u, 1u, v.eye, nrm).a) / 255.0);
    o.color1 = v.color1;
    j3d_vertex_override(v, o);
}
float4 material_color(NativeVaryings i) {
    [branch] if (material.ovr.x != 0.0) {
        const float4 t = float4(0.0, 0.0, 0.0, 0.0);
        const float4 ras = float4(0.0, 0.0, 0.0, 0.0);
        const float4 prev = material.reg[0];
        const float4 r0 = material.reg[1];
        const float4 r1 = material.reg[2];
        const float4 r2 = material.reg[3];
        const float ka = 64.0 / 255.0;
        return j3d_override(t, i.color, 64.0 / 255.0, saturate((r0.a + lerp(ras.a, ka, ras.a)) * 2.0), i.pos);
    }
    float4 prev = material.reg[0];
    float4 r0 = material.reg[1];
    float4 r1 = material.reg[2];
    float4 r2 = material.reg[3];
    {  // stage 0
        const float4 t = float4(0.0, 0.0, 0.0, 0.0);
        const float4 ras = i.color.rgba;
        const float ka = 64.0 / 255.0;
        const float3 rc = saturate((r0.rgb + lerp(j3d_in(r1.rgb), ras.rgb, ras.rgb)));
        const float ra = saturate((r0.a + lerp(ras.a, ka, ras.a)) * 2.0);
        prev.rgb = rc;
        prev.a = ra;
    }
    {  // stage 1
        const float4 t = float4(0.0, 0.0, 0.0, 0.0);
        const float4 ras = i.color.rgba;
        const float3 rc = saturate((prev.rgb + lerp(float3(0.0, 0.0, 0.0), ras.rgb, j3d_in(prev.aaa))) * 2.0);
        const float ra = saturate((0.0 + lerp(0.0, j3d_in(r1.a), j3d_in(r1.a))));
        prev.rgb = rc;
        prev.a = ra;
    }
    prev = j3d_out(float4(prev.rgb, prev.a));
    prev.rgb = j3d_fog(prev.rgb, i.pos);
    return prev;
}
