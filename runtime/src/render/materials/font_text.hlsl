// SPDX-License-Identifier: GPL-3.0-only
// nw4r's CharWriter (nw4r/ut/ut_CharWriter.cpp): the glyph sheet (texture 0, the first coordinate) mapped between two
// colours (lerp(C0, C1, tex); 0 and 1 without a mapping: the glyph as it is), times the vertex colour. An
// intensity font (I4/I8): the vertex colour at the glyph's alpha. A sheet of letters is a signed distance field
// (nebula-import cook::distance; one field, or the fill's in rgb and the outline's in alpha), the edge at one half and
// params.y texels from end to end: each channel becomes coverage over one screen pixel at any size. Pictures in
// colour (params.y 0) stay as they are.
struct MaterialConstants {
    float4 color_min;  // rgba, 0-1
    float4 color_max;
    float4 params;     // x: 1 an intensity font; y: the sheet's distance range in texels, 0 a picture
};

#define NATIVE_COORDS 1
#include "native_material.hlsli"

#if !NATIVE_OWN_VERTEX
void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color = v.color0;
    o.uv[0] = float4(v.uv[0], 1.0, 0.0);
}
#endif

float4 material_color(NativeVaryings i) {
    float4 t = TEX(0).Sample(SAMP(0), i.uv[0].xy);
    if (material.params.y != 0.0) {
        // Distance in texels, then in screen pixels (texels a pixel along the sheet's steeper axis), as coverage.
        float2 sheet;
        TEX(0).GetDimensions(sheet.x, sheet.y);
        const float2 step = fwidth(i.uv[0].xy * sheet);
        const float texels = max(max(step.x, step.y), 0.0625);
        t = saturate((t - 0.5) * (material.params.y / texels) + 0.5);
    }
    if (material.params.x != 0.0) {
        return float4(i.color.rgb, t.a * i.color.a);
    }
    return lerp(material.color_min, material.color_max, t) * i.color;
}
