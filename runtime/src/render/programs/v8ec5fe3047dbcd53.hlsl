#include "program.hlsli"
#define AMBIENT_TERM(x) (x)
cbuffer MaterialVertex : register(b1) {
    float4 m[2];
};
struct VertexIn {
    float3 pos : POSITION;
    uint draw_matrix : BLENDINDICES;
    float3 nrm : NORMAL;
};
#define NEBULA_MOTION_CURRENT_INDEX 0
#define NEBULA_MOTION_PREVIOUS_INDEX 1
struct Varyings {
    float4 pos : SV_Position;
    PER_SAMPLE float4 color : COLOR0;
    VARYINGS_MOTION
};
Varyings program_vs(VertexIn v, uint instance : SV_InstanceID) {
    Varyings o = (Varyings)0;
    const Place p = native_place(v.pos, v.draw_matrix, instance);
    PLACE_OUT(o, p);
    const float3 nrm = lit_normal(p, v.nrm);
    float4 c0 = float4(0.0, 0.0, 0.0, 0.0);
    c0.rgb = m[0].rgb * TRACED_LEVEL(saturate((AMBIENT_TERM(m[1].rgb) * 255.0 + light_term(p.light, 0u, 3u, 2u, p.eye, nrm).rgb) / 255.0));
    c0.a = m[0].a * saturate((AMBIENT_TERM(m[1].a) * 255.0 + light_term(p.light, 2u, 3u, 2u, p.eye, nrm).a) / 255.0);
    o.color = c0;
    return o;
}
