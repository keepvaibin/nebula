#include "program.hlsli"
cbuffer MaterialPixel : register(b2) {
    float4 m[1];
};
#define NEBULA_MOTION_CURRENT_INDEX 1
#define NEBULA_MOTION_PREVIOUS_INDEX 2
struct Varyings {
    float4 pos : SV_Position;
    PER_SAMPLE float4 color : COLOR0;
    PER_SAMPLE float2 uv0 : TEXCOORD0;
    VARYINGS_MOTION
};
NativeOut program_ps(Varyings i) {
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    {
        const float4 t = ENV_FETCH(TEX(0).Sample(SAMP(0), i.uv0).rgba);
        const float4 ras = i.color.rgba;
        const float3 k = m[0].rgb;
        const float3 rc = saturate((ras.rgb + lerp(float3(0.0, 0.0, 0.0), t.rgb, k.rgb)));
        const float ra = saturate((0.0 + lerp(0.0, t.a, ras.a)));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(0, float4(rc, ra))
    }
    prev = float4(prev.rgb, prev.a);
    PROGRAM_RETURN(saturate(prev));
}
