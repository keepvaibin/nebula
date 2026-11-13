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
NativeOut program_ps(Varyings i) {
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    {
        const float4 t = TEX(0).Sample(SAMP(0), i.uv0).rgba;
        const float4 ras = i.color.rgba;
        const float3 rc = (t.rgb + lerp(float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0)) + 128.0 / 255.0);
        const float ra = (0.0 + lerp(0.0, t.a, ras.a)) * 4.0;
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(0, float4(rc, ra))
    }
    {
        const float4 t = TEX(1).Sample(SAMP(1), i.uv1).rgba;
        const float3 rc = (float3(1.0, 1.0, 1.0) - lerp(float3(0.0, 0.0, 0.0), low8(prev.rgb), t.rgb)) * 4.0;
        const float ra = (0.0 + lerp(0.0, low8(prev.a), t.a));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(1, float4(rc, ra))
    }
    {
        const float4 ras = i.color.rgba;
        const float3 rc = (float3(0.0, 0.0, 0.0) + lerp(float3(0.0, 0.0, 0.0), ras.rgb, low8(prev.rgb)));
        const float ra = (prev.a + lerp(0.0, 0.0, 0.0));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(2, float4(rc, ra))
    }
    prev = low8(float4(prev.rgb, prev.a));
    const float a8 = round(prev.a * 255.0);
    if (!((a8 <= round(m[0].x * 255.0)) || (a8 >= round(m[0].y * 255.0)))) {
        discard;
    }
    prev.rgb = program_fog_curve(prev.rgb, m[2].rgb, fog_amount(i.pos.w, m[1].x, m[1].y), float4(m[1].x, m[1].y, i.pos.w, 2.0));
    PROGRAM_RETURN(saturate(prev));
}
