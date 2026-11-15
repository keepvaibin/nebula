#include "program.hlsli"
#define AMBIENT_TERM(x) (x)
cbuffer MaterialVertex : register(b1) {
    float4 m[1];
};
struct VertexIn {
    float3 pos : POSITION;
    uint draw_matrix : BLENDINDICES;
    float4 color0 : COLOR0;
#if NATIVE_TRACED
    uint gain : BLENDWEIGHT;
#endif
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
#if NATIVE_TRACED
    v.color0.rgb = traced_vertex_gain(v.color0.rgb, v.gain);
#endif
    float4 c0 = float4(0.0, 0.0, 0.0, 0.0);
    c0.rgb = v.color0.rgb;
    c0.a = m[0].a;
    o.color = c0;
    return o;
}
