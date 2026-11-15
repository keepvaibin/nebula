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
    PER_SAMPLE float3 uv2 : TEXCOORD2;
    VARYINGS_MOTION
};
NativeOut program_ps(Varyings i) {
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    float4 r0 = m[0];
    float4 r1 = m[1];
    {
        const float4 t = TEX(0).Sample(SAMP(0), i.uv0).rgba;
        const float4 ras = i.color.rgba;
        const float ka = 32.0 / 255.0;
        const float3 rc = saturate((r0.rgb + lerp(r1.rgb, t.rgb, ras.rgb)));
        const float ra = saturate((r0.a + lerp(t.a, ka, ras.a)) * 2.0);
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(0, float4(rc, ra))
    }
    {
        const float4 ras = i.color.rgba;
        const float ka = 64.0 / 255.0;
        const float3 rc = saturate((prev.rgb + lerp(float3(0.0, 0.0, 0.0), ras.rgb, prev.aaa)) * 2.0);
        const float ra = saturate((r0.a + lerp(0.0, ka, ras.a)) * 2.0);
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(1, float4(rc, ra))
    }
    {
        const float4 t = TEX(1).Sample(SAMP(1), i.uv1).rgba;
        const float4 ras = i.color.rgba;
        const float3 rc = saturate((prev.rgb + lerp(float3(0.0, 0.0, 0.0), t.rgb, ras.rgb)));
        const float ra = (0.0 + lerp(prev.a, 0.0, 0.0));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(2, float4(rc, ra))
    }
    {
        const float4 t = TEX(2).Sample(SAMP(2), (i.uv2.z != 0.0 ? i.uv2.xy / i.uv2.z : i.uv2.xy)).rgba;
        const float3 k = float3(32.0 / 255.0, 32.0 / 255.0, 32.0 / 255.0);
        const float ka = 1.0;
        const float3 rc = saturate((prev.rgb - lerp(k.rgb, prev.rgb, t.rgb)));
        const float ra = (0.0 + lerp(0.0, ka, ka)) * 4.0;
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(3, float4(rc, ra))
    }
    prev = low8(float4(prev.rgb, prev.a));
    PROGRAM_RETURN(saturate(prev));
}
