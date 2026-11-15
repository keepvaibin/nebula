#include "program.hlsli"
cbuffer MaterialPixel : register(b2) {
    float4 m[4];
};
#define NEBULA_MOTION_CURRENT_INDEX 2
#define NEBULA_MOTION_PREVIOUS_INDEX 3
struct Varyings {
    float4 pos : SV_Position;
    PER_SAMPLE float4 color : COLOR0;
    PER_SAMPLE float2 uv0 : TEXCOORD0;
    PER_SAMPLE float3 uv1 : TEXCOORD1;
    PER_SAMPLE float3 gain : TEXCOORD4;
    VARYINGS_MOTION
};
NativeOut program_ps(Varyings i) {
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    float2 tevcoord = float2(0.0, 0.0);
    const float3 ind0 = round(TEX(0).Sample(SAMP(0), i.uv0 / float2(1.0, 1.0)).abg * 255.0);
    {
        float2 texel = (i.uv1.z != 0.0 ? i.uv1.xy / i.uv1.z : i.uv1.xy) * m[0].xy;
        float3 sv = floor(ind0 / 1.0);
        sv.x += -128.0;
        sv.y += -128.0;
        sv.z += -128.0;
        const float2 off = float2(dot(m[1].xyz, sv), dot(m[2].xyz, sv));
        tevcoord = texel + off;
        const float4 t = TEX(1).Sample(SAMP(1), tevcoord / m[0].zw).rgba;
        const float4 ras = i.color.rgba;
        const float3 k = m[3].rgb;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(k.rgb, t.rgb, ras.rgb)));
        const float ra = saturate((0.0 + lerp(0.0, t.a, ras.a)));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(0, float4(rc, ra))
    }
    prev = float4(prev.rgb, prev.a);
#if NATIVE_TRACED
    prev.rgb = traced_map_shown(prev.rgb, i.gain);
#endif
    PROGRAM_RETURN(saturate(prev));
}
