#include "program.hlsli"
struct VertexIn {
    float3 pos : POSITION;
    uint draw_matrix : BLENDINDICES;
};
struct Varyings {
    float4 pos : SV_Position;
    VARYINGS_MOTION
};
Varyings program_vs(VertexIn v, uint instance : SV_InstanceID) {
    Varyings o = (Varyings)0;
    const Place p = native_place(v.pos, v.draw_matrix, instance);
    PLACE_OUT(o, p);
    return o;
}
