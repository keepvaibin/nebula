#include "program.hlsli"
#include "program_lit.hlsli"
cbuffer MaterialPixel : register(b2) {
    float4 m[5];
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
#if NATIVE_TRACED || NATIVE_PREPASS
#define TAP(name, sample) (sample)
#else
#define TAP(name, sample) name
#endif
NativeOut program_ps(Varyings i) {
    g_enh = enh_begin(i.lit_eye, i.lit_nrm);
    enh_sprite(0.743, 0.725);
    float4 lit0 = float4(0.0, 0.0, 0.0, 0.0);
    float4 lit1 = float4(0.0, 0.0, 0.0, 0.0);
    if (enh_on()) {
        {
            const float3 enh_mat = m[3].rgb;
            const float3 enh_amb = m[4].rgb;
            const float3 enh_amb_lin = enh_lin(enh_amb);
            float3 enh_acc = AMBIENT_TERM(enh_amb_lin * enh_sky(g_enh));
            enh_add_light(i.lit_light, 0u, true, 2u, true, false, i.lit_eye, enh_amb, enh_amb_lin, enh_acc);
            enh_acc *= enh_sky_reflection(g_enh, enh_amb_lin);
            lit0.rgb = enh_mat * TRACED_LEVEL(enh_srgb(enh_shoulder(max(enh_acc, 0.0))));
        }
        lit0.a = m[3].a;
    } else {
        lit0.rgb = m[3].rgb * TRACED_LEVEL(saturate((AMBIENT_TERM(m[4].rgb) * 255.0 + light_term(i.lit_light, 0u, 3u, 2u, i.lit_eye, g_enh.N).rgb) / 255.0));
        lit0.a = m[3].a;
    }
    i.color = lit0;
#if !(NATIVE_TRACED || NATIVE_PREPASS)
    const float4 tap0 = TEX(0).Sample(SAMP(0), i.uv0);
#endif
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    float4 r0 = m[0];
    float4 r1 = m[1];
    float4 r2 = m[2];
    {
        const float4 t = TAP(tap0, TEX(0).Sample(SAMP(0), i.uv0)).rgba;
        const float4 ras = i.color.rgba;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(r1.rgb, r0.rgb, ras.rgb)) * 2.0);
        const float ra = saturate((0.0 + lerp(0.0, t.a, ras.a)));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(0, float4(rc, ra))
    }
    {
        const float4 t = TAP(tap0, TEX(0).Sample(SAMP(0), i.uv0)).rgba;
        const float4 ras = i.color.rgba;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(float3(0.0, 0.0, 0.0), prev.rgb, t.rgb)) * 2.0);
        const float ra = (0.0 + lerp(ras.a, 0.0, 0.0));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(1, float4(rc, ra))
    }
    {
        const float4 t = HIGHLIGHT_FETCH(TEX(1).Sample(SAMP(1), i.uv1).rgba);
        const float4 ras = i.color.rgba;
        const float3 rc = saturate((prev.rgb + lerp(float3(0.0, 0.0, 0.0), t.rgb, r2.rgb)));
        const float ra = (0.0 + lerp(ras.a, 0.0, 0.0));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(2, float4(rc, ra))
    }
    prev = float4(prev.rgb, prev.a);
    if (enh_on()) prev.rgb = enh_finish(prev.rgb, g_enh);
    PROGRAM_RETURN(float4(max(prev.rgb, 0.0), saturate(prev.a)));
}
