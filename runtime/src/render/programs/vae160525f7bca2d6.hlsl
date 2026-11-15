#include "program.hlsli"
#define AMBIENT_TERM(x) (x)
cbuffer MaterialVertex : register(b1) {
    float4 m[5];
};
struct VertexIn {
    float3 pos : POSITION;
    uint draw_matrix : BLENDINDICES;
    float3 nrm : NORMAL;
#if NATIVE_TRACED
    uint gain : BLENDWEIGHT;
#endif
};
#define NEBULA_MOTION_CURRENT_INDEX 2
#define NEBULA_MOTION_PREVIOUS_INDEX 3
struct Varyings {
    float4 pos : SV_Position;
    PER_SAMPLE float4 color : COLOR0;
    PER_SAMPLE float2 uv0 : TEXCOORD0;
    PER_SAMPLE float2 uv1 : TEXCOORD1;
    PER_SAMPLE float3 gain : TEXCOORD4;
    VARYINGS_MOTION
};
Varyings program_vs(VertexIn v, uint instance : SV_InstanceID) {
    Varyings o = (Varyings)0;
    const Place p = native_place(v.pos, v.draw_matrix, instance);
    PLACE_OUT(o, p);
    float4 c0 = float4(0.0, 0.0, 0.0, 0.0);
    c0.rgb = m[0].rgb;
    c0.a = m[0].a;
    o.color = c0;
    {
        const float4 c = float4(eye_normal_rigid(p, v.nrm), 1.0);
        o.uv0 = float2(dot(c, m[1]), dot(c, m[2]));
    }
    {
        const float4 c = float4(eye_normal_rigid(p, v.nrm), 1.0);
        o.uv1 = float2(dot(c, m[3]), dot(c, m[4]));
    }
#if NATIVE_TRACED
    o.gain = traced_map_gain(v.gain);
#endif
    return o;
}
