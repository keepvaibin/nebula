#include "program.hlsli"
cbuffer MaterialPixel : register(b2) {
    float4 m[2];
};
#define NEBULA_MOTION_CURRENT_INDEX 2
#define NEBULA_MOTION_PREVIOUS_INDEX 3
struct Varyings {
    float4 pos : SV_Position;
    PER_SAMPLE float2 uv0 : TEXCOORD0;
    PER_SAMPLE float2 uv1 : TEXCOORD1;
    VARYINGS_MOTION
};
NativeOut program_ps(Varyings i) {
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    float4 r0 = m[0];
    float4 r1 = m[1];
    {
        const float4 t = TEX(0).Sample(SAMP(0), i.uv0).rgba;
        const float ka = 64.0 / 255.0;
        const float3 rc = saturate((r0.rgb + lerp(r1.rgb, t.rgb, t.rgb)));
        const float ra = saturate((r0.a + lerp(t.a, ka, 0.0)) * 2.0);
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(0, float4(rc, ra))
    }
    {
        const float ka = 64.0 / 255.0;
        const float3 rc = saturate((prev.rgb + lerp(float3(0.0, 0.0, 0.0), float3(1.0, 1.0, 1.0), prev.aaa)) * 2.0);
        const float ra = saturate((r0.a + lerp(0.0, ka, 0.0)) * 2.0);
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(1, float4(rc, ra))
    }
    {
        const float4 t = TEX(1).Sample(SAMP(1), i.uv1).rgba;
        const float3 rc = saturate((prev.rgb + lerp(float3(0.0, 0.0, 0.0), t.rgb, t.rgb)));
        const float ra = (0.0 + lerp(prev.a, 0.0, 0.0));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(2, float4(rc, ra))
    }
    prev = float4(prev.rgb, prev.a);
    PROGRAM_RETURN(saturate(prev));
}
