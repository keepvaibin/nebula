#include "program.hlsli"
#define AMBIENT_TERM(x) (x)
cbuffer MaterialVertex : register(b1) {
    float4 m[2];
};
struct VertexIn {
    float3 pos : POSITION;
    uint draw_matrix : BLENDINDICES;
#if NATIVE_TRACED
    uint gain : BLENDWEIGHT;
#endif
    float2 uv0 : TEXCOORD0;
};
#define NEBULA_MOTION_CURRENT_INDEX 1
#define NEBULA_MOTION_PREVIOUS_INDEX 2
struct Varyings {
    float4 pos : SV_Position;
    PER_SAMPLE float2 uv0 : TEXCOORD0;
    PER_SAMPLE float3 gain : TEXCOORD3;
    VARYINGS_MOTION
};
Varyings program_vs(VertexIn v, uint instance : SV_InstanceID) {
    Varyings o = (Varyings)0;
    const Place p = native_place(v.pos, v.draw_matrix, instance);
    PLACE_OUT(o, p);
    {
        const float4 c = float4(v.uv0, 1.0, 1.0);
        o.uv0 = float2(dot(c, m[0]), dot(c, m[1]));
    }
#if NATIVE_TRACED
    o.gain = traced_map_gain(v.gain);
#endif
    return o;
}
