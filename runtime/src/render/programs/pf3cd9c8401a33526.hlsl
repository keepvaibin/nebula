#include "program.hlsli"
cbuffer MaterialPixel : register(b2) {
    float4 m[3];
};
#define NEBULA_MOTION_CURRENT_INDEX 1
#define NEBULA_MOTION_PREVIOUS_INDEX 2
struct Varyings {
    float4 pos : SV_Position;
    PER_SAMPLE float4 color : COLOR0;
    PER_SAMPLE float2 uv0 : TEXCOORD0;
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
    float4 r1 = m[1];
    float4 r2 = m[2];
    {
        const float4 t = TAP(tap0, TEX(0).Sample(SAMP(0), i.uv0)).rgba;
        const float4 ras = i.color.rgba;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(low8(r1.rgb), low8(r0.rgb), ras.rgb)) * 2.0);
        const float ra = saturate((0.0 + lerp(0.0, t.a, ras.a)));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(0, float4(rc, ra))
    }
    {
        const float4 t = TAP(tap0, TEX(0).Sample(SAMP(0), i.uv0)).rgba;
        const float4 ras = i.color.rgba;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(float3(0.0, 0.0, 0.0), prev.rgb, t.rgb)) * 4.0);
        const float ra = saturate((r0.a + lerp(t.a, low8(r0.a), ras.a)));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(1, float4(rc, ra))
    }
    {
        const float4 t = TAP(tap0, TEX(0).Sample(SAMP(0), i.uv0)).rgba;
        const float3 rc = saturate((prev.rgb + lerp(low8(r0.rgb), prev.aaa, r2.rgb)) * 0.5);
        const float ra = saturate((prev.a + lerp(t.a, 0.0, 0.0) - 128.0 / 255.0) * 0.5);
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(2, float4(rc, ra))
    }
    prev = float4(prev.rgb, prev.a);
    PROGRAM_RETURN(saturate(prev));
}
