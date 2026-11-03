// SPDX-License-Identifier: GPL-3.0-only
// nw4r's CharWriter (nw4r/ut/ut_CharWriter.cpp): the glyph sheet (texture 0, the first coordinate) mapped between two
// colours (lerp(C0, C1, tex); 0 and 1 without a mapping: the glyph as it is), times the vertex colour. An
// intensity font (I4/I8): the vertex colour at the glyph's alpha.
struct MaterialConstants {
    float4 color_min;  // rgba, 0-1
    float4 color_max;
    float4 params;     // x: 1 an intensity font
};

#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color = v.color0;
    o.uv[0] = float4(v.uv[0], 1.0, 0.0);
}

float4 material_color(NativeVaryings i) {
    const float4 t = tex[0].Sample(samp[0], i.uv[0].xy);
    if (material.params.x != 0.0) {
        return float4(i.color.rgb, t.a * i.color.a);
    }
    return lerp(material.color_min, material.color_max, t) * i.color;
}
