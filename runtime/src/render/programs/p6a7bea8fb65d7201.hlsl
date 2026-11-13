#include "program.hlsli"
cbuffer MaterialPixel : register(b2) {
    float4 m[6];
};
#define NEBULA_MOTION_CURRENT_INDEX 3
#define NEBULA_MOTION_PREVIOUS_INDEX 4
struct Varyings {
    float4 pos : SV_Position;
    PER_SAMPLE float4 color : COLOR0;
    PER_SAMPLE float2 uv0 : TEXCOORD0;
    PER_SAMPLE float2 uv1 : TEXCOORD1;
    PER_SAMPLE float2 uv2 : TEXCOORD2;
    VARYINGS_MOTION
};
NativeOut program_ps(Varyings i) {
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    float4 r0 = m[0];
    float2 tevcoord = float2(0.0, 0.0);
    const float3 ind0 = round(TEX(0).Sample(SAMP(0), i.uv0 / float2(1.0, 1.0)).abg * 255.0);
    {
        const float4 t = TEX(1).Sample(SAMP(1), i.uv1).rgba;
        const float4 ras = i.color.rgba;
        const float3 rc = (float3(0.0, 0.0, 0.0) + lerp(float3(0.0, 0.0, 0.0), t.rgb, ras.rgb));
        const float ra = saturate((0.0 + lerp(0.0, t.a, ras.a)));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(0, float4(rc, ra))
    }
    {
        float2 texel = i.uv2 * m[1].xy;
        float3 sv = floor(ind0 / 1.0);
        sv.x += -128.0;
        sv.y += -128.0;
        sv.z += -128.0;
        const float2 off = float2(dot(m[2].xyz, sv), dot(m[3].xyz, sv));
        tevcoord = texel + off;
        const float4 t = TEX(2).Sample(SAMP(2), tevcoord / m[1].zw).rgba;
        const float4 ras = i.color.rgba;
        const float3 rc = saturate((prev.rgb + lerp(low8(r0.rgb), ras.rgb, t.rgb)));
        const float ra = (0.0 + lerp(ras.a, 0.0, 0.0));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(1, float4(rc, ra))
    }
    prev = float4(prev.rgb, prev.a);
    prev.rgb = program_fog_curve(prev.rgb, m[5].rgb, fog_amount(i.pos.w, m[4].x, m[4].y), float4(m[4].x, m[4].y, i.pos.w, 2.0));
    PROGRAM_RETURN(saturate(prev));
}
