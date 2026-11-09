// SPDX-License-Identifier: GPL-3.0-only
// A flat colour (the draw's sites: AstroDomeOrbit's orbits and their bloom, fills): every pixel the material's colour.
struct MaterialConstants {
    float4 color;  // rgba, 0-1
};

#define NATIVE_COORDS 1
#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color = material.color;
}

float4 material_color(NativeVaryings i) {
    return i.color;
}
