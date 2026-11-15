#include "program.hlsli"
#include "program_lit.hlsli"
cbuffer MaterialPixel : register(b2) {
    float4 m[3];
};
#define NEBULA_MOTION_CURRENT_INDEX 5
#define NEBULA_MOTION_PREVIOUS_INDEX 6
struct Varyings {
    PER_SAMPLE float3 lit_eye : TEXCOORD2;
    PER_SAMPLE float3 lit_nrm : TEXCOORD3;
    nointerpolation uint lit_light : TEXCOORD4;
    float4 pos : SV_Position;
    PER_SAMPLE float4 color : COLOR0;
    PER_SAMPLE float3 uv0 : TEXCOORD0;
    PER_SAMPLE float3 uv1 : TEXCOORD1;
    PER_SAMPLE float3 gain : TEXCOORD7;
    VARYINGS_MOTION
};
NativeOut program_ps(Varyings i) {
    TRACED_UNKEYED;
    g_enh = enh_begin(i.lit_eye, i.lit_nrm);
    if (enh_on()) enh_key_highlight(i.lit_light, g_enh, i.lit_eye);
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    float4 r0 = m[0];
    float4 r1 = m[1];
    {
        const float4 t = ENV_FETCH(enh_env(TEX(0).Sample(SAMP(0), (i.uv0.z != 0.0 ? i.uv0.xy / i.uv0.z : i.uv0.xy)).rgba));
        const float4 ras = i.color.rgba;
        const float3 rc = (float3(0.0, 0.0, 0.0) + lerp(float3(0.0, 0.0, 0.0), t.rgb, r1.rgb));
        const float ra = saturate((0.0 + lerp(0.0, t.a, ras.a)));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(0, float4(rc, ra))
    }
    {
        const float4 t = ENV_FETCH(enh_env(TEX(1).Sample(SAMP(1), (i.uv1.z != 0.0 ? i.uv1.xy / i.uv1.z : i.uv1.xy)).rgba));
        const float4 ras = i.color.rgba;
        const float ka = 96.0 / 255.0;
        const float3 rc = saturate((prev.rgb + lerp(float3(0.0, 0.0, 0.0), r0.rgb, t.rgb)));
        const float ra = saturate((r0.a + lerp(t.a, ka, ras.a)) * 4.0);
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(1, float4(rc, ra))
    }
    prev = float4(prev.rgb, prev.a);
    const float a8 = round(prev.a * 255.0);
    if (!((a8 >= round(m[2].x * 255.0)) && (a8 <= round(m[2].y * 255.0)))) {
        discard;
    }
    if (enh_on()) prev.rgb = enh_finish(prev.rgb, g_enh);
#if NATIVE_TRACED
    prev.rgb = traced_map_shown(prev.rgb, i.gain);
#endif
    PROGRAM_RETURN(float4(max(prev.rgb, 0.0), saturate(prev.a)));
}
