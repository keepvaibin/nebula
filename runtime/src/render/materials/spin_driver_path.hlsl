// SPDX-License-Identifier: GPL-3.0-only
// SpinDriverPathDrawer (Game/MapObj/SpinDriverPathDrawer.cpp): a launch star path's colour texture (0) at its alpha
// times the mask texture's (1), each through its own texture matrix of the draw (places 0 and 1); drawn where above
// zero.
struct MaterialConstants {
    float4 coord[6];  // the colour's matrix and the mask's, three rows each
};

#define NATIVE_COORDS 1
#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    const float4 c = float4(v.uv[0], 1.0, 1.0);
    o.color = float4(1.0, 1.0, 1.0, 1.0);
    o.uv[0] = float4(dot(c, material.coord[0]), dot(c, material.coord[1]),
                     dot(c, material.coord[3]), dot(c, material.coord[4]));
}

float4 material_color(NativeVaryings i) {
    const float4 color = TEX(0).Sample(SAMP(0), i.uv[0].xy);
    const float alpha = color.a * TEX(1).Sample(SAMP(1), i.uv[0].zw).a;
    if (round(alpha * 255.0) <= 0.0) {
        discard;
    }
    return float4(color.rgb, alpha);
}
