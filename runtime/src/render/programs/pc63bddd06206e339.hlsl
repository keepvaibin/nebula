#include "program.hlsli"
cbuffer MaterialPixel : register(b2) {
    float4 m[7];
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
    float2 tevcoord = float2(0.0, 0.0);
    const float3 ind0 = round(TEX(0).Sample(SAMP(0), i.uv0 / float2(1.0, 1.0)).abg * 255.0);
    {
        float2 texel = i.uv1 * m[1].xy;
        float3 sv = floor(ind0 / 1.0);
        sv.x += -128.0;
        sv.y += -128.0;
        sv.z += -128.0;
        const float2 off = float2(dot(m[2].xyz, sv), dot(m[3].xyz, sv));
        tevcoord = texel + off;
        const float4 t = TEX(1).Sample(SAMP(1), tevcoord / m[1].zw).rgba;
        const float4 ras = i.color.rgba;
        const float3 k = m[4].rgb;
        const float ka = 32.0 / 255.0;
        const float3 rc = saturate((k.rgb + lerp(float3(0.0, 0.0, 0.0), ras.rgb, t.rgb) - 128.0 / 255.0));
        const float ra = saturate((r0.a + lerp(t.a, ka, ras.a)));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(0, float4(rc, ra))
    }
    prev = float4(prev.rgb, prev.a);
    prev.rgb = program_fog_curve(prev.rgb, m[6].rgb, fog_amount(i.pos.w, m[5].x, m[5].y), float4(m[5].x, m[5].y, i.pos.w, 2.0));
    PROGRAM_RETURN(saturate(prev));
}
