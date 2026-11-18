#include "program.hlsli"
#define AMBIENT_TERM(x) (x)
cbuffer MaterialVertex : register(b1) {
    float4 m[10];
};
struct VertexIn {
    float3 pos : POSITION;
    uint draw_matrix : BLENDINDICES;
    float3 nrm : NORMAL;
    float4 color0 : COLOR0;
    float2 uv0 : TEXCOORD0;
};
#define NEBULA_MOTION_CURRENT_INDEX 8
#define NEBULA_MOTION_PREVIOUS_INDEX 9
struct Varyings {
    PER_SAMPLE float3 lit_eye : TEXCOORD5;
    PER_SAMPLE float3 lit_nrm : TEXCOORD6;
    nointerpolation uint lit_light : TEXCOORD7;
    float4 pos : SV_Position;
    PER_SAMPLE float4 color : COLOR0;
    PER_SAMPLE float2 uv0 : TEXCOORD0;
    PER_SAMPLE float2 uv1 : TEXCOORD1;
    PER_SAMPLE float2 uv2 : TEXCOORD2;
    PER_SAMPLE float2 uv3 : TEXCOORD3;
    PER_SAMPLE float2 uv4 : TEXCOORD4;
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
    o.color = v.color0;
    {
        const float4 c = float4(v.uv0, 1.0, 1.0);
        o.uv0 = float2(dot(c, m[0]), dot(c, m[1]));
    }
    {
        const float4 c = float4(v.uv0, 1.0, 1.0);
        o.uv1 = float2(dot(c, m[2]), dot(c, m[3]));
    }
    {
        const float4 c = float4(eye_normal_rigid(p, v.nrm), 1.0);
        o.uv2 = float2(dot(c, m[4]), dot(c, m[5]));
    }
    {
        const float4 c = float4(v.uv0, 1.0, 1.0);
        o.uv3 = float2(dot(c, m[6]), dot(c, m[7]));
    }
    {
        const float4 c = float4(eye_normal_rigid(p, v.nrm), 1.0);
        o.uv4 = float2(dot(c, m[8]), dot(c, m[9]));
    }
    return o;
}
