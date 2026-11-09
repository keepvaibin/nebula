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
    PER_SAMPLE float2 uv1 : TEXCOORD1;
    VARYINGS_MOTION
};
NativeOut program_ps(Varyings i) {
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    float4 r0 = m[0];
    float4 r1 = m[1];
    {
        const float4 t = TEX(0).Sample(SAMP(0), i.uv0).rgba;
        const float3 k = m[2].rgb;
        const float ka = m[2].a;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(float3(0.0, 0.0, 0.0), k.rgb, t.rgb)));
        const float ra = saturate((0.0 + lerp(0.0, ka, t.a)));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(0, float4(rc, ra))
    }
    {
        const float4 t = TEX(1).Sample(SAMP(1), i.uv1).rgba;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(float3(0.0, 0.0, 0.0), prev.rgb, t.rgb)) * 2.0);
        const float ra = (0.0 + lerp(0.0, prev.a, t.a)) * 2.0;
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(1, float4(rc, ra))
    }
    {
        const float4 ras = i.color.rgba;
        const float3 rc = prev.rgb + ((round((prev.rgb).r * 255.0)) > (round((r0.aaa).r * 255.0)) ? r0.rgb : float3(0.0, 0.0, 0.0));
        const float ra = (0.0 + lerp(ras.a, 0.0, 0.0));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(2, float4(rc, ra))
    }
    {
        const float4 ras = i.color.rgba;
        const float3 k = m[3].rgb;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(low8(r1.rgb), k.rgb, low8(prev.rgb))));
        const float ra = (0.0 + lerp(ras.a, 0.0, 0.0));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(3, float4(rc, ra))
    }
    prev = float4(prev.rgb, prev.a);
    PROGRAM_RETURN(saturate(prev));
}
