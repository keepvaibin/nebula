#include "program.hlsli"
cbuffer MaterialPixel : register(b2) {
    float4 m[4];
};
#define NEBULA_MOTION_CURRENT_INDEX 4
#define NEBULA_MOTION_PREVIOUS_INDEX 5
struct Varyings {
    float4 pos : SV_Position;
    PER_SAMPLE float4 color : COLOR0;
    PER_SAMPLE float2 uv0 : TEXCOORD0;
    PER_SAMPLE float2 uv1 : TEXCOORD1;
    PER_SAMPLE float2 uv2 : TEXCOORD2;
    PER_SAMPLE float2 uv3 : TEXCOORD3;
    VARYINGS_MOTION
};
NativeOut program_ps(Varyings i) {
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    float4 r0 = m[0];
    float4 r1 = float4(0.0, 0.0, 0.0, 0.0);
    float4 r2 = float4(0.0, 0.0, 0.0, 0.0);
    {
        const float4 t = TEX(0).Sample(SAMP(0), i.uv0).rgba;
        const float3 k = m[1].aaa;
        const float3 rc = saturate(float3(0.0, 0.0, 0.0) + ((round((t.aaa).g * 255.0) * 256.0 + round((t.aaa).r * 255.0)) > (round((k.rgb).g * 255.0) * 256.0 + round((k.rgb).r * 255.0)) ? float3(1.0, 1.0, 1.0) : float3(0.0, 0.0, 0.0)));
        const float ra = saturate((0.0 + lerp(0.0, 0.0, 0.0)));
        r2.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(0, float4(rc, ra))
    }
    {
        const float4 t = TEX(1).Sample(SAMP(1), i.uv1).rgba;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(float3(0.0, 0.0, 0.0), t.rgb, r2.rgb)));
        const float ra = saturate((0.0 + lerp(0.0, 0.0, 0.0)));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(1, float4(rc, ra))
    }
    {
        const float4 t = TEX(2).Sample(SAMP(2), i.uv2).rgba;
        const float3 rc = saturate((prev.rgb + lerp(t.rgb, float3(0.0, 0.0, 0.0), r2.rgb)));
        const float ra = saturate((0.0 + lerp(0.0, 0.0, 0.0)));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(2, float4(rc, ra))
    }
    {
        const float4 t = TEX(3).Sample(SAMP(3), i.uv3).rgba;
        const float4 ras = i.color.rgba;
        const float3 k = float3(159.0 / 255.0, 159.0 / 255.0, 159.0 / 255.0);
        const float3 rc = saturate((r0.rgb + lerp(t.rgb, ras.rgb, k.rgb) - 128.0 / 255.0));
        const float ra = (0.0 + lerp(0.0, 0.0, 0.0));
        r1.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(3, float4(rc, ra))
    }
    {
        const float4 ras = i.color.rgba;
        const float3 k = float3(128.0 / 255.0, 128.0 / 255.0, 128.0 / 255.0);
        const float3 rc = saturate((prev.rgb + lerp(r1.rgb, float3(0.0, 0.0, 0.0), k.rgb) - 128.0 / 255.0));
        const float ra = (ras.a + lerp(0.0, 0.0, 0.0));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(4, float4(rc, ra))
    }
    prev = float4(prev.rgb, prev.a);
    prev.rgb = program_fog_curve(prev.rgb, m[3].rgb, fog_amount(i.pos.w, m[2].x, m[2].y), float4(m[2].x, m[2].y, i.pos.w, 2.0));
    PROGRAM_RETURN(saturate(prev));
}
