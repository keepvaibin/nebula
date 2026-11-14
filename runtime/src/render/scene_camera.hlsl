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
//
// The traced relight's finish (g_finish 1: the 3D scene ray traced, the relight style on; DIRECTOR.md M7, October's
// rt_camera.hlsl and SkyStateCS): the eye between kAdaptLeast and kAdaptMost (meter_traced), Khronos PBR Neutral's
// shoulder per channel from kShoulderStart, a dither into the display's levels (traced_camera). With g_finish 0
// everything else here runs as it did, statement for statement.

cbuffer CameraConstants : register(b0) {
    float2 g_size;     // the picture's pixels
    float g_seconds;   // since the last meter; below zero, the eye starts open
    float g_finish;    // 1: the scene is traced and relit, the finish below; 2: the same where no sun is traced
                       // (the eye may open, l95); 0: the camera without it
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

// The traced relight's finish (g_finish 1).
static const float kShoulderStart = 0.76f;      // linear light from which the shoulder (per channel) bends; below it unchanged
static const float kAdaptLeast = 0.5f;          // the eye's least exposure (closes one stop for a bright view)
// Its most: 1.0, the game's own exposure: the meter cannot tell a sunlit view of dark ground from a
// dim one, and a sunlit view is never opened.
static const float kAdaptMost = 1.0f;
// Its most where no sun is traced (g_finish 2, l95; PLANS-UNBUILT "Sunless places", FA's (b)): a room the art lights
// dimly is metered open, up to one stop, as October's eye did.
static const float kAdaptMostSunless = 2.0f;
static const float kAdaptReference = 0.2f;      // the surfaces' mean light (the art's colour at full light is 1) a view is exposed for
static const float kAdaptBright = 0.1f;         // the share of the surfaces, brightest first, that must not show past white
static const float kAdaptSurfaces = 50.0f;      // a view is metered when its surfaces' weight times this passes the grid's cells
static const float kEaseShare = 0.02f;          // the share of the way to its target the eye goes in a frame ...
static const float kEaseRate = 60.0f;           // ... at this many frames a second (any rate eases alike)
static const float kLightLeast = 1.0f / 256.0f; // the metered light is kept between this ...
static const float kLightMost = 16.0f;          // ... and this (full light = 1)
static const uint kAdaptBuckets = 16u;          // the brightness histogram's buckets ...
static const float kAdaptBucketStops = 0.75f;   // ... each this many stops wide ...
static const float kAdaptFirstStop = -8.0f;     // ... from log2 of kLightLeast
static const float2 kCentre = float2(0.5f, 0.55f);      // where the meter looks (the player), of the picture
static const float2 kCentreHalf = float2(0.2f, 0.25f);  // the half sizes of the box it counts more
static const uint kCentreWeight = 4u;           // how many times more the box counts
static const float kDitherLevels = 1023.0f;     // the target's levels (RGB10A2), into which the picture is dithered
static const float3 kLuma = float3(0.2126f, 0.7152f, 0.0722f);  // Rec. 709 luminance of linear light

// The sRGB transfer the traced programs encode the scene with (program.hlsli, c39), both ways.
float3 srgb_linear(float3 c) {
    c = max(c, 0.0f);
    return lerp(c / 12.92f, pow((c + 0.055f) / 1.055f, 2.4f), step(0.04045f, c));
}
float3 srgb_encode(float3 c) {
    c = max(c, 0.0f);
    return lerp(c * 12.92f, 1.055f * pow(c, 1.0f / 2.4f) - 0.055f, step(0.0031308f, c));
}

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

// October's eye (SkyStateCS, GlowBaseCS) on this grid: the surfaces' (depth shows no sky) linear luminance, the
// picture's middle counting kCentreWeight times, its geometric mean against kAdaptReference, and no further open than
// shows the brightest kAdaptBright of them at white; the exposure eased in log space; a view with too few surfaces holds.
void meter_traced() {
    uint histogram[kAdaptBuckets];
    [unroll] for (uint bucket_at = 0u; bucket_at < kAdaptBuckets; ++bucket_at) {
        histogram[bucket_at] = 0u;
    }
    float logs = 0.0f;
    uint weights = 0u;
    for (uint row = 0u; row < kMeterRows; ++row) {
        for (uint column = 0u; column < kMeterColumns; ++column) {
            const float2 place = (float2(column, row) + 0.5f) / float2(kMeterColumns, kMeterRows) * g_size;
            if (t_depth.Load(int3(int2(place), 0)) > 1e-5f) {
                const float3 wide = t_wide.Load(int3(int2(place), 0)).rgb;
                const float light = clamp(dot(srgb_linear(wide), kLuma), kLightLeast, kLightMost);
                const uint weight = all(abs(place / g_size - kCentre) < kCentreHalf) ? kCentreWeight : 1u;
                logs += log2(light) * float(weight);
                weights += weight;
                const uint bucket = min(uint(max((log2(light) - kAdaptFirstStop) / kAdaptBucketStops, 0.0f)), kAdaptBuckets - 1u);
                histogram[bucket] += weight;
            }
        }
    }
    const float most = g_finish > 1.5f ? kAdaptMostSunless : kAdaptMost;
    const float now = u_exposure[uint2(0u, 0u)];
    float target = now > 0.0f ? now : 1.0f;
    if (float(weights) * kAdaptSurfaces > float(kMeterColumns * kMeterRows)) {
        const float mean = exp2(logs / float(weights));
        // The light at the brightest tenth: within the bucket it falls in, as far down as its share of the count.
        const float wanted = kAdaptBright * float(weights);
        float bright = 0.0f;
        uint above = 0u;
        [loop] for (uint k = kAdaptBuckets; k > 0u; --k) {
            const uint count = histogram[k - 1u];
            if (bright == 0.0f && count != 0u && float(above + count) >= wanted) {
                const float into = saturate((wanted - float(above)) / float(count));
                bright = exp2(kAdaptFirstStop + kAdaptBucketStops * (float(k) - into));
            }
            above += count;
        }
        const float highlight = bright > 0.0f ? 1.0f / max(bright, 1e-4f) : most;
        target = clamp(min(max(kAdaptReference / max(mean, 1e-4f), 1.0f), highlight), kAdaptLeast, most);
    }
    // After a load or a pause, and with no eye yet, it starts at its target.
    float adapted = target;
    if (g_seconds >= 0.0f && now > 0.0f) {
        adapted = now * pow(abs(target / now), 1.0f - pow(1.0f - kEaseShare, min(g_seconds, 1.0f) * kEaseRate));
    }
    adapted = clamp(adapted, kAdaptLeast, most);
    u_exposure[uint2(0u, 0u)] = adapted;
    u_exposure[uint2(1u, 0u)] = pow(abs(adapted), 1.0f / 2.2f);
}

// Light past white at a grid point; nothing for the sky.
float light_at(uint column, uint row) {
    const float2 place = (float2(column, row) + 0.5f) / float2(kMeterColumns, kMeterRows) * g_size;
    float light = 0.0f;
    if (t_depth.Load(int3(int2(place), 0)) > 1e-5f) {
        const float3 wide = t_wide.Load(int3(int2(place), 0)).rgb;
        light = max(pow(max(max(wide.r, wide.g), max(wide.b, 0.0f)), 2.2f) - 1.0f, 0.0f);
    }
    return light;
}

// One thread per two grid points (columns c and c + 8 of a row): 72 threads, within every Vulkan device's 128
// invocations a group, the grid's loads in flight together rather than one after another; the first thread sums the
// points in the grid's row order, as one thread looping over them would.
groupshared float gs_over[kMeterRows * kMeterColumns];

[numthreads(8, 9, 1)]
void MeterCS(uint3 cell : SV_GroupThreadID) {
    // The traced finish meters on one thread; the whole group leaves here (g_finish is a constant: the branch is uniform).
    [branch] if (g_finish > 0.5f) {
        if (cell.x == 0u && cell.y == 0u) {
            meter_traced();
        }
        return;
    }
    const uint column = cell.x;
    const uint row = cell.y;
    gs_over[row * kMeterColumns + column] = light_at(column, row);
    gs_over[row * kMeterColumns + column + kMeterColumns / 2u] = light_at(column + kMeterColumns / 2u, row);
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

// Khronos PBR Neutral's shoulder (no toe: the game's darks stay as drawn; FB b100, EVERYTHING 8.2): on the peak
// channel, unchanged below kShoulderStart, then 1 - d^2 / (peak + d - start), which meets the line there with its
// slope; every channel by the same factor, so the hue holds (a yellow planet stays yellow); then toward white by what
// the shoulder took away, kNeutralDesaturation (Khronos's 0.15).
static const float kNeutralDesaturation = 0.15f;
float3 traced_shoulder(float3 c) {
    const float peak = max(c.r, max(c.g, c.b));
    if (peak < kShoulderStart) {
        return c;
    }
    const float d = 1.0f - kShoulderStart;
    const float bent = 1.0f - d * d / (peak + d - kShoulderStart);
    const float3 held = c * (bent / peak);
    const float g = 1.0f - 1.0f / (kNeutralDesaturation * (peak - bent) + 1.0f);
    return lerp(held, float3(bent, bent, bent), g);
}

// The traced relight's picture: the eye's exposure, the shoulder, the display's levels with a dither (interleaved
// gradient noise, Jimenez 2014; stochastic rounding) so that the shoulder's gradients come out smooth, not banded.
float4 traced_camera(float4 wide, float2 pixel) {
    const float exposure = t_exposure.Load(int3(0, 0, 0));
    const float3 shown = traced_shoulder(srgb_linear(wide.rgb) * exposure);
    const float noise = frac(52.9829189f * frac(dot(floor(pixel), float2(0.06711056f, 0.00583715f))));
    const float3 level = floor(saturate(srgb_encode(shown)) * kDitherLevels + noise);
    return float4(level / kDitherLevels, wide.a);
}

float4 plain_camera(float4 wide) {
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

float4 camera(float4 wide, float2 pixel) {
    [branch] if (g_finish > 0.5f) {
        return traced_camera(wide, pixel);
    } else {
        return plain_camera(wide);
    }
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
    const float4 mapped = round(saturate(camera(first, position.xy)) * levels) / levels;
    float4 sum = mapped;
    [loop] for (uint sample = 1u; sample < samples; ++sample) {
        const float4 value = t_wide.Load(at, sample);
        [branch] if (all(value == first)) {
            sum += mapped;
        } else {
            sum += round(saturate(camera(value, position.xy)) * levels) / levels;
        }
    }
    return sum / float(samples);
}
#elif defined(CAMERA_SAMPLES)
float4 CameraPS(float4 position : SV_Position, uint index : SV_SampleIndex) : SV_Target {
    return camera(t_wide.Load(int2(position.xy), index), position.xy);
}
#else
float4 CameraPS(float4 position : SV_Position) : SV_Target {
    return camera(t_wide.Load(int3(int2(position.xy), 0)), position.xy);
}

// The scene's begin: what the scene target holds, into the wide target, unchanged (every sample alike).
float4 WidenPS(float4 position : SV_Position) : SV_Target {
    return t_wide.Load(int3(int2(position.xy), 0));
}
#endif
#endif
