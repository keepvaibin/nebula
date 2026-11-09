#include "program.hlsli"
cbuffer MaterialPixel : register(b2) {
    float4 m[5];
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
    const float4 ras = float4(0.0, 0.0, 0.0, 0.0);
    const float ka = m[0].a;
    const float4 r2 = m[4];
    const float a0 = saturate((0.0 + lerp(low8(r2.a), 0.0, 0.0)));
    prev = float4(saturate(t.rgb * 0.5), m[1].y > 0.0 ? m[1].y : a0);
    if (round((a0 >= 0.0 ? a0 : prev.a) * 255.0) <= m[1].w) {
        discard;
    }
    PROGRAM_RETURN(saturate(prev));
}
