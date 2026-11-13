#include "program.hlsli"
#define AMBIENT_TERM(x) (x)
cbuffer MaterialVertex : register(b1) {
    float4 m[10];
};
struct VertexIn {
    float3 pos : POSITION;
    uint draw_matrix : BLENDINDICES;
#if NATIVE_TRACED
    uint gain : BLENDWEIGHT;
#endif
};
#define NEBULA_MOTION_CURRENT_INDEX 3
#define NEBULA_MOTION_PREVIOUS_INDEX 4
struct Varyings {
    float4 pos : SV_Position;
    PER_SAMPLE float4 color : COLOR0;
    PER_SAMPLE float3 uv0 : TEXCOORD0;
    PER_SAMPLE float3 uv1 : TEXCOORD1;
    PER_SAMPLE float3 uv2 : TEXCOORD2;
    PER_SAMPLE float3 gain : TEXCOORD5;
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
        const float4 c = float4(world_position(p, v.pos), 1.0);
        float3 res = float3(dot(c, m[1]), dot(c, m[2]), dot(c, m[3]));
        if (res.z == 0.0) res.xy = clamp(res.xy * 0.5, -1.0, 1.0);
        o.uv0 = res;
    }
    {
        const float4 c = float4(world_position(p, v.pos), 1.0);
        float3 res = float3(dot(c, m[4]), dot(c, m[5]), dot(c, m[6]));
        if (res.z == 0.0) res.xy = clamp(res.xy * 0.5, -1.0, 1.0);
        o.uv1 = res;
    }
    {
        const float4 c = float4(world_position(p, v.pos), 1.0);
        float3 res = float3(dot(c, m[7]), dot(c, m[8]), dot(c, m[9]));
        if (res.z == 0.0) res.xy = clamp(res.xy * 0.5, -1.0, 1.0);
        o.uv2 = res;
    }
#if NATIVE_TRACED
    o.gain = traced_map_gain(v.gain);
#endif
    return o;
}
