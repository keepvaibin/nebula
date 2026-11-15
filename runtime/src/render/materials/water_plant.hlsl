// SPDX-License-Identifier: GPL-3.0-only
// WaterPlant (Game/Map/WaterPlant.cpp): the plant texture (0), drawn where its alpha is above 50 (of 255), fogged
// linearly toward the water's blue by eye depth between the fog's start and end.
struct MaterialConstants {
    float4 fog;        // x: start, y: end (eye depth)
    float4 fog_color;  // rgb
};

#define NATIVE_COORDS 1
#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color = float4(1.0, 1.0, 1.0, 1.0);
    o.uv[0] = float4(v.uv[0], 1.0, 0.0);
}

float4 material_color(NativeVaryings i) {
    const float4 t = TEX(0).Sample(SAMP(0), i.uv[0].xy);
    if (round(t.a * 255.0) <= 50.0) {
        discard;
    }
    const float f = saturate((i.pos.w - material.fog.x) / max(material.fog.y - material.fog.x, 1.0e-6));
    return float4(program_fog(t.rgb, material.fog_color.rgb, f), t.a);  // a traced form fogs after its light (15.3)
}
