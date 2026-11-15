#include "program.hlsli"
#include "program_lit.hlsli"
cbuffer MaterialPixel : register(b2) {
    float4 m[4];
};
#define NEBULA_MOTION_CURRENT_INDEX 5
#define NEBULA_MOTION_PREVIOUS_INDEX 6
struct Varyings {
    PER_SAMPLE float3 lit_eye : TEXCOORD2;
    PER_SAMPLE float3 lit_nrm : TEXCOORD3;
    nointerpolation uint lit_light : TEXCOORD4;
    float4 pos : SV_Position;
    PER_SAMPLE float4 color : COLOR0;
    PER_SAMPLE float2 uv0 : TEXCOORD0;
    PER_SAMPLE float2 uv1 : TEXCOORD1;
    VARYINGS_MOTION
};
NativeOut program_ps(Varyings i) {
    TRACED_UNKEYED;
    g_enh = enh_begin(i.lit_eye, i.lit_nrm);
    enh_sprite(0.604, 1.000);
    if (enh_on()) enh_key_highlight(i.lit_light, g_enh, i.lit_eye);
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    float4 r0 = m[0];
    float4 r1 = m[1];
    {
        const float4 ras = i.color.rgba;
        const float3 k = m[2].rgb;
        const float ka = m[2].a;
        const float3 rc = saturate((r1.rgb + lerp(k.rgb, r0.rgb, ras.rgb) - 128.0 / 255.0));
        const float ra = saturate((0.0 + lerp(ka, 0.0, ras.a)) * 2.0);
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(0, float4(rc, ra))
    }
    {
        const float4 t = TEX(0).Sample(SAMP(0), i.uv0).rgba;
        const float4 ras = i.color.rgba;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(float3(0.0, 0.0, 0.0), prev.rgb, t.rgb)));
        const float ra = saturate((0.0 + lerp(0.0, ras.a, t.a)));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(1, float4(rc, ra))
    }
    {
        const float4 t = HIGHLIGHT_FETCH(TEX(1).Sample(SAMP(1), i.uv1).rgba);
        const float3 k = m[3].rgb;
        const float3 rc = saturate((prev.rgb + lerp(float3(0.0, 0.0, 0.0), k.rgb, t.rgb)));
        const float ra = (0.0 + lerp(prev.a, 0.0, 0.0));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(2, float4(rc, ra))
    }
    prev = float4(prev.rgb, prev.a);
    if (enh_on()) prev.rgb = enh_finish(prev.rgb, g_enh);
    PROGRAM_RETURN(float4(max(prev.rgb, 0.0), saturate(prev.a)));
}
