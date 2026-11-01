// SPDX-License-Identifier: GPL-3.0-only
// TDDraw's draw mode 1 (Game/Util/DirectDraw.cpp): texture 0 at the first coordinate as it is; drawn where its alpha
// is above a reference.
struct MaterialConstants {
    float4 color;   // unused (the layout TDDraw's materials share)
    float4 params;  // x: the alpha reference (0-255; drawn where the alpha is above it), -1 always drawn
};

#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color = v.color0;
    o.uv[0] = float4(v.uv[0], 1.0, 0.0);
}

float4 material_color(NativeVaryings i) {
    const float4 t = tex[0].Sample(samp[0], i.uv[0].xy);
    if (round(t.a * 255.0) <= material.params.x) {
        discard;
    }
    return t;
}
