#include "program.hlsli"
#include "program_lit.hlsli"
cbuffer MaterialPixel : register(b2) {
    float4 m[6];
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
NativeOut program_ps(Varyings i) {
    g_enh = enh_begin(i.lit_eye, i.lit_nrm);
    float4 lit0 = float4(0.0, 0.0, 0.0, 0.0);
    float4 lit1 = float4(0.0, 0.0, 0.0, 0.0);
    if (enh_on()) {
        {
            const float3 enh_mat = m[4].rgb;
            const float3 enh_amb = m[5].rgb;
            const float3 enh_amb_lin = enh_lin(enh_amb);
            float3 enh_acc = AMBIENT_TERM(enh_amb_lin * enh_sky(g_enh));
            enh_add_light(i.lit_light, 0u, true, 1u, true, false, i.lit_eye, enh_amb, enh_amb_lin, enh_acc);
            enh_add_light(i.lit_light, 1u, true, 1u, false, false, i.lit_eye, enh_amb, enh_amb_lin, enh_acc);
            enh_acc *= enh_sky_reflection(g_enh, enh_amb_lin);
            lit0.rgb = enh_mat * TRACED_LEVEL(enh_srgb(enh_shoulder(max(enh_acc, 0.0))));
        }
        lit0.a = m[4].a;
    } else {
        lit0.rgb = m[4].rgb * TRACED_LEVEL(saturate((AMBIENT_TERM(m[5].rgb) * 255.0 + light_term(i.lit_light, 0u, 3u, 1u, i.lit_eye, g_enh.N).rgb + light_term(i.lit_light, 1u, 3u, 1u, i.lit_eye, g_enh.N).rgb) / 255.0));
        lit0.a = m[4].a;
    }
    i.color = lit0;
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    float4 r0 = m[0];
    float4 r1 = float4(0.0, 0.0, 0.0, 0.0);
    float4 r2 = float4(0.0, 0.0, 0.0, 0.0);
    {
        const float4 t = TEX(0).Sample(SAMP(0), i.uv0).rgba;
        const float3 k = m[1].aaa;
        const float3 rc = saturate(float3(0.0, 0.0, 0.0) + ((round((t.aaa).g * 255.0) * 256.0 + round((t.aaa).r * 255.0)) > (round((k.rgb).g * 255.0) * 256.0 + round((k.rgb).r * 255.0)) ? float3(1.0, 1.0, 1.0) : float3(0.0, 0.0, 0.0)));
        const float ra = saturate((0.0 + lerp(0.0, 0.0, 0.0)));
        r2.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(0, float4(rc, ra))
    }
    {
        const float4 t = TEX(1).Sample(SAMP(1), i.uv1).rgba;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(float3(0.0, 0.0, 0.0), t.rgb, r2.rgb)));
        const float ra = saturate((0.0 + lerp(0.0, 0.0, 0.0)));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(1, float4(rc, ra))
    }
    {
        const float4 t = TEX(2).Sample(SAMP(2), i.uv2).rgba;
        const float3 rc = saturate((prev.rgb + lerp(t.rgb, float3(0.0, 0.0, 0.0), r2.rgb)));
        const float ra = saturate((0.0 + lerp(0.0, 0.0, 0.0)));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(2, float4(rc, ra))
    }
    {
        const float4 t = TEX(3).Sample(SAMP(3), i.uv3).rgba;
        const float4 ras = i.color.rgba;
        const float3 k = float3(159.0 / 255.0, 159.0 / 255.0, 159.0 / 255.0);
        const float3 rc = saturate((r0.rgb + lerp(t.rgb, ras.rgb, k.rgb) - 128.0 / 255.0));
        const float ra = (0.0 + lerp(0.0, 0.0, 0.0));
        r1.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(3, float4(rc, ra))
    }
    {
        const float4 ras = i.color.rgba;
        const float3 k = float3(128.0 / 255.0, 128.0 / 255.0, 128.0 / 255.0);
        const float3 rc = saturate((prev.rgb + lerp(r1.rgb, float3(0.0, 0.0, 0.0), k.rgb) - 128.0 / 255.0));
        const float ra = (ras.a + lerp(0.0, 0.0, 0.0));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(4, float4(rc, ra))
    }
    prev = float4(prev.rgb, prev.a);
    if (enh_on()) prev.rgb = enh_finish(prev.rgb, g_enh);
    prev.rgb = program_fog(prev.rgb, m[3].rgb, fog_amount(i.pos.w, m[2].x, m[2].y));
    PROGRAM_RETURN(float4(max(prev.rgb, 0.0), saturate(prev.a)));
}
