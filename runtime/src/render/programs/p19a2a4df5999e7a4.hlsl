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
    VARYINGS_MOTION
};
#if NATIVE_TRACED || NATIVE_PREPASS
#define TAP(name, sample) (sample)
#else
#define TAP(name, sample) name
#endif
NativeOut program_ps(Varyings i) {
    g_enh = enh_begin(i.lit_eye, i.lit_nrm);
    float4 lit0 = float4(0.0, 0.0, 0.0, 0.0);
    float4 lit1 = float4(0.0, 0.0, 0.0, 0.0);
    if (enh_on()) {
        lit0.rgb = i.color.rgb;
        lit0.a = i.color.a * saturate((m[7].a * 255.0 + light_term(i.lit_light, 2u, 3u, 2u, i.lit_eye, g_enh.N).a) / 255.0);
    } else {
        lit0.rgb = i.color.rgb;
        lit0.a = i.color.a * saturate((AMBIENT_TERM(m[7].a) * 255.0 + light_term(i.lit_light, 2u, 3u, 2u, i.lit_eye, g_enh.N).a) / 255.0);
    }
    i.color = lit0;
#if !(NATIVE_TRACED || NATIVE_PREPASS)
    const float4 tap0 = TEX(1).Sample(SAMP(1), i.uv1);
#endif
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    float4 r0 = m[0];
    float2 tevcoord = float2(0.0, 0.0);
    const float3 ind0 = round(TEX(0).Sample(SAMP(0), i.uv0 / float2(1.0, 1.0)).abg * 255.0);
    {
        const float4 t = TAP(tap0, TEX(1).Sample(SAMP(1), i.uv1)).rgba;
        const float4 ras = i.color.rgba;
        const float3 k = m[1].rgb;
        const float ka = 96.0 / 255.0;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(k.rgb, r0.rgb, ras.rgb)) * 4.0);
        const float ra = saturate((r0.a + lerp(t.a, ka, ras.a)) * 2.0);
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(0, float4(rc, ra))
    }
    {
        const float4 t = TAP(tap0, TEX(1).Sample(SAMP(1), i.uv1)).rgba;
        const float3 rc = saturate((prev.aaa + lerp(float3(0.0, 0.0, 0.0), prev.rgb, t.rgb)));
        const float ra = saturate((0.0 + lerp(0.0, 0.0, 0.0)));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(1, float4(rc, ra))
    }
    {
        float2 texel = i.uv2 * m[2].xy;
        float3 sv = floor(ind0 / 1.0);
        sv.x += -128.0;
        sv.y += -128.0;
        sv.z += -128.0;
        const float2 off = float2(dot(m[3].xyz, sv), dot(m[4].xyz, sv));
        tevcoord = texel + off;
        const float4 t = ENV_FETCH(enh_env(TEX(2).Sample(SAMP(2), tevcoord / m[2].zw).rgba));
        const float4 ras = i.color.rgba;
        const float3 k = m[5].rgb;
        const float3 rc = saturate((prev.rgb + lerp(float3(0.0, 0.0, 0.0), k.rgb, t.rgb)));
        const float ra = (0.0 + lerp(ras.a, 0.0, 0.0));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(2, float4(rc, ra))
    }
    {
        const float4 t = TEX(3).Sample(SAMP(3), i.uv3).rgba;
        const float4 ras = i.color.rgba;
        const float3 k = m[6].rgb;
        const float3 rc = saturate((prev.rgb + lerp(float3(0.0, 0.0, 0.0), k.rgb, t.rgb)));
        const float ra = (0.0 + lerp(ras.a, 0.0, 0.0));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(3, float4(rc, ra))
    }
    prev = float4(prev.rgb, prev.a);
    if (enh_on()) prev.rgb = enh_finish_bare(prev.rgb);
    PROGRAM_RETURN(float4(max(prev.rgb, 0.0), saturate(prev.a)));
}
