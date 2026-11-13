#include "program.hlsli"
cbuffer MaterialPixel : register(b2) {
    float4 m[3];
};
#define NEBULA_MOTION_CURRENT_INDEX 2
#define NEBULA_MOTION_PREVIOUS_INDEX 3
struct Varyings {
    float4 pos : SV_Position;
    PER_SAMPLE float4 color : COLOR0;
    PER_SAMPLE float4 color1 : COLOR1;
    PER_SAMPLE float2 uv0 : TEXCOORD0;
    PER_SAMPLE float2 uv1 : TEXCOORD1;
    VARYINGS_MOTION
};
NativeOut program_ps(Varyings i) {
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    float4 r0 = m[0];
    {
        const float4 t = TEX(0).Sample(SAMP(0), i.uv0).rgba;
        const float4 ras = i.color.rgba;
        const float3 k = float3(159.0 / 255.0, 159.0 / 255.0, 159.0 / 255.0);
        const float3 rc = saturate((r0.rgb + lerp(t.rgb, ras.rgb, k.rgb) - 128.0 / 255.0));
        const float ra = saturate((0.0 + lerp(0.0, 0.0, 0.0)));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(0, float4(rc, ra))
    }
    {
        const float4 t = TEX(1).Sample(SAMP(1), i.uv1).rgba;
        const float4 ras = i.color1.rgba;
        const float3 k = float3(128.0 / 255.0, 128.0 / 255.0, 128.0 / 255.0);
        const float3 rc = saturate((t.rgb + lerp(prev.rgb, ras.rgb, k.rgb) - 128.0 / 255.0));
        const float ra = saturate((prev.a + lerp(0.0, t.a, ras.a)));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(1, float4(rc, ra))
    }
    prev = float4(prev.rgb, prev.a);
    prev.rgb = program_fog_curve(prev.rgb, m[2].rgb, fog_amount(i.pos.w, m[1].x, m[1].y), float4(m[1].x, m[1].y, i.pos.w, 2.0));
    PROGRAM_RETURN(saturate(prev));
}
