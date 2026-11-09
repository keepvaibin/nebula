// SPDX-License-Identifier: GPL-3.0-only
// MarioActor's heat and cold water ripples (Game/Player/MarioActorSpecialDraw.cpp): the frame (texture 0), each row
// moved sideways by a wave. One draw over the screen; what moves past the screen's edge leaves the frame as it is.
struct MaterialConstants {
    float4 wave;  // x: amplitude in screen units; y: the screen's height in rows; z: the screen's width in units;
                  // w: 0 the cold wave, 1 the heat wave
    float4 time;  // x: the wave's phase; y: its change per row
};

#define NATIVE_COORDS 1
#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.uv[0] = float4(v.uv[0], 0.0, 0.0);
}

float4 material_color(NativeVaryings i) {
    const float row = i.uv[0].y * material.wave.y;
    const float pi = 3.14159265358979;
    const float offset = material.wave.w < 0.5
                             ? material.wave.x * sin(2.0 * pi * (material.time.x + row * material.time.y) / material.wave.y)
                             : material.wave.x * sin(sin(material.time.y * row) * pi + material.time.x);
    const float u = i.uv[0].x - offset / material.wave.z;
    // Opaque over the taken frame: what moves past the screen's edge shows the frame unmoved; the frame's alpha kept.
    const float4 frame = TEX(0).Sample(SAMP(0), i.uv[0].xy);
    if (u < 0.0 || u > 1.0) {
        return frame;
    }
    return float4(TEX(0).Sample(SAMP(0), float2(u, i.uv[0].y)).rgb, frame.a);
}
