// SPDX-License-Identifier: GPL-3.0-only
// What every pass of the tracer binds, by register (rt_backend.h PassBinds; the same numbers on every back end):
//   t0       the scene: every instance of the frame, in the scene camera's eye space
//   t1-t4    buffers of the renderer's
//   t5-t8    textures of the renderer's
//   t9-t20   images of the tracer's, read
//   t21-t29  the mesh memory: eight arenas, then the frame's rows
//   t0 up in space1, s0: every texture of the renderer's, and one sampler
//   u0-u7    images of the tracer's, written
//   u8       the counters; u9 a buffer of the tracer's, written
//   b0       the frame's constants (rt_frame.hlsli), or the pass's where it has its own; b1 the pass's (rt_frame.hlsli)
// A written image names its format for Vulkan (RT_FORMAT), which Direct3D 12 takes from the view.
#ifndef NEBULA_RT_BINDINGS_HLSLI
#define NEBULA_RT_BINDINGS_HLSLI

#if defined(NEBULA_VULKAN)
#define RT_FORMAT(name) [[vk::image_format(#name)]]
#else
#define RT_FORMAT(name)
#endif

// Instance masks (rt_scene.h).
static const uint kMaskOpaque = 1u;
static const uint kMaskBlended = 2u;
static const uint kMaskEarly = 4u;
static const uint kMaskSolid = 8u;
static const uint kMaskLit = 16u;
static const uint kMaskCutout = 32u;
static const uint kMaskSky = 64u;
static const uint kMaskShadowOnly = 128u;  // a draw only the tracer sees: the sun's and the lamps' shadow rays alone
// What an instance is (rt_scene.h), read back at a hit: CommittedInstanceContributionToHitGroupIndex.
static const uint kKindCutout = 1u;
static const uint kKindClipped = 2u;  // a map part its clip areas show (rt_scene.h)

// The scene camera as a pass reads it (RtView, the occlusion's constants): `ray` gives a pixel's eye ray at distance 1
// (x = px * ray.x + ray.y, y = py * ray.z + ray.w, z = -1), `depth` turns the depth target's value into eye z
// (z_scale, z_offset, min_depth, max_depth).
float3 rt_eye_ray(float4 ray, float2 pixel) {
    return float3(pixel.x * ray.x + ray.y, pixel.y * ray.z + ray.w, -1.0f);
}

float rt_eye_z(float4 depth_terms, float depth) {
    const float ndc = (depth - depth_terms.z) / max(depth_terms.w - depth_terms.z, 1e-7f);
    return -depth_terms.y / (ndc + depth_terms.x);
}

bool rt_far(float4 depth_terms, float depth) {
    return depth <= depth_terms.z + 1e-6f;
}

// A unit normal as one number a half float holds whole: 1 (a surface is there) plus ten bits of its place on the
// octahedral map (five each way: steps of about six degrees, enough to tell a floor from a wall). The filter's
// guide carries it; 0 stays "no surface".
float rt_pack_normal(float3 n) {
    const float3 a = n / max(abs(n.x) + abs(n.y) + abs(n.z), 1e-6f);
    const float2 folded = a.z >= 0.0f ? a.xy : (1.0f - abs(a.yx)) * float2(a.x >= 0.0f ? 1.0f : -1.0f, a.y >= 0.0f ? 1.0f : -1.0f);
    const uint2 q = uint2(saturate(folded * 0.5f + 0.5f) * 31.0f + 0.5f);
    return 1.0f + float(q.x + q.y * 32u);
}

float3 rt_unpack_normal(float packed) {
    const uint v = uint(packed - 0.5f);
    const float2 o = float2(float(v & 31u), float(v >> 5u & 31u)) / 31.0f * 2.0f - 1.0f;
    float3 n = float3(o, 1.0f - abs(o.x) - abs(o.y));
    const float t = saturate(-n.z);
    n.xy += float2(n.x >= 0.0f ? -t : t, n.y >= 0.0f ? -t : t);
    return normalize(n);
}

uint rt_hash(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

#endif
