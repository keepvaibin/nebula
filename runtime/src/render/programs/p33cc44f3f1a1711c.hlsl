#include "program.hlsli"
cbuffer MaterialPixel : register(b2) {
    float4 m[2];
};
#define NEBULA_MOTION_CURRENT_INDEX 0
#define NEBULA_MOTION_PREVIOUS_INDEX 1
struct Varyings {
    float4 pos : SV_Position;
    PER_SAMPLE float4 color : COLOR0;
    VARYINGS_MOTION
};
NativeOut program_ps(Varyings i) {
    float4 prev = float4(1.0, 1.0, 1.0, 1.0);
    {
        const float4 ras = i.color.rgba;
        const float3 rc = saturate((float3(0.0, 0.0, 0.0) + lerp(ras.rgb, float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0))));
        const float ra = saturate((0.0 + lerp(ras.a, 0.0, 0.0)));
        prev.rgb = rc;
        prev.a = ra;
        NATIVE_STAGE(0, float4(rc, ra))
    }
    prev = float4(prev.rgb, prev.a);
    prev.rgb = program_fog_curve(prev.rgb, m[1].rgb, fog_amount(i.pos.w, m[0].x, m[0].y), float4(m[0].x, m[0].y, i.pos.w, 2.0));
    PROGRAM_RETURN(saturate(prev));
}
