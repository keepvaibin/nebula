#include "program.hlsli"
cbuffer MaterialPixel : register(b2) {
    float4 m[10];
};
#define NEBULA_MOTION_CURRENT_INDEX 4
#define NEBULA_MOTION_PREVIOUS_INDEX 5
struct Varyings {
    float4 pos : SV_Position;
    PER_SAMPLE float4 color : COLOR0;
    PER_SAMPLE float2 uv0 : TEXCOORD0;
    PER_SAMPLE float3 uv1 : TEXCOORD1;
    PER_SAMPLE float2 uv2 : TEXCOORD2;
    PER_SAMPLE float2 uv3 : TEXCOORD3;
    VARYINGS_MOTION
};
NativeOut program_ps(Varyings i) {
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    float4 r0 = m[0];
    float4 r1 = m[1];
    float4 r2 = float4(0.0, 0.0, 0.0, 0.0);
    float2 tevcoord = float2(0.0, 0.0);
    const float3 ind0 = round(TEX(0).Sample(SAMP(0), i.uv0 / float2(1.0, 1.0)).abg * 255.0);
    {
        const float4 t = ENV_FETCH(TEX(1).Sample(SAMP(1), (i.uv1.z != 0.0 ? i.uv1.xy / i.uv1.z : i.uv1.xy)).rgba);
        const float4 ras = i.color.rgba;
        const float3 rc = (ras.rgb + lerp(low8(r0.rgb), r1.rgb, t.rgb) - 128.0 / 255.0) * 0.5;
        const float ra = saturate((0.0 + lerp(0.0, t.a, ras.a)));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(0, float4(rc, ra))
    }
    {
        float2 texel = i.uv2 * m[2].xy;
        float3 sv = floor(ind0 / 1.0);
        sv.x += -128.0;
        sv.y += -128.0;
        sv.z += -128.0;
        const float2 off = float2(dot(m[3].xyz, sv), dot(m[4].xyz, sv));
        tevcoord = texel + off;
        const float4 t = ENV_FETCH(TEX(2).Sample(SAMP(2), tevcoord / m[2].zw).rgba);
        const float4 ras = i.color.rgba;
        const float3 k = m[5].rgb;
        const float3 rc = saturate((prev.rgb + lerp(k.rgb, ras.rgb, t.rgb) - 128.0 / 255.0));
        const float ra = (0.0 + lerp(ras.a, 0.0, 0.0));
        prev.rgb = rc;
        r2.a = ra;
        NATIVE_STAGE(1, float4(rc, ra))
    }
    {
        float2 texel = i.uv3 * m[6].xy;
        float3 sv = floor(ind0 / 1.0);
        sv.x += -128.0;
        sv.y += -128.0;
        sv.z += -128.0;
        const float2 off = float2(dot(m[3].xyz, sv), dot(m[4].xyz, sv));
        tevcoord = texel + off;
        const float4 t = TEX(3).Sample(SAMP(3), tevcoord / m[6].zw).rgba;
        const float4 ras = i.color.rgba;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(float3(0.0, 0.0, 0.0), prev.rgb, t.rgb)));
        const float ra = (0.0 + lerp(0.0, ras.a, t.a));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(2, float4(rc, ra))
    }
    prev = float4(prev.rgb, prev.a);
    const float a8 = round(prev.a * 255.0);
    if (!((a8 >= round(m[7].x * 255.0)) && (a8 <= round(m[7].y * 255.0)))) {
        discard;
    }
    prev.rgb = lerp(prev.rgb, m[9].rgb, fog_amount(i.pos.w, m[8].x, m[8].y));
    PROGRAM_RETURN(saturate(prev));
}
