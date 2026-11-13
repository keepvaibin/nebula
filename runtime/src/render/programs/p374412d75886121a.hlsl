#include "program.hlsli"
cbuffer MaterialPixel : register(b2) {
    float4 m[1];
};
#define NEBULA_MOTION_CURRENT_INDEX 1
#define NEBULA_MOTION_PREVIOUS_INDEX 2
struct Varyings {
    float4 pos : SV_Position;
    PER_SAMPLE float4 color : COLOR0;
    PER_SAMPLE float2 uv0 : TEXCOORD0;
    PER_SAMPLE float3 gain : TEXCOORD3;
    VARYINGS_MOTION
};
NativeOut program_ps(Varyings i) {
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    {
        const float4 t = TEX(0).Sample(SAMP(0), i.uv0).rgba;
        const float4 ras = i.color.rgba;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(float3(0.0, 0.0, 0.0), t.rgb, ras.rgb)) * 2.0);
        const float ra = saturate((0.0 + lerp(0.0, t.a, ras.a)));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(0, float4(rc, ra))
    }
    prev = float4(prev.rgb, prev.a);
    const float a8 = round(prev.a * 255.0);
    if (!((a8 >= round(m[0].x * 255.0)) && (a8 <= round(m[0].y * 255.0)))) {
        discard;
    }
#if NATIVE_TRACED
    prev.rgb = traced_map_shown(prev.rgb, i.gain);
#endif
    PROGRAM_RETURN(saturate(prev));
}
