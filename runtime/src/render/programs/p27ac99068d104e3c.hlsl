#include "program.hlsli"
cbuffer MaterialPixel : register(b2) {
    float4 m[2];
};
#define NEBULA_MOTION_CURRENT_INDEX 3
#define NEBULA_MOTION_PREVIOUS_INDEX 4
struct Varyings {
    float4 pos : SV_Position;
    PER_SAMPLE float4 color : COLOR0;
    PER_SAMPLE float2 uv0 : TEXCOORD0;
    PER_SAMPLE float2 uv1 : TEXCOORD1;
    PER_SAMPLE float2 uv2 : TEXCOORD2;
    VARYINGS_MOTION
};
NativeOut program_ps(Varyings i) {
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    float4 r0 = m[0];
    {
        const float4 t = ENV_FETCH(TEX(0).Sample(SAMP(0), i.uv0).rgba);
        const float4 ras = i.color.rgba;
        const float3 k = float3(64.0 / 255.0, 64.0 / 255.0, 64.0 / 255.0);
        const float3 rc = saturate((r0.rgb + lerp(float3(0.0, 0.0, 0.0), t.rgb, k.rgb)));
        const float ra = (0.0 + lerp(0.0, t.a, ras.a));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(0, float4(rc, ra))
    }
    {
        const float4 t = ENV_FETCH(TEX(1).Sample(SAMP(1), i.uv1).rgba);
        const float3 k = float3(1.0, 1.0, 1.0);
        const float3 rc = saturate((prev.rgb + lerp(float3(0.0, 0.0, 0.0), k.rgb, t.rgb)));
        const float ra = saturate((prev.a + lerp(0.0, 0.0, 0.0)));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(1, float4(rc, ra))
    }
    {
        const float4 t = TEX(2).Sample(SAMP(2), i.uv2).rgba;
        const float4 ras = i.color.rgba;
        const float3 k = m[1].aaa;
        const float3 rc = saturate((prev.rgb - lerp(float3(0.0, 0.0, 0.0), k.rgb, t.rgb)));
        const float ra = (0.0 + lerp(ras.a, 0.0, 0.0));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(2, float4(rc, ra))
    }
    prev = float4(prev.rgb, prev.a);
    PROGRAM_RETURN(saturate(prev));
}
