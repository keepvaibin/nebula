#include "program.hlsli"
#define AMBIENT_TERM(x) (x)
cbuffer MaterialVertex : register(b1) {
    float4 m[7];
};
struct VertexIn {
    float3 pos : POSITION;
    uint draw_matrix : BLENDINDICES;
    float3 nrm : NORMAL;
    float4 color0 : COLOR0;
    float2 uv0 : TEXCOORD0;
};
#define NEBULA_MOTION_CURRENT_INDEX 3
#define NEBULA_MOTION_PREVIOUS_INDEX 4
struct Varyings {
    float4 pos : SV_Position;
    PER_SAMPLE float4 color : COLOR0;
    PER_SAMPLE float2 uv0 : TEXCOORD0;
    PER_SAMPLE float2 uv1 : TEXCOORD1;
    PER_SAMPLE float2 uv2 : TEXCOORD2;
    VARYINGS_MOTION
};
Varyings program_vs(VertexIn v, uint instance : SV_InstanceID) {
    Varyings o = (Varyings)0;
    const Place p = native_place(v.pos, v.draw_matrix, instance);
    PLACE_OUT(o, p);
    const float3 nrm = lit_normal(p, v.nrm);
    float4 c0 = float4(0.0, 0.0, 0.0, 0.0);
    c0.rgb = v.color0.rgb * TRACED_LEVEL(saturate((AMBIENT_TERM(m[0].rgb) * 255.0 + light_term(p.light, 0u, 3u, 2u, p.eye, nrm).rgb + light_term(p.light, 1u, 3u, 2u, p.eye, nrm).rgb + light_term(p.light, 4u, 3u, 2u, p.eye, nrm).rgb) / 255.0));
    c0.a = v.color0.a;
    o.color = c0;
    {
        const float4 c = float4(v.uv0, 1.0, 1.0);
        o.uv0 = float2(dot(c, m[1]), dot(c, m[2]));
    }
    {
        const float4 c = float4(v.uv0, 1.0, 1.0);
        o.uv1 = float2(dot(c, m[3]), dot(c, m[4]));
    }
    {
        const float4 c = float4(eye_normal_skinned(p, v.nrm), 1.0);
        o.uv2 = float2(dot(c, m[5]), dot(c, m[6]));
    }
    return o;
}
