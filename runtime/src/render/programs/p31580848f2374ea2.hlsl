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
    g_enh = enh_begin(i.lit_eye, i.lit_nrm);
    enh_sprite(0.604, 0.577);
    float4 lit0 = float4(0.0, 0.0, 0.0, 0.0);
    float4 lit1 = float4(0.0, 0.0, 0.0, 0.0);
    if (enh_on()) {
        {
            const float3 enh_mat = i.color.rgb;
            const float3 enh_amb = m[2].rgb;
            const float3 enh_amb_lin = enh_lin(enh_amb);
            float3 enh_acc = AMBIENT_TERM(enh_amb_lin * enh_sky(g_enh));
            enh_add_light(i.lit_light, 0u, true, 2u, true, false, i.lit_eye, enh_amb, enh_amb_lin, enh_acc);
            enh_add_light(i.lit_light, 1u, true, 2u, false, false, i.lit_eye, enh_amb, enh_amb_lin, enh_acc);
            enh_acc *= enh_sky_reflection(g_enh, enh_amb_lin);
            lit0.rgb = enh_mat * TRACED_LEVEL(enh_srgb(enh_shoulder(max(enh_acc, 0.0))));
        }
        lit0.a = m[3].a * saturate((m[2].a * 255.0 + light_term(i.lit_light, 2u, 3u, 1u, i.lit_eye, g_enh.N).a) / 255.0);
    } else {
        lit0.rgb = i.color.rgb * TRACED_LEVEL(saturate((AMBIENT_TERM(m[2].rgb) * 255.0 + light_term(i.lit_light, 0u, 3u, 2u, i.lit_eye, g_enh.N).rgb + light_term(i.lit_light, 1u, 3u, 2u, i.lit_eye, g_enh.N).rgb) / 255.0));
        lit0.a = m[3].a * saturate((AMBIENT_TERM(m[2].a) * 255.0 + light_term(i.lit_light, 2u, 3u, 1u, i.lit_eye, g_enh.N).a) / 255.0);
    }
    i.color = lit0;
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    float4 r0 = m[0];
    float4 r1 = m[1];
    {
        const float4 t = TEX(0).Sample(SAMP(0), i.uv0).rgba;
        const float4 ras = i.color.rgba;
        const float ka = 32.0 / 255.0;
        const float3 rc = saturate((r0.rgb + lerp(r1.rgb, t.rgb, ras.rgb)));
        const float ra = saturate((r0.a + lerp(t.a, ka, ras.a)) * 2.0);
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(0, float4(rc, ra))
    }
    {
        const float4 ras = i.color.rgba;
        const float ka = 64.0 / 255.0;
        const float3 rc = saturate((prev.rgb + lerp(float3(0.0, 0.0, 0.0), ras.rgb, prev.aaa)));
        const float ra = saturate((r0.a + lerp(0.0, ka, ras.a)) * 2.0);
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(1, float4(rc, ra))
    }
    {
        const float4 t = HIGHLIGHT_FETCH(TEX(1).Sample(SAMP(1), i.uv1).rgba);
        const float4 ras = i.color.rgba;
        const float3 rc = saturate((prev.rgb + lerp(float3(0.0, 0.0, 0.0), t.rgb, ras.rgb)));
        const float ra = (0.0 + lerp(prev.a, 0.0, 0.0));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(2, float4(rc, ra))
    }
    prev = float4(prev.rgb, prev.a);
    if (enh_on()) prev.rgb = enh_finish(prev.rgb, g_enh);
    PROGRAM_RETURN(float4(max(prev.rgb, 0.0), saturate(prev.a)));
}
