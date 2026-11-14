#include "program.hlsli"
cbuffer MaterialPixel : register(b2) {
    float4 m[3];
};
#define NEBULA_MOTION_CURRENT_INDEX 2
#define NEBULA_MOTION_PREVIOUS_INDEX 3
struct Varyings {
    float4 pos : SV_Position;
    PER_SAMPLE float4 color : COLOR0;
    PER_SAMPLE float2 uv0 : TEXCOORD0;
    PER_SAMPLE float2 uv1 : TEXCOORD1;
    VARYINGS_MOTION
};
#if NATIVE_TRACED || NATIVE_PREPASS
#define TAP(name, sample) (sample)
#else
#define TAP(name, sample) name
#endif
NativeOut program_ps(Varyings i) {
#if !(NATIVE_TRACED || NATIVE_PREPASS)
    const float4 tap0 = TEX(0).Sample(SAMP(0), i.uv0);
#endif
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    float4 r0 = m[0];
    float4 r2 = m[1];
    {
        const float4 t = TAP(tap0, TEX(0).Sample(SAMP(0), i.uv0)).rgba;
        const float4 ras = i.color.rgba;
        const float3 k = m[2].rgb;
        const float ka = 64.0 / 255.0;
        const float3 rc = saturate((r2.rgb + lerp(k.rgb, t.rgb, ras.rgb)) * 2.0);
        const float ra = saturate((r0.a + lerp(t.a, ka, ras.a)) * 2.0);
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(0, float4(rc, ra))
    }
    {
        const float4 t = TAP(tap0, TEX(0).Sample(SAMP(0), i.uv0)).rgba;
        const float4 ras = i.color.rgba;
        const float ka = 64.0 / 255.0;
        const float3 rc = saturate((prev.rgb + lerp(r0.rgb, ras.rgb, prev.aaa)));
        const float ra = saturate((r0.a + lerp(t.a, ka, ras.a)) * 2.0);
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(1, float4(rc, ra))
    }
    {
        const float4 t = ENV_FETCH(TEX(1).Sample(SAMP(1), i.uv1).rgba);
        const float3 rc = saturate((t.rgb + lerp(float3(0.0, 0.0, 0.0), float3(1.0, 1.0, 1.0), prev.rgb)));
        const float ra = (0.0 + lerp(prev.a, 0.0, 0.0));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(2, float4(rc, ra))
    }
    prev = float4(prev.rgb, prev.a);
    PROGRAM_RETURN(saturate(prev));
}
