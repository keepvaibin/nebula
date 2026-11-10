// SPDX-License-Identifier: GPL-3.0-only
// What every pass of the tracer binds, by register (rt_backend.h PassBinds; the same numbers on every back end):
//   t0       the scene: every instance of the frame, in the scene camera's eye space
//   t1-t4    buffers of the renderer's
//   t5-t8    textures of the renderer's
//   t9-t12   images of the tracer's, read
//   u0-u3    images of the tracer's, written
//   b0       the pass's constants
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
// What an instance is (rt_scene.h), read back at a hit: CommittedInstanceContributionToHitGroupIndex.
static const uint kKindCutout = 1u;

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

uint rt_hash(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

#endif
