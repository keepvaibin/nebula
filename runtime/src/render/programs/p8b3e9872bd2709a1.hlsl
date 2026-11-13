#include "program.hlsli"
#include "program_lit.hlsli"
cbuffer MaterialPixel : register(b2) {
    float4 m[8];
};
#define NEBULA_MOTION_CURRENT_INDEX 6
#define NEBULA_MOTION_PREVIOUS_INDEX 7
struct Varyings {
    PER_SAMPLE float3 lit_eye : TEXCOORD3;
    PER_SAMPLE float3 lit_nrm : TEXCOORD4;
    nointerpolation uint lit_light : TEXCOORD5;
    float4 pos : SV_Position;
    PER_SAMPLE float4 color : COLOR0;
    PER_SAMPLE float2 uv0 : TEXCOORD0;
    PER_SAMPLE float2 uv1 : TEXCOORD1;
    PER_SAMPLE float2 uv2 : TEXCOORD2;
    VARYINGS_MOTION
};
NativeOut program_ps(Varyings i) {
    g_enh = enh_begin(i.lit_eye, i.lit_nrm);
    enh_sprite(0.592, 0.988);
    float4 lit0 = float4(0.0, 0.0, 0.0, 0.0);
    float4 lit1 = float4(0.0, 0.0, 0.0, 0.0);
    if (enh_on()) {
        {
            const float3 enh_mat = i.color.rgb;
            const float3 enh_amb = m[6].rgb;
            const float3 enh_amb_lin = enh_lin(enh_amb);
            float3 enh_acc = AMBIENT_TERM(enh_amb_lin * enh_sky(g_enh));
            enh_add_light(i.lit_light, 0u, true, 2u, true, false, i.lit_eye, enh_amb, enh_amb_lin, enh_acc);
            enh_acc *= enh_sky_reflection(g_enh, enh_amb_lin);
            lit0.rgb = enh_mat * TRACED_LEVEL(enh_srgb(enh_shoulder(max(enh_acc, 0.0))));
        }
        lit0.a = m[7].a * saturate((m[6].a * 255.0 + light_term(i.lit_light, 2u, 3u, 2u, i.lit_eye, g_enh.N).a) / 255.0);
    } else {
        lit0.rgb = i.color.rgb * TRACED_LEVEL(saturate((AMBIENT_TERM(m[6].rgb) * 255.0 + light_term(i.lit_light, 0u, 3u, 2u, i.lit_eye, g_enh.N).rgb) / 255.0));
        lit0.a = m[7].a * saturate((AMBIENT_TERM(m[6].a) * 255.0 + light_term(i.lit_light, 2u, 3u, 2u, i.lit_eye, g_enh.N).a) / 255.0);
    }
    i.color = lit0;
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    float4 r0 = m[0];
    float2 tevcoord = float2(0.0, 0.0);
    const float3 ind0 = round(TEX(0).Sample(SAMP(0), i.uv0 / float2(1.0, 1.0)).abg * 255.0);
    {
        const float4 t = TEX(1).Sample(SAMP(1), i.uv1).rgba;
        const float4 ras = i.color.rgba;
        const float3 rc = (float3(0.0, 0.0, 0.0) + lerp(float3(0.0, 0.0, 0.0), t.rgb, ras.rgb));
        const float ra = saturate((0.0 + lerp(0.0, t.a, ras.a)));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(0, float4(rc, ra))
    }
    {
        float2 texel = i.uv2 * m[1].xy;
        float3 sv = floor(ind0 / 1.0);
        sv.x += -128.0;
        sv.y += -128.0;
        sv.z += -128.0;
        const float2 off = float2(dot(m[2].xyz, sv), dot(m[3].xyz, sv));
        tevcoord = texel + off;
        const float4 t = HIGHLIGHT_FETCH(TEX(2).Sample(SAMP(2), tevcoord / m[1].zw).rgba);
        const float4 ras = i.color.rgba;
        const float3 rc = saturate((prev.rgb + lerp(r0.rgb, ras.rgb, t.rgb)) * 2.0);
        const float ra = (0.0 + lerp(ras.a, 0.0, 0.0));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(1, float4(rc, ra))
    }
    prev = float4(prev.rgb, prev.a);
    if (enh_on()) prev.rgb = enh_finish(prev.rgb, g_enh);
    prev.rgb = program_fog_curve(prev.rgb, m[5].rgb, fog_amount(i.pos.w, m[4].x, m[4].y), float4(m[4].x, m[4].y, i.pos.w, 2.0));
    PROGRAM_RETURN(float4(max(prev.rgb, 0.0), saturate(prev.a)));
}
