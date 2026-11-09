// SPDX-License-Identifier: GPL-3.0-only
// TDDraw's draw mode 2 (Game/Util/DirectDraw.cpp): texture 0's colour plus a colour, its alpha times the colour's;
// drawn where the alpha is above a reference.
struct MaterialConstants {
    float4 color;   // rgba, 0-1
    float4 params;  // x: the alpha reference (0-255; drawn where the alpha is above it), -1 always drawn
};

#define NATIVE_COORDS 1
#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color = v.color0;
    o.uv[0] = float4(v.uv[0], 1.0, 0.0);
}

float4 material_color(NativeVaryings i) {
    const float4 t = TEX(0).Sample(SAMP(0), i.uv[0].xy);
    const float4 c = float4(saturate(t.rgb + material.color.rgb), t.a * material.color.a);
    if (round(c.a * 255.0) <= material.params.x) {
        discard;
    }
    return c;
}
