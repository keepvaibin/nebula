#include "program.hlsli"
cbuffer MaterialPixel : register(b2) {
    float4 m[2];
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
    prev = float4(t.rgb, 0.25);
    if (round(prev.a * 255.0) <= m[1].w) {
        discard;
    }
    PROGRAM_RETURN(saturate(prev));
}
