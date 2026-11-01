// SPDX-License-Identifier: GPL-3.0-only
// WaterCameraFilter (Game/Screen/WaterCameraFilter.cpp): under water, the screen behind (texture 1, coordinate 1)
// warped by the filter texture (texture 0, coordinate 0 turned by the filter's matrix) and darkened by the water's
// colour; alpha the filter's, drawn where above zero.
struct MaterialConstants {
    float4 water;          // the water's colour (rgb), subtracted from the screen's, and the filter's alpha (a)
    float4 filter_row[2];  // coordinate 0 -> the filter texture's s and t (the wave's turn about the middle)
    float4 warp;           // xy: the warp per indirect step, in screen texture coordinates
};

#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    const float4 c = float4(v.uv[0], 1.0, 1.0);
    o.uv[0] = float4(dot(material.filter_row[0], c), dot(material.filter_row[1], c), v.uv[1]);
}

float4 material_color(NativeVaryings i) {
    if (round(material.water.a * 255.0) <= 0.0) {
        discard;
    }
    const float2 step = round(tex[0].Sample(samp[0], i.uv[0].xy).ab * 255.0) - 128.0;
    const float3 screen = tex[1].Sample(samp[1], i.uv[0].zw + step * material.warp.xy).rgb;
    return float4(saturate(screen - material.water.rgb), material.water.a);
}
