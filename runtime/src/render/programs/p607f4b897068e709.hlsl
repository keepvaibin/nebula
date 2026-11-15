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
    PER_SAMPLE float4 color : COLOR0;
    PER_SAMPLE float2 uv0 : TEXCOORD0;
    PER_SAMPLE float3 gain : TEXCOORD6;
    VARYINGS_MOTION
};
NativeOut program_ps(Varyings i) {
    TRACED_UNKEYED;
    g_enh = enh_begin(i.lit_eye, i.lit_nrm);
    if (enh_on()) enh_key_highlight(i.lit_light, g_enh, i.lit_eye);
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    float4 r0 = m[0];
    {
        const float4 t = ENV_FETCH(enh_env(TEX(0).Sample(SAMP(0), i.uv0).rgba));
        const float4 ras = i.color.rgba;
        const float3 k = m[1].rgb;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(k.rgb, low8(r0.rgb), t.rgb)));
        const float ra = saturate((0.0 + lerp(0.0, t.a, ras.a)));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(0, float4(rc, ra))
    }
    prev = float4(prev.rgb, prev.a);
    if (enh_on()) prev.rgb = enh_finish(prev.rgb, g_enh);
#if NATIVE_TRACED
    prev.rgb = traced_map_shown(prev.rgb, i.gain);
#endif
    PROGRAM_RETURN(float4(max(prev.rgb, 0.0), saturate(prev.a)));
}
