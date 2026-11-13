#include "program.hlsli"
cbuffer MaterialPixel : register(b2) {
    float4 m[3];
};
#define NEBULA_MOTION_CURRENT_INDEX 2
#define NEBULA_MOTION_PREVIOUS_INDEX 3
struct Varyings {
    float4 pos : SV_Position;
    PER_SAMPLE float4 color : COLOR0;
    PER_SAMPLE float2 uv0 : TEXCOORD0;
    PER_SAMPLE float2 uv1 : TEXCOORD1;
    PER_SAMPLE float3 gain : TEXCOORD4;
    VARYINGS_MOTION
};
NativeOut program_ps(Varyings i) {
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    float4 r1 = m[0];
    {
        const float4 t = TEX(0).Sample(SAMP(0), i.uv0).rgba;
        const float4 ras = i.color.rgba;
        const float ka = m[1].a;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(t.rgb, float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0)) - 128.0 / 255.0) * 4.0);
        const float ra = saturate((0.0 + lerp(ka, 0.0, ras.a)) * 2.0);
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(0, float4(rc, ra))
    }
    {
        const float4 t = ENV_FETCH(TEX(1).Sample(SAMP(1), i.uv1).rgba);
        const float3 rc = saturate((prev.aaa + lerp(low8(r1.rgb), t.rgb, prev.rgb)) * 2.0);
        const float ra = saturate((prev.a + lerp(0.0, 0.0, 0.0)));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(1, float4(rc, ra))
    }
    {
        const float3 k = m[2].aaa;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(float3(0.0, 0.0, 0.0), k.rgb, prev.rgb)));
        const float ra = (prev.a + lerp(0.0, 0.0, 0.0));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(2, float4(rc, ra))
    }
    prev = float4(prev.rgb, prev.a);
#if NATIVE_TRACED
    prev.rgb = traced_map_shown(prev.rgb, i.gain);
#endif
    PROGRAM_RETURN(saturate(prev));
}
