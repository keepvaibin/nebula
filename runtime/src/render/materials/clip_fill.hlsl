// SPDX-License-Identifier: GPL-3.0-only
// ClipFieldFillDraw (Game/MapObj/ClipFieldFillDraw.cpp): a screen fill of one colour whose alpha shows where the
// screen alpha mask (texture 0) is set (or, for the other order, where it is clear); nothing elsewhere.
struct MaterialConstants {
    float4 color;   // the fill's colour (rgba, 0-1)
    float4 params;  // x: 1 shows where the mask's alpha is above 1/255, 0 where it is below; y: 1 the mask is the taken
                    // frame and this draw replaces it: the colour blended over it where shown, alpha 0 there
};

#define NATIVE_COORDS 1
#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color = material.color;
    o.uv[0] = float4(v.uv[0], 1.0, 0.0);
}

float4 material_color(NativeVaryings i) {
    const float mask = round(TEX(0).Sample(SAMP(0), i.uv[0].xy).a * 255.0);
    const bool shown = material.params.x != 0.0 ? mask > 1.0 : mask < 1.0;
    if (material.params.y != 0.0) {
        const float4 frame = TEX(0).Sample(SAMP(0), i.uv[0].xy);
        return shown ? float4(lerp(frame.rgb, i.color.rgb, i.color.a), 0.0) : frame;
    }
    if (!shown || round(i.color.a * 255.0) <= 0.0) {
        discard;
    }
    return i.color;
}
