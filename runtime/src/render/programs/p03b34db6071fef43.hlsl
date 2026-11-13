#include "program.hlsli"
cbuffer MaterialPixel : register(b2) {
    float4 m[2];
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
    float4 r0 = m[0];
    {
        const float4 t = ENV_FETCH(TEX(0).Sample(SAMP(0), i.uv0).rgba);
        const float4 ras = i.color.rgba;
        const float ka = m[1].a;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(float3(0.0, 0.0, 0.0), ras.rgb, t.rgb)) * 2.0);
        const float ra = saturate((0.0 + lerp(ka, 0.0, ras.a)) * 2.0);
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(0, float4(rc, ra))
    }
    {
        const float3 rc = saturate((prev.aaa + lerp(float3(0.0, 0.0, 0.0), low8(r0.rgb), prev.rgb)) * 2.0);
        const float ra = saturate((0.0 + lerp(0.0, 0.0, 0.0)));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(1, float4(rc, ra))
    }
    prev = float4(prev.rgb, prev.a);
    PROGRAM_RETURN(saturate(prev));
}
