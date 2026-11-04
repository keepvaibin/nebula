// SPDX-License-Identifier: GPL-3.0-only
// nw4r's CharWriter (nw4r/ut/ut_CharWriter.cpp): the glyph sheet (texture 0, the first coordinate) mapped between two
// colours (lerp(C0, C1, tex); 0 and 1 without a mapping: the glyph as it is), times the vertex colour. An
// intensity font (I4/I8): the vertex colour at the glyph's alpha. A sheet of letters magnified has its alpha edge
// narrowed to one pixel of the screen, so that text is as sharp at any resolution; pictures in colour stay as they are.
struct MaterialConstants {
    float4 color_min;  // rgba, 0-1
    float4 color_max;
    float4 params;     // x: 1 an intensity font; y: 1 a sheet of letters
};

#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color = v.color0;
    o.uv[0] = float4(v.uv[0], 1.0, 0.0);
}

float4 material_color(NativeVaryings i) {
    float4 t = tex[0].Sample(samp[0], i.uv[0].xy);
    if (material.params.y != 0.0) {
        float2 sheet;
        tex[0].GetDimensions(sheet.x, sheet.y);
        const float2 step = fwidth(i.uv[0].xy * sheet);
        const float texels = max(step.x, step.y);
        if (texels < 1.0) {
            const float2 at = i.uv[0].xy * sheet - 0.5;
            const float2 whole = floor(at);
            const float2 f = at - whole;
            const float2 w0 = (1.0 - f) * (1.0 - f) * (1.0 - f) / 6.0;
            const float2 w1 = (4.0 + f * f * (3.0 * f - 6.0)) / 6.0;
            const float2 w3 = f * f * f / 6.0;
            const float2 w2 = 1.0 - w0 - w1 - w3;
            const float2 near = w0 + w1;
            const float2 far = w2 + w3;
            const float2 a = (whole + 0.5 + w1 / near - 1.0) / sheet;
            const float2 b = (whole + 0.5 + w3 / far + 1.0) / sheet;
            const float smooth_alpha =
                near.y * (near.x * tex[0].SampleLevel(samp[0], float2(a.x, a.y), 0.0).a + far.x * tex[0].SampleLevel(samp[0], float2(b.x, a.y), 0.0).a) +
                far.y * (near.x * tex[0].SampleLevel(samp[0], float2(a.x, b.y), 0.0).a + far.x * tex[0].SampleLevel(samp[0], float2(b.x, b.y), 0.0).a);
            t.a = saturate((smooth_alpha - 0.5) / max(texels, 0.0625) + 0.5);
        }
    }
    if (material.params.x != 0.0) {
        return float4(i.color.rgb, t.a * i.color.a);
    }
    return lerp(material.color_min, material.color_max, t) * i.color;
}
