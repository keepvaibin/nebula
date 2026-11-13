// SPDX-License-Identifier: GPL-3.0-only
// The upscaler's reactive mask where the added light changed (plan 16.4): rt_lights_time.hlsl puts in its moments' a
// whether the filtered light moved by more than a quarter from its history; there FSR 3, DLSS and XeSS take this
// frame's colour (0.9: as much as AMD's guide gives a mask), so a flickering or flashing light leaves no trail behind
// it. Run at the scene's end, after the blended draws wrote their alpha into the mask; elsewhere it is left alone.
#include "rt_bindings.hlsli"
#include "rt_frame.hlsli"

Texture2D<float4> g_lights_moments : register(t9);  // this frame's moments of the added light: a, the change
Texture2D<float> g_reflect_moved : register(t10);    // FB: 1 where the reflections changed (rt_reflect_time.hlsl); g_pass.w 1
RT_FORMAT(r8) RWTexture2D<unorm float> g_reactive : register(u7);  // the renderer's reactive mask

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint width, height;
    g_reactive.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) {
        return;
    }
    if (g_lights_moments.Load(int3(id.xy, 0)).a > 0.5f || (g_pass.w != 0u && g_reflect_moved.Load(int3(id.xy, 0)) > 0.5f)) {
        g_reactive[id.xy] = 0.9f;
    }
}
