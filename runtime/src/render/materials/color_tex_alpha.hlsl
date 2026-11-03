// SPDX-License-Identifier: GPL-3.0-only
// A colour at a texture's alpha (texture 0, the first coordinate): StarPointerBlur's trail, SnowFloor's layers,
// CollisionShadow's shadow; drawn where the alpha passes a test against a reference.
struct MaterialConstants {
    float4 color;   // rgb
    float4 params;  // x: the alpha reference (0-255, -1 with test 0: always drawn), y: the test (0 above the
                    // reference, 1 equal to it, 2 not equal)
};

#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color = float4(material.color.rgb, 1.0);
    o.uv[0] = float4(v.uv[0], 1.0, 0.0);
}

float4 material_color(NativeVaryings i) {
    const float a = tex[0].Sample(samp[0], i.uv[0].xy).a;
    const float a8 = round(a * 255.0);
    const uint test = uint(material.params.y);
    const bool shown = test == 1u ? a8 == material.params.x : test == 2u ? a8 != material.params.x : a8 > material.params.x;
    if (!shown) {
        discard;
    }
    return float4(i.color.rgb, a);
}
