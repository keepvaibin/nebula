#include "program.hlsli"
#define AMBIENT_TERM(x) (x)
struct VertexIn {
    float3 pos : POSITION;
    uint draw_matrix : BLENDINDICES;
    float3 nrm : NORMAL;
    float4 color0 : COLOR0;
#if NATIVE_TRACED
    uint gain : BLENDWEIGHT;
#endif
};
#define NEBULA_MOTION_CURRENT_INDEX 3
#define NEBULA_MOTION_PREVIOUS_INDEX 4
struct Varyings {
    PER_SAMPLE float3 lit_eye : TEXCOORD0;
    PER_SAMPLE float3 lit_nrm : TEXCOORD1;
    nointerpolation uint lit_light : TEXCOORD2;
    float4 pos : SV_Position;
    PER_SAMPLE float4 color : COLOR0;
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
    return o;
}
