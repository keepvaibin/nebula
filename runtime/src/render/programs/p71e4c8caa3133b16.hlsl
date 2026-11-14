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
    {
        const float4 t = TAP(tap0, TEX(0).Sample(SAMP(0), i.uv0)).rgba;
        const float4 ras = i.color.rgba;
        const float3 rc = saturate((r0.rgb + lerp(float3(0.0, 0.0, 0.0), low8(r0.rgb), ras.rgb)));
        const float ra = saturate((r0.a + lerp(t.a, low8(r1.a), ras.a)) * 4.0);
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(0, float4(rc, ra))
    }
    {
        const float4 ras = i.color.rgba;
        const float ka = 1.0;
        const float3 rc = saturate((prev.rgb + lerp(float3(0.0, 0.0, 0.0), ras.rgb, prev.aaa)));
        const float ra = saturate((0.0 + lerp(0.0, ka, ka)));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(1, float4(rc, ra))
    }
    {
        const float4 t = TAP(tap0, TEX(0).Sample(SAMP(0), i.uv0)).rgba;
        const float4 ras = i.color.rgba;
        const float ka = 64.0 / 255.0;
        const float3 rc = saturate((prev.rgb + lerp(r1.rgb, t.rgb, ras.rgb)) * 2.0);
        const float ra = (prev.a + lerp(0.0, t.a, ka) + 128.0 / 255.0);
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(2, float4(rc, ra))
    }
    prev = low8(float4(prev.rgb, prev.a));
    const float a8 = round(prev.a * 255.0);
    if (!((a8 >= round(m[2].x * 255.0)) && (a8 <= round(m[2].y * 255.0)))) {
        discard;
    }
    PROGRAM_RETURN(saturate(prev));
}
