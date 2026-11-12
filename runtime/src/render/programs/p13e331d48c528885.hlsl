#include "program.hlsli"
#include "program_lit.hlsli"
cbuffer MaterialPixel : register(b2) {
    float4 m[7];
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
    PER_SAMPLE float3 uv2 : TEXCOORD2;
    VARYINGS_MOTION
};
NativeOut program_ps(Varyings i) {
    g_enh = enh_begin(i.lit_eye, i.lit_nrm);
    float4 lit0 = float4(0.0, 0.0, 0.0, 0.0);
    float4 lit1 = float4(0.0, 0.0, 0.0, 0.0);
    if (enh_on()) {
        lit0.rgb = i.color.rgb;
        lit0.a = m[5].a * saturate((m[6].a * 255.0 + light_term(i.lit_light, 2u, 3u, 1u, i.lit_eye, g_enh.N).a) / 255.0);
    } else {
        lit0.rgb = i.color.rgb;
        lit0.a = m[5].a * saturate((AMBIENT_TERM(m[6].a) * 255.0 + light_term(i.lit_light, 2u, 3u, 1u, i.lit_eye, g_enh.N).a) / 255.0);
    }
    i.color = lit0;
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    float4 r0 = m[0];
    float4 r1 = m[1];
    {
        const float4 t = TEX(0).Sample(SAMP(0), i.uv0).rgba;
        const float4 ras = i.color.rgba;
        const float ka = 32.0 / 255.0;
        const float3 rc = saturate((r0.rgb + lerp(float3(0.0, 0.0, 0.0), low8(r0.rgb), ras.rgb)));
        const float ra = saturate((r0.a + lerp(t.a, ka, ras.a)) * 4.0);
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(0, float4(rc, ra))
    }
    {
        const float4 ras = i.color.rgba;
        const float ka = 1.0;
        const float3 rc = saturate((prev.rgb + lerp(float3(0.0, 0.0, 0.0), ras.rgb, prev.aaa)));
        const float ra = (0.0 + lerp(0.0, ka, ka));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(1, float4(rc, ra))
    }
    {
        const float4 t = TEX(1).Sample(SAMP(1), i.uv1).rgba;
        const float3 rc = saturate((prev.rgb + lerp(low8(r1.rgb), t.rgb, float3(1.0, 1.0, 1.0))));
        const float ra = (0.0 + lerp(prev.a, 0.0, 0.0));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(2, float4(rc, ra))
    }
    {
        const float4 t = TEX(2).Sample(SAMP(2), (i.uv2.z != 0.0 ? i.uv2.xy / i.uv2.z : i.uv2.xy)).rgba;
        const float ka = m[2].a;
        const float3 rc = saturate((prev.rgb + lerp(float3(0.0, 0.0, 0.0), low8(r1.aaa), t.rgb)));
        const float ra = (0.0 + lerp(0.0, ka, ka)) * 0.5;
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(3, float4(rc, ra))
    }
    prev = float4(prev.rgb, prev.a);
    if (enh_on()) prev.rgb = enh_finish(prev.rgb, g_enh);
    prev.rgb = program_fog(prev.rgb, m[4].rgb, fog_amount(i.pos.w, m[3].x, m[3].y));
    PROGRAM_RETURN(float4(max(prev.rgb, 0.0), saturate(prev.a)));
}
