// SPDX-License-Identifier: GPL-3.0-only
// TDDraw's draw mode 0 (Game/Util/DirectDraw.cpp): the vertex colour; drawn where its alpha is above a reference.
struct MaterialConstants {
    float4 color;   // unused (the layout TDDraw's materials share)
    float4 params;  // x: the alpha reference (0-255; drawn where the alpha is above it), -1 always drawn
};

#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color = v.color0;
}

float4 material_color(NativeVaryings i) {
    if (round(i.color.a * 255.0) <= material.params.x) {
        discard;
    }
    return i.color;
}
