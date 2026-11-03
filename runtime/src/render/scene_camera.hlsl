// SPDX-License-Identifier: GPL-3.0-only
// The scene camera (scene_camera.h): the wide scene target back to the display at the 3D scene's end.
//
// MeterCS: reads a grid of the picture, finds how much light the world holds past white (the sky, nothing in the depth, is
// no light on it), and moves the exposure toward what that asks for by the time that passed (scene_camera_shape.h).
// CameraVS/CameraPS: every pixel (every sample of a
// multisampled scene, so an edge against a highlight blends as its parts look) under the exposure and the shoulder. The
// game's colours are display values; the shoulder works in linear light, as the enhanced lighting that goes past white.
// Numbers: scene_camera_shape.h.

cbuffer CameraConstants : register(b0) {
    float2 g_size;     // the picture's pixels
    float g_seconds;   // since the last meter; below zero, the eye starts open
    float g_pad;
};

static const float kKnee = 0.9f;
static const float kKneeDisplay = 0.9532f;  // the knee as a display value (kKnee ^ (1 / 2.2))
static const float kLeastExposure = 0.85f;
static const float kOverRange = 1.0f;
static const float kCloseSeconds = 0.35f;
static const float kOpenSeconds = 1.5f;
static const uint kMeterColumns = 16u;
static const uint kMeterRows = 9u;

#ifdef CAMERA_STORAGE_FORMAT
#define CAMERA_FORMAT(name) [[spv::format_##name]]
#else
#define CAMERA_FORMAT(name)
#endif

#ifdef CAMERA_SAMPLES
Texture2DMS<float4> t_wide : register(t0);
#else
Texture2D<float4> t_wide : register(t0);
#endif
Texture2D<float> t_exposure : register(t1);
#if defined(CAMERA_METER)
Texture2D<float> t_depth : register(t2);  // reversed: 0 at the far plane
CAMERA_FORMAT(r32f) RWTexture2D<float> u_exposure : register(u0);

[numthreads(1, 1, 1)]
void MeterCS() {
    float over = 0.0f;
    for (uint row = 0u; row < kMeterRows; ++row) {
        for (uint column = 0u; column < kMeterColumns; ++column) {
            const float2 place = (float2(column, row) + 0.5f) / float2(kMeterColumns, kMeterRows) * g_size;
            if (t_depth.Load(int3(int2(place), 0)) > 1e-5f) {
                const float3 wide = t_wide.Load(int3(int2(place), 0)).rgb;
                over += max(pow(max(max(wide.r, wide.g), max(wide.b, 0.0f)), 2.2f) - 1.0f, 0.0f);
            }
        }
    }
    over /= float(kMeterColumns * kMeterRows);
    const float target = max(1.0f / (1.0f + over / kOverRange), kLeastExposure);
    const float now = u_exposure[uint2(0u, 0u)];
    if (g_seconds < 0.0f || !(now > 0.0f)) {
        u_exposure[uint2(0u, 0u)] = 1.0f;
        return;
    }
    const float tau = target < now ? kCloseSeconds : kOpenSeconds;
    u_exposure[uint2(0u, 0u)] = now + (target - now) * (1.0f - exp(-min(g_seconds, 1.0f) / tau));
}
#else
float4 CameraVS(uint vertex : SV_VertexID) : SV_Position {
    const float2 uv = float2((vertex << 1) & 2u, vertex & 2u);
    return float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
}

float4 camera(float4 wide) {
    const float exposure = t_exposure.Load(int3(0, 0, 0));
    if (exposure >= 1.0f && all(wide.rgb <= kKneeDisplay)) {
        return wide;
    }
    const float3 x = pow(max(wide.rgb, 0.0f), 2.2f) * exposure;
    const float3 over = kKnee + (1.0f - kKnee) * (1.0f - exp((kKnee - x) / (1.0f - kKnee)));
    return float4(pow(lerp(x, over, step(kKnee, x)), 1.0f / 2.2f), wide.a);
}

#ifdef CAMERA_SAMPLES
float4 CameraPS(float4 position : SV_Position, uint index : SV_SampleIndex) : SV_Target {
    return camera(t_wide.Load(int2(position.xy), index));
}
#else
float4 CameraPS(float4 position : SV_Position) : SV_Target {
    return camera(t_wide.Load(int3(int2(position.xy), 0)));
}

// The scene's begin: what the scene target holds, into the wide target, unchanged (every sample alike).
float4 WidenPS(float4 position : SV_Position) : SV_Target {
    return t_wide.Load(int3(int2(position.xy), 0));
}
#endif
#endif
