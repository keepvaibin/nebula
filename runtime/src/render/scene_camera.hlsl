// SPDX-License-Identifier: GPL-3.0-only
// The scene camera (scene_camera.h): the wide scene target back to the display at the 3D scene's end.
//
// MeterCS: reads a grid of the picture, finds how much light the world holds past white (the sky, nothing in the depth, is
// no light on it), and moves the exposure toward what that asks for by the time that passed (kCloseSeconds, kOpenSeconds).
// CameraVS/CameraPS: every pixel (every sample of a
// multisampled scene, so an edge against a highlight blends as its parts look) under the exposure and the shoulder. The
// game's colours are display values; the shoulder works in linear light, as the enhanced lighting that goes past white.
// The wide scene target's colour is the game's own display values, with what the enhanced lighting adds past white
// left in. A value at or below the knee passes unchanged, so what the game authored stays.

cbuffer CameraConstants : register(b0) {
    float2 g_size;     // the picture's pixels
    float g_seconds;   // since the last meter; below zero, the eye starts open
    float g_pad;
};

// Linear light: where the shoulder starts (its values pass unchanged below). Authored white comes out at 0.963 linear.
static const float kKnee = 0.9f;
static const float kKneeDisplay = 0.9532f;  // the knee as a display value (kKnee ^ (1 / 2.2))
// The eye closes while the scene holds light past white, which only the enhanced lighting makes: its exposure falls
// toward this at most, and a picture of the game's own colours, however bright, leaves it open.
static const float kLeastExposure = 0.85f;
// Light past white, averaged over the picture, that takes the exposure to 1 / (1 + 1).
static const float kOverRange = 1.0f;
// The eye's time constants, seconds: it closes quickly and opens slowly; any frame rate reaches the target alike.
static const float kCloseSeconds = 0.35f;
static const float kOpenSeconds = 1.5f;
// The meter's grid over the picture.
static const uint kMeterColumns = 16u;
static const uint kMeterRows = 9u;

#ifdef CAMERA_STORAGE_FORMAT
#define CAMERA_FORMAT(name) [[spv::format_##name]]
#else
#define CAMERA_FORMAT(name)
#endif

#if defined(CAMERA_SAMPLES) || defined(CAMERA_RESOLVE)
Texture2DMS<float4> t_wide : register(t0);
#else
Texture2D<float4> t_wide : register(t0);
#endif
Texture2D<float> t_exposure : register(t1);
#if defined(CAMERA_METER)
Texture2D<float> t_depth : register(t2);  // reversed: 0 at the far plane
CAMERA_FORMAT(r32f) RWTexture2D<float> u_exposure : register(u0);

// One thread per grid point (kMeterColumns x kMeterRows), so the grid's loads are in flight together rather than one
// after another; the first thread sums the points in the grid's row order, as one thread looping over them would.
groupshared float gs_over[kMeterRows * kMeterColumns];

[numthreads(16, 9, 1)]
void MeterCS(uint3 cell : SV_GroupThreadID) {
    const uint column = cell.x;
    const uint row = cell.y;
    const float2 place = (float2(column, row) + 0.5f) / float2(kMeterColumns, kMeterRows) * g_size;
    float light = 0.0f;  // light past white here; nothing for the sky
    if (t_depth.Load(int3(int2(place), 0)) > 1e-5f) {
        const float3 wide = t_wide.Load(int3(int2(place), 0)).rgb;
        light = max(pow(max(max(wide.r, wide.g), max(wide.b, 0.0f)), 2.2f) - 1.0f, 0.0f);
    }
    gs_over[row * kMeterColumns + column] = light;
    GroupMemoryBarrierWithGroupSync();
    if (row != 0u || column != 0u) {
        return;
    }
    // Adding the sky's +0 leaves the sum as it was (every term is at least +0).
    float over = 0.0f;
    for (uint i = 0u; i < kMeterRows * kMeterColumns; ++i) {
        over += gs_over[i];
    }
    over /= float(kMeterColumns * kMeterRows);
    const float target = max(1.0f / (1.0f + over / kOverRange), kLeastExposure);
    const float now = u_exposure[uint2(0u, 0u)];
    if (g_seconds < 0.0f || !(now > 0.0f)) {
        u_exposure[uint2(0u, 0u)] = 1.0f;
        u_exposure[uint2(1u, 0u)] = 1.0f;
        return;
    }
    const float tau = target < now ? kCloseSeconds : kOpenSeconds;
    const float adapted = now + (target - now) * (1.0f - exp(-min(g_seconds, 1.0f) / tau));
    u_exposure[uint2(0u, 0u)] = adapted;
    // Below the shoulder, pow(pow(c, 2.2) * exposure, 1/2.2) = c * exposure^(1/2.2).
    // Compute the frame-uniform factor once, rather than for every pixel/MSAA sample. adapted lies between now and
    // target, both positive; abs is a free source modifier that tells the compiler so.
    u_exposure[uint2(1u, 0u)] = pow(abs(adapted), 1.0f / 2.2f);
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
    const float gain = t_exposure.Load(int3(1, 0, 0));
    const float3 below = max(wide.rgb, 0.0f) * gain;
    [branch] if (all(below <= kKneeDisplay)) {
        return float4(below, wide.a);
    }
    const float3 x = pow(max(wide.rgb, 0.0f), 2.2f) * exposure;
    const float3 over = kKnee + (1.0f - kKnee) * (1.0f - exp((kKnee - x) / (1.0f - kKnee)));
    // The lerp picks x (never negative) below the knee and over (at least kKnee) above it; abs is free and says so.
    return float4(pow(abs(lerp(x, over, step(kKnee, x))), 1.0f / 2.2f), wide.a);
}

#ifdef CAMERA_RESOLVE
// Tone-map every distinct sample before averaging: mapping an already averaged HDR edge would change
// its brightness. Interior samples normally agree, so they share the nonlinear colour calculation.
float4 CameraResolvePS(float4 position : SV_Position) : SV_Target {
    uint width, height, samples;
    t_wide.GetDimensions(width, height, samples);
    const int2 at = int2(position.xy);
    const float4 first = t_wide.Load(at, 0);
    // Match the RGB10A2 attachment's per-sample quantization before the old hardware resolve.
    const float4 levels = float4(1023.0f, 1023.0f, 1023.0f, 3.0f);
    const float4 mapped = round(saturate(camera(first)) * levels) / levels;
    float4 sum = mapped;
    [loop] for (uint sample = 1u; sample < samples; ++sample) {
        const float4 value = t_wide.Load(at, sample);
        [branch] if (all(value == first)) {
            sum += mapped;
        } else {
            sum += round(saturate(camera(value)) * levels) / levels;
        }
    }
    return sum / float(samples);
}
#elif defined(CAMERA_SAMPLES)
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
