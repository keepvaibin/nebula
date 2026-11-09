#include "program.hlsli"
cbuffer MaterialPixel : register(b2) {
    float4 m[4];
};
#define NEBULA_MOTION_CURRENT_INDEX 1
#define NEBULA_MOTION_PREVIOUS_INDEX 2
struct Varyings {
    float4 pos : SV_Position;
    PER_SAMPLE float2 uv0 : TEXCOORD0;
    VARYINGS_MOTION
};
NativeOut program_ps(Varyings i) {
    float4 prev;
    const float4 t = TEX(0).Sample(SAMP(0), i.uv0);
    const float4 ras = float4(0.0, 0.0, 0.0, 0.0);
    const float ka = 32.0 / 255.0;
    const float4 r0 = m[1];
    const float a0 = saturate((r0.a + lerp(t.a, ka, ras.a)));
    prev = float4(saturate(t.rgb * 0.5), m[0].y > 0.0 ? m[0].y : a0);
    if (round((a0 >= 0.0 ? a0 : prev.a) * 255.0) <= m[0].w) {
        discard;
    }
    PROGRAM_RETURN(saturate(prev));
}
