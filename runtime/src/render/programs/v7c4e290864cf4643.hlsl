#include "program.hlsli"
#define AMBIENT_TERM(x) (x)
cbuffer MaterialVertex : register(b1) {
    float4 m[11];
};
struct VertexIn {
    float3 pos : POSITION;
    uint draw_matrix : BLENDINDICES;
    float3 nrm : NORMAL;
    float2 uv0 : TEXCOORD0;
};
#define NEBULA_MOTION_CURRENT_INDEX 4
#define NEBULA_MOTION_PREVIOUS_INDEX 5
struct Varyings {
    float4 pos : SV_Position;
    PER_SAMPLE float4 color : COLOR0;
    PER_SAMPLE float2 uv0 : TEXCOORD0;
    PER_SAMPLE float2 uv1 : TEXCOORD1;
    PER_SAMPLE float2 uv2 : TEXCOORD2;
    PER_SAMPLE float3 uv3 : TEXCOORD3;
    VARYINGS_MOTION
};
Varyings program_vs(VertexIn v, uint instance : SV_InstanceID) {
    Varyings o = (Varyings)0;
    const Place p = native_place(v.pos, v.draw_matrix, instance);
    PLACE_OUT(o, p);
    const float3 nrm = lit_normal(p, v.nrm);
    float4 c0 = float4(0.0, 0.0, 0.0, 0.0);
    c0.rgb = m[0].rgb;
    c0.a = m[0].a * saturate((AMBIENT_TERM(m[1].a) * 255.0 + light_term(p.light, 2u, 3u, 2u, p.eye, nrm).a) / 255.0);
    o.color = c0;
    {
        const float4 c = float4(v.uv0, 1.0, 1.0);
        o.uv0 = float2(dot(c, m[2]), dot(c, m[3]));
    }
    {
        const float4 c = float4(v.uv0, 1.0, 1.0);
        o.uv1 = float2(dot(c, m[4]), dot(c, m[5]));
    }
    {
        const float4 c = float4(eye_normal_rigid(p, v.nrm), 1.0);
        o.uv2 = float2(dot(c, m[6]), dot(c, m[7]));
    }
    {
        const float4 c = float4(world_position(p, v.pos), 1.0);
        float3 res = float3(dot(c, m[8]), dot(c, m[9]), dot(c, m[10]));
        if (res.z == 0.0) res.xy = clamp(res.xy * 0.5, -1.0, 1.0);
        o.uv3 = res;
    }
    return o;
}
