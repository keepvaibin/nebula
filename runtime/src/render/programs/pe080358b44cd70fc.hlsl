#include "program.hlsli"
#include "program_lit.hlsli"
cbuffer MaterialPixel : register(b2) {
    float4 m[8];
};
#define NEBULA_MOTION_CURRENT_INDEX 7
#define NEBULA_MOTION_PREVIOUS_INDEX 8
struct Varyings {
    PER_SAMPLE float3 lit_eye : TEXCOORD4;
    PER_SAMPLE float3 lit_nrm : TEXCOORD5;
    nointerpolation uint lit_light : TEXCOORD6;
    float4 pos : SV_Position;
    PER_SAMPLE float4 color : COLOR0;
    PER_SAMPLE float2 uv0 : TEXCOORD0;
    PER_SAMPLE float2 uv1 : TEXCOORD1;
    PER_SAMPLE float2 uv2 : TEXCOORD2;
    PER_SAMPLE float2 uv3 : TEXCOORD3;
    PER_SAMPLE float3 gain : TEXCOORD9;
    VARYINGS_MOTION
};
NativeOut program_ps(Varyings i) {
    TRACED_UNKEYED;
    g_enh = enh_begin(i.lit_eye, i.lit_nrm);
    if (enh_on()) enh_key_highlight(i.lit_light, g_enh, i.lit_eye);
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
        const float3 k = m[4].rgb;
        const float ka = m[4].a;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(float3(0.0, 0.0, 0.0), k.rgb, t.rgb)) * 2.0);
        const float ra = saturate((0.0 + lerp(0.0, t.a, ka)));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(0, float4(rc, ra))
    }
    {
        float2 texel = i.uv2 * m[5].xy;
        float3 sv = floor(ind0 / 1.0);
        sv.x += -128.0;
        sv.y += -128.0;
        sv.z += -128.0;
        const float2 off = float2(dot(m[2].xyz, sv), dot(m[3].xyz, sv));
        tevcoord = texel + off;
        const float4 t = TEX(2).Sample(SAMP(2), tevcoord / m[5].zw).rgba;
        const float4 ras = i.color.rgba;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(r0.rgb, prev.rgb, t.rgb)));
        const float ra = (0.0 + lerp(ras.a, 0.0, 0.0));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(1, float4(rc, ra))
    }
    {
        float2 texel = i.uv3 * m[6].xy;
        float3 sv = floor(ind0 / 1.0);
        sv.x += -128.0;
        sv.y += -128.0;
        sv.z += -128.0;
        const float2 off = float2(dot(m[2].xyz, sv), dot(m[3].xyz, sv));
        tevcoord = texel + off;
        const float4 t = ENV_FETCH(enh_env(TEX(3).Sample(SAMP(3), tevcoord / m[6].zw).rgba));
        const float4 ras = i.color.rgba;
        const float3 k = m[7].rgb;
        const float3 rc = saturate((k.rgb + lerp(prev.rgb, ras.rgb, t.rgb) - 128.0 / 255.0));
        const float ra = saturate((0.0 + lerp(ras.a, 0.0, 0.0)));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(2, float4(rc, ra))
    }
    prev = float4(prev.rgb, prev.a);
    if (enh_on()) prev.rgb = enh_finish(prev.rgb, g_enh);
#if NATIVE_TRACED
    prev.rgb = traced_map_shown(prev.rgb, i.gain);
#endif
    PROGRAM_RETURN(float4(max(prev.rgb, 0.0), saturate(prev.a)));
}
