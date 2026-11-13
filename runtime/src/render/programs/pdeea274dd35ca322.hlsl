#include "program.hlsli"
#include "program_lit.hlsli"
cbuffer MaterialPixel : register(b2) {
    float4 m[2];
};
#define NEBULA_MOTION_CURRENT_INDEX 4
#define NEBULA_MOTION_PREVIOUS_INDEX 5
struct Varyings {
    PER_SAMPLE float3 lit_eye : TEXCOORD1;
    PER_SAMPLE float3 lit_nrm : TEXCOORD2;
    nointerpolation uint lit_light : TEXCOORD3;
    float4 pos : SV_Position;
    PER_SAMPLE float2 uv0 : TEXCOORD0;
    VARYINGS_MOTION
};
NativeOut program_ps(Varyings i) {
    g_enh = enh_begin(i.lit_eye, i.lit_nrm);
    float4 lit0 = float4(0.0, 0.0, 0.0, 0.0);
    float4 lit1 = float4(0.0, 0.0, 0.0, 0.0);
    if (enh_on()) {
    } else {
    }
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    float4 r0 = m[0];
    {
        const float4 t = ENV_FETCH(enh_env(TEX(0).Sample(SAMP(0), i.uv0).rgba));
        const float ka = m[1].a;
        const float3 rc = saturate((t.aaa + lerp(float3(0.0, 0.0, 0.0), t.rgb, r0.rgb)) * 4.0);
        const float ra = saturate((0.0 + lerp(0.0, r0.a, ka)));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(0, float4(rc, ra))
    }
    prev = float4(prev.rgb, prev.a);
    if (enh_on()) prev.rgb = enh_finish(prev.rgb, g_enh);
    PROGRAM_RETURN(float4(max(prev.rgb, 0.0), saturate(prev.a)));
}
