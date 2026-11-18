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
#define NEBULA_MOTION_CURRENT_INDEX 6
#define NEBULA_MOTION_PREVIOUS_INDEX 7
struct Varyings {
    PER_SAMPLE float3 lit_eye : TEXCOORD3;
    PER_SAMPLE float3 lit_nrm : TEXCOORD4;
    nointerpolation uint lit_light : TEXCOORD5;
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
    o.lit_eye = p.eye;
    o.lit_nrm = nrm;
    o.lit_light = p.light;
    float4 c0 = float4(0.0, 0.0, 0.0, 0.0);
    c0.rgb = v.color0.rgb;
    c0.a = m[0].a;
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
        const float4 c = float4(eye_normal_rigid(p, v.nrm), 1.0);
        o.uv2 = float2(dot(c, m[5]), dot(c, m[6]));
    }
    return o;
}
