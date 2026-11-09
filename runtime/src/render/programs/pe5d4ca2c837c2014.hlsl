#include "program.hlsli"
cbuffer MaterialPixel : register(b2) {
    float4 m[1];
};
#define NEBULA_MOTION_CURRENT_INDEX 0
#define NEBULA_MOTION_PREVIOUS_INDEX 1
struct Varyings {
    float4 pos : SV_Position;
    PER_SAMPLE float4 color : COLOR0;
    VARYINGS_MOTION
};
NativeOut program_ps(Varyings i) {
    float4 prev;
    const float4 ras = i.color;
    prev = float4(ras.rgb, m[0].y);
    if (round(prev.a * 255.0) <= m[0].w) {
        discard;
    }
    PROGRAM_RETURN(saturate(prev));
}
