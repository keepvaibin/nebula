#include "program.hlsli"
#define AMBIENT_TERM(x) (x)
cbuffer MaterialVertex : register(b1) {
    float4 m[2];
};
struct VertexIn {
    float3 pos : POSITION;
    uint draw_matrix : BLENDINDICES;
    float3 nrm : NORMAL;
    float4 color0 : COLOR0;
};
#define NEBULA_MOTION_CURRENT_INDEX 1
#define NEBULA_MOTION_PREVIOUS_INDEX 2
struct Varyings {
    float4 pos : SV_Position;
    PER_SAMPLE float4 color : COLOR0;
    PER_SAMPLE float2 uv0 : TEXCOORD0;
    VARYINGS_MOTION
};
Varyings program_vs(VertexIn v, uint instance : SV_InstanceID) {
    Varyings o = (Varyings)0;
    const Place p = native_place(v.pos, v.draw_matrix, instance);
    PLACE_OUT(o, p);
    const float3 nrm = lit_normal(p, v.nrm);
    float4 c0 = float4(0.0, 0.0, 0.0, 0.0);
    c0.rgb = v.color0.rgb * TRACED_LEVEL(saturate((AMBIENT_TERM(m[0].rgb) * 255.0 + light_term(p.light, 0u, 3u, 2u, p.eye, nrm).rgb + light_term(p.light, 1u, 3u, 2u, p.eye, nrm).rgb) / 255.0));
    c0.a = m[1].a * saturate((AMBIENT_TERM(m[0].a) * 255.0 + light_term(p.light, 2u, 3u, 1u, p.eye, nrm).a) / 255.0);
    o.color = c0;
    {
        const float3 n = dot(v.nrm, v.nrm) > 0.0 ? normalize(v.nrm) : v.nrm;
        o.uv0 = n.xy;
    }
    return o;
}
