#include "program.hlsli"
cbuffer MaterialPixel : register(b2) {
    float4 m[2];
};
#define NEBULA_MOTION_CURRENT_INDEX 0
#define NEBULA_MOTION_PREVIOUS_INDEX 1
struct Varyings {
    float4 pos : SV_Position;
    VARYINGS_MOTION
};
NativeOut program_ps(Varyings i) {
    float4 prev;
    const float4 t = float4(0.0, 0.0, 0.0, 0.0);
    prev = float4(t.rgb, 0.25);
    if (round(prev.a * 255.0) <= m[1].w) {
        discard;
    }
    PROGRAM_RETURN(saturate(prev));
}
