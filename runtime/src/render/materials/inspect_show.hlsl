// SPDX-License-Identifier: GPL-3.0-only
// The inspector's picture of a value (NEBULA_DEV builds, GALAXY_INSPECT_SHOW): draw_flags >> 8 picks it. 1 the normal in eye space,
// 2 the depth, 3 + n coordinate n.
struct MaterialConstants {
    float4 unused;
};

#define NATIVE_COORDS 8
#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color = float4(v.eye_nrm * 0.5 + 0.5, 1.0);
    [unroll] for (uint n = 0u; n < 8u; ++n) {
        o.uv[n] = float4(v.uv[n], 0.0, 0.0);
    }
}

float4 material_color(NativeVaryings i) {
    const uint show = draw_flags >> 8u;
    if (show == 1u) {
        return i.color;
    }
    if (show == 2u) {
        return float4(i.pos.zzz, 1.0);
    }
    if (show >= 3u && show < 11u) {
        return float4(frac(i.uv[show - 3u].xy), 0.0, 1.0);
    }
    return float4(1.0, 0.0, 1.0, 1.0);
}
