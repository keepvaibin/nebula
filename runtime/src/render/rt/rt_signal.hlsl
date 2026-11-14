// SPDX-License-Identifier: GPL-3.0-only
// One lighting signal as a picture (--diagnostics rt-signals), in the scene target's own format so it is copied over
// the scene colour and read back as signal_<name>_<n>.ppm. A share of 0 to 1 is written as it is; a signal that goes
// over 1 (the local lights, the bounce, a mirror) as x / (1 + x), turned back on reading.
// Pass constants: g_pass.w low byte 0 the red channel, 1 rgb, 2 the alpha channel; bit 8: x / (1 + x).
#include "rt_bindings.hlsli"
#include "rt_frame.hlsli"

Texture2D<float4> g_signal : register(t9);
RT_FORMAT(unknown) RWTexture2D<float4> g_out : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint width, height;
    g_out.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) {
        return;
    }
    const float4 s = g_signal.Load(int3(id.xy, 0));
    const uint channel = g_pass.w & 255u;
    float3 x = channel == 0u ? s.rrr : channel == 2u ? s.aaa : s.rgb;
    x = max(x, 0.0f);
    x = (g_pass.w & 256u) != 0u ? x / (1.0f + x) : saturate(x);
    g_out[id.xy] = float4(x, 1.0f);
}
