// SPDX-License-Identifier: GPL-3.0-only
// One axis of a Gaussian blur (ImageEffectLocalUtil::blurTexture), two texels a bilinear tap: its width follows the
// picture, so taps never spread apart however many pixels the picture has.
struct MaterialConstants {
    float4 axis;  // xy the axis (1, 0 or 0, 1), z the width as a fraction of the picture along it, w above 0: the alpha alone, times w and clamped
};

#define NATIVE_COORDS 1
#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.uv[0] = float4(v.uv[0], 1.0, 0.0);
}

float4 material_color(NativeVaryings i) {
    uint width;
    uint height;
    TEX(0).GetDimensions(width, height);
    const float2 step = material.axis.xy / float2(width, height);
    const float sigma = max(material.axis.z / (step.x + step.y), 0.25);
    const int radius = int(ceil(3.0 * sigma));
    const float k = -0.5 / (sigma * sigma);
    const float2 uv = i.uv[0].xy;
    float4 sum = TEX(0).SampleLevel(SAMP(0), uv, 0.0);
    float weights = 1.0;
    for (int n = 1; n <= radius; n += 2) {
        const float w0 = exp(k * float(n * n));
        const float w1 = exp(k * float((n + 1) * (n + 1)));
        const float w = w0 + w1;
        const float2 offset = step * ((float(n) * w0 + float(n + 1) * w1) / w);
        sum += (TEX(0).SampleLevel(SAMP(0), uv + offset, 0.0) + TEX(0).SampleLevel(SAMP(0), uv - offset, 0.0)) * w;
        weights += 2.0 * w;
    }
    sum /= weights;
    if (material.axis.w > 0.0) {
        const float a = saturate(sum.a * material.axis.w);
        return float4(a, a, a, a);
    }
    return float4(sum.rgb, 1.0);
}
