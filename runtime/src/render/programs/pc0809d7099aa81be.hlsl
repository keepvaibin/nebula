#include "program.hlsli"
#include "program_lit.hlsli"
cbuffer MaterialPixel : register(b2) {
    float4 m[1];
};
#define NEBULA_MOTION_CURRENT_INDEX 6
#define NEBULA_MOTION_PREVIOUS_INDEX 7
struct Varyings {
    PER_SAMPLE float3 lit_eye : TEXCOORD3;
    PER_SAMPLE float3 lit_nrm : TEXCOORD4;
    nointerpolation uint lit_light : TEXCOORD5;
    float4 pos : SV_Position;
    PER_SAMPLE float4 color : COLOR0;
    PER_SAMPLE float2 uv0 : TEXCOORD0;
    PER_SAMPLE float2 uv1 : TEXCOORD1;
    PER_SAMPLE float2 uv2 : TEXCOORD2;
    PER_SAMPLE float3 gain : TEXCOORD8;
    VARYINGS_MOTION
};
NativeOut program_ps(Varyings i) {
    TRACED_UNKEYED;
    g_enh = enh_begin(i.lit_eye, i.lit_nrm);
    if (enh_on()) enh_key_highlight(i.lit_light, g_enh, i.lit_eye);
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    float2 tevcoord = float2(0.0, 0.0);
    const float3 ind0 = round(TEX(0).Sample(SAMP(0), i.uv0 / float2(1.0, 1.0)).abg * 255.0);
    {
        float2 texel = i.uv1 * m[0].xy;
        float3 sv = floor(ind0 / 1.0);
        sv.x += -128.0;
        sv.y += -128.0;
        sv.z += -128.0;
        const float2 off = texel * sv.x / 256.0;
        tevcoord = texel + off;
        const float4 t = ENV_FETCH(enh_env(TEX(1).Sample(SAMP(1), tevcoord / m[0].zw).rgba));
        const float4 ras = i.color.rgba;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(float3(0.0, 0.0, 0.0), ras.rgb, t.rgb)));
        const float ra = saturate((0.0 + lerp(0.0, ras.a, t.a)));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(0, float4(rc, ra))
    }
    {
        const float4 t = ENV_FETCH(enh_env(TEX(2).Sample(SAMP(2), i.uv2).rgba));
        const float4 ras = i.color.rgba;
        const float3 rc = saturate((prev.rgb + lerp(float3(0.0, 0.0, 0.0), float3(1.0, 1.0, 1.0), t.rgb)));
        const float ra = (0.0 + lerp(ras.a, 0.0, 0.0));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(1, float4(rc, ra))
    }
    prev = float4(prev.rgb, prev.a);
    if (enh_on()) prev.rgb = enh_finish(prev.rgb, g_enh);
#if NATIVE_TRACED
    prev.rgb = traced_map_shown(prev.rgb, i.gain);
#endif
    PROGRAM_RETURN(float4(max(prev.rgb, 0.0), saturate(prev.a)));
}
