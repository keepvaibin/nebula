#include "program.hlsli"
#include "program_lit.hlsli"
#include "program_pbr.hlsli"
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
    PER_SAMPLE float4 color : COLOR0;
    PER_SAMPLE float2 uv0 : TEXCOORD0;
    VARYINGS_MOTION
};
NativeOut program_ps(Varyings i) {
    g_enh = enh_begin(i.lit_eye, i.lit_nrm);
    float4 lit0 = float4(0.0, 0.0, 0.0, 0.0);
    float4 lit1 = float4(0.0, 0.0, 0.0, 0.0);
    {
        const float3 pbr_amb = m[1].rgb;
        pbr_begin(pbr_amb);
        pbr_add_light(i.lit_light, 0u, false, 2u, true, i.lit_eye, pbr_amb);
        pbr_add_light(i.lit_light, 1u, false, 2u, false, i.lit_eye, pbr_amb);
    }
    lit0.rgb = m[0].rgb;
    lit0.a = m[0].a;
    i.color = lit0;
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    {
        const float4 t = TEX(0).Sample(SAMP(0), i.uv0).rgba;
        const float4 ras = i.color.rgba;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(float3(0.0, 0.0, 0.0), t.rgb, ras.rgb)));
        const float ra = saturate((0.0 + lerp(0.0, t.a, ras.a)));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(0, float4(rc, ra))
    }
    prev = float4(prev.rgb, prev.a);
    {
        float pbr_metal = 0.0;
        float pbr_rough = 1.0;
        float pbr_ao = 1.0;
        const float4 pbr_env = ENV_FETCH(pbr_reflection(prev.rgb, pbr_metal, pbr_rough, pbr_ao));
        prev.rgb = pbr_finish(prev.rgb, pbr_metal, pbr_rough, pbr_ao, pbr_env.rgb);
    }
    PROGRAM_RETURN(float4(max(prev.rgb, 0.0), saturate(prev.a)));
}
