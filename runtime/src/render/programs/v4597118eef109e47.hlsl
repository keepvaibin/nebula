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
    float4 color1 : COLOR1;
#if NATIVE_TRACED
    uint gain : BLENDWEIGHT;
#endif
    float2 uv0 : TEXCOORD0;
};
#define NEBULA_MOTION_CURRENT_INDEX 4
#define NEBULA_MOTION_PREVIOUS_INDEX 5
struct Varyings {
    PER_SAMPLE float3 lit_eye : TEXCOORD1;
    PER_SAMPLE float3 lit_nrm : TEXCOORD2;
    nointerpolation uint lit_light : TEXCOORD3;
    float4 pos : SV_Position;
    PER_SAMPLE float4 color : COLOR0;
    PER_SAMPLE float4 color1 : COLOR1;
    PER_SAMPLE float2 uv0 : TEXCOORD0;
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
    o.color1 = v.color1;
    {
        const float4 c = float4(v.uv0, 1.0, 1.0);
        o.uv0 = float2(dot(c, m[0]), dot(c, m[1]));
    }
    return o;
}
