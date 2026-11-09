// SPDX-License-Identifier: GPL-3.0-only
// The wide-table probe (NEBULA_DEV builds, GALAXY_TEST_WIDE_TABLES=1; Game/Render/NebulaWideProbe.cpp): samples the draw's map
// number `pick.x` at its middle and writes the red channel as the pixel's depth (1 - red; the depth buffer is reversed), so
// the game's depth probe reads the colour back on both back ends. A list longer than the old sixteen maps, or than a bound
// table holds, shows as a wrong value.
struct MaterialConstants {
    float4 pick;  // x: the map number
};

#define NATIVE_COORDS 1
#define NATIVE_DEPTH_OUT 1
#include "native_material.hlsli"

void material_vertex(NativeVertex v, inout NativeVaryings o) {
    o.color = float4(1.0, 1.0, 1.0, 1.0);
    o.uv[0] = float4(v.uv[0], 0.0, 0.0);
}

float4 material_color(NativeVaryings i) {
    const uint map = uint(material.pick.x);
    float4 t = float4(0.0, 0.0, 0.0, 1.0);
#if NEBULA_BOUND_TABLES
    // A device without descriptor indexing binds NEBULA_TABLE_SLOTS maps: a later one reads nothing.
    if (map < NEBULA_TABLE_SLOTS) {
        t = TEX(map).SampleLevel(SAMP(map), float2(0.5, 0.5), 0.0);
    }
#else
    t = TEX(map).SampleLevel(SAMP(map), float2(0.5, 0.5), 0.0);
#endif
    g_native_depth = 1.0 - t.r;
    return t;
}
