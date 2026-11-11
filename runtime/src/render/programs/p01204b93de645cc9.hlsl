#include "program.hlsli"
cbuffer MaterialPixel : register(b2) {
    float4 m[5];
};
#define NEBULA_MOTION_CURRENT_INDEX 3
#define NEBULA_MOTION_PREVIOUS_INDEX 4
struct Varyings {
    float4 pos : SV_Position;
    PER_SAMPLE float4 color : COLOR0;
    PER_SAMPLE float3 uv0 : TEXCOORD0;
    PER_SAMPLE float2 uv1 : TEXCOORD1;
    PER_SAMPLE float3 uv2 : TEXCOORD2;
    VARYINGS_MOTION
};
NativeOut program_ps(Varyings i) {
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    float4 r0 = m[0];
    float4 r1 = m[1];
    float2 tevcoord = float2(0.0, 0.0);
    const float3 ind0 = round(TEX(0).Sample(SAMP(0), (i.uv0.z != 0.0 ? i.uv0.xy / i.uv0.z : i.uv0.xy) / float2(1.0, 1.0)).abg * 255.0);
    {
        const float4 t = TEX(1).Sample(SAMP(1), i.uv1).rgba;
        const float4 ras = i.color.rgba;
        const float ka = 128.0 / 255.0;
        const float3 rc = saturate((r0.rgb + lerp(r1.rgb, t.rgb, t.rgb)));
        const float ra = saturate((r0.a + lerp(0.0, ka, ras.a)) * 4.0);
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(0, float4(rc, ra))
    }
    {
        const float4 t = TEX(1).Sample(SAMP(1), i.uv1).rgba;
        const float ka = 223.0 / 255.0;
        const float3 rc = saturate((prev.rgb + lerp(float3(0.0, 0.0, 0.0), t.rgb, prev.aaa)) * 2.0);
        const float ra = saturate((prev.a + lerp(0.0, ka, t.a)));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(1, float4(rc, ra))
    }
    {
        float2 texel = (i.uv2.z != 0.0 ? i.uv2.xy / i.uv2.z : i.uv2.xy) * m[2].xy;
        float3 sv = floor(ind0 / 1.0);
        sv.x += -128.0;
        sv.y += -128.0;
        sv.z += -128.0;
        const float2 off = float2(dot(m[3].xyz, sv), dot(m[4].xyz, sv));
        tevcoord = texel + off;
        const float4 t = ENV_FETCH(TEX(2).Sample(SAMP(2), tevcoord / m[2].zw).rgba);
        const float3 k = float3(191.0 / 255.0, 191.0 / 255.0, 191.0 / 255.0);
        const float3 rc = saturate((r0.rgb + lerp(float3(1.0, 1.0, 1.0), k.rgb, t.rgb)));
        const float ra = saturate((prev.a + lerp(0.0, 0.0, 0.0)));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(2, float4(rc, ra))
    }
    prev = float4(prev.rgb, prev.a);
    PROGRAM_RETURN(saturate(prev));
}
