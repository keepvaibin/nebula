#include "program.hlsli"
#include "program_lit.hlsli"
cbuffer MaterialPixel : register(b2) {
    float4 m[2];
};
#define NEBULA_MOTION_CURRENT_INDEX 5
#define NEBULA_MOTION_PREVIOUS_INDEX 6
struct Varyings {
    PER_SAMPLE float3 lit_eye : TEXCOORD2;
    PER_SAMPLE float3 lit_nrm : TEXCOORD3;
    nointerpolation uint lit_light : TEXCOORD4;
    float4 pos : SV_Position;
    PER_SAMPLE float2 uv0 : TEXCOORD0;
    PER_SAMPLE float3 uv1 : TEXCOORD1;
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
        const float4 t = ENV_FETCH(enh_env(TEX(0).Sample(SAMP(0), i.uv0).rgba));
        const float ka = 1.0;
        const float3 rc = saturate((r0.rgb + lerp(float3(0.0, 0.0, 0.0), t.rgb, r1.rgb)));
        const float ra = saturate((r0.a + lerp(0.0, t.a, ka)));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(0, float4(rc, ra))
    }
    {
        const float4 t = ENV_FETCH(enh_env(TEX(1).Sample(SAMP(1), (i.uv1.z != 0.0 ? i.uv1.xy / i.uv1.z : i.uv1.xy)).rgba));
        const float3 rc = saturate((prev.rgb + lerp(float3(0.0, 0.0, 0.0), r0.aaa, t.rgb)));
        const float ra = (prev.a + lerp(0.0, 0.0, 0.0));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(1, float4(rc, ra))
    }
    prev = float4(prev.rgb, prev.a);
    if (enh_on()) prev.rgb = enh_finish(prev.rgb, g_enh);
    PROGRAM_RETURN(float4(max(prev.rgb, 0.0), saturate(prev.a)));
}
