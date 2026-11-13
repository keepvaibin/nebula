#include "program.hlsli"
#define AMBIENT_TERM(x) (x)
cbuffer MaterialVertex : register(b1) {
    float4 m[4];
};
struct VertexIn {
    float3 pos : POSITION;
    uint draw_matrix : BLENDINDICES;
    float3 nrm : NORMAL;
    float4 color0 : COLOR0;
#if NATIVE_TRACED
    uint gain : BLENDWEIGHT;
#endif
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
#if NATIVE_TRACED
    v.color0.rgb = traced_vertex_gain(v.color0.rgb, v.gain);
#endif
    o.lit_eye = p.eye;
    o.lit_nrm = nrm;
    o.lit_light = p.light;
    o.color = v.color0;
    {
        const float4 c = float4(eye_normal_rigid(p, v.nrm), 1.0);
        o.uv0 = float2(dot(c, m[0]), dot(c, m[1]));
    }
    {
        const float3 n = dot(v.nrm, v.nrm) > 0.0 ? normalize(v.nrm) : v.nrm;
        o.uv1 = n.xy;
    }
    {
        const float4 c = float4(eye_normal_rigid(p, v.nrm), 1.0);
        o.uv2 = float2(dot(c, m[2]), dot(c, m[3]));
    }
    return o;
}
