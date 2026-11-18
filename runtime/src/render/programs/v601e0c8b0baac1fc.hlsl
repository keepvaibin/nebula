#include "program.hlsli"
#define AMBIENT_TERM(x) (x)
struct VertexIn {
    float3 pos : POSITION;
    uint draw_matrix : BLENDINDICES;
    float4 color0 : COLOR0;
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
    float4 c0 = float4(0.0, 0.0, 0.0, 0.0);
    c0.rgb = v.color0.rgb;
    c0.a = v.color0.a;
    o.color = c0;
    return o;
}
