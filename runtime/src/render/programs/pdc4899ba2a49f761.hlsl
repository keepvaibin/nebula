#include "program.hlsli"
cbuffer MaterialPixel : register(b2) {
    float4 m[8];
};
#define NEBULA_MOTION_CURRENT_INDEX 3
#define NEBULA_MOTION_PREVIOUS_INDEX 4
struct Varyings {
    float4 pos : SV_Position;
    PER_SAMPLE float4 color : COLOR0;
    PER_SAMPLE float2 uv0 : TEXCOORD0;
    PER_SAMPLE float3 uv1 : TEXCOORD1;
    PER_SAMPLE float2 uv2 : TEXCOORD2;
    VARYINGS_MOTION
};
NativeOut program_ps(Varyings i) {
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    float4 r0 = m[0];
    float4 r1 = m[1];
    float2 tevcoord = float2(0.0, 0.0);
    const float3 ind0 = round(TEX(0).Sample(SAMP(0), i.uv0 / float2(1.0, 1.0)).abg * 255.0);
    {
        const float4 t = ENV_FETCH(TEX(1).Sample(SAMP(1), (i.uv1.z != 0.0 ? i.uv1.xy / i.uv1.z : i.uv1.xy)).rgba);
        const float4 ras = i.color.rgba;
        const float ka = m[2].a;
        const float3 rc = (float3(0.0, 0.0, 0.0) + lerp(low8(r0.rgb), r1.rgb, t.rgb));
        const float ra = saturate((0.0 + lerp(ka, 0.0, ras.a)) * 2.0);
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(0, float4(rc, ra))
    }
    {
        float2 texel = i.uv2 * m[3].xy;
        float3 sv = floor(ind0 / 1.0);
        sv.x += -128.0;
        sv.y += -128.0;
        sv.z += -128.0;
        const float2 off = float2(dot(m[4].xyz, sv), dot(m[5].xyz, sv));
        tevcoord = texel + off;
        const float4 t = ENV_FETCH(TEX(2).Sample(SAMP(2), tevcoord / m[3].zw).rgba);
        const float4 ras = i.color.rgba;
        const float3 rc = saturate((prev.aaa + lerp(float3(0.0, 0.0, 0.0), ras.rgb, low8(prev.rgb))));
        const float ra = (0.0 + lerp(0.0, t.a, ras.a));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(1, float4(rc, ra))
    }
    prev = float4(prev.rgb, prev.a);
    prev.rgb = program_fog_curve(prev.rgb, m[7].rgb, fog_amount(i.pos.w, m[6].x, m[6].y), float4(m[6].x, m[6].y, i.pos.w, 2.0));
    PROGRAM_RETURN(saturate(prev));
}
