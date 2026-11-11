// SPDX-License-Identifier: GPL-3.0-only
// The tracer's scene, drawn (--diagnostics rt-view, rt-check): a camera ray a pixel through the scene (t0) into the
// view image (u0), which the renderer copies over the scene's colour.
//   mode 0  each instance in a colour of its own, its triangles in shades of it
//   mode 1  the ray (through what was drawn before the opaque end) against the depth the scene drew by then (t5),
//           counted (u4) and coloured by class:
//           1 grey     neither drew nor met anything (sky)
//           2 green    the ray ends on the surface drawn there
//           3 blue     drawn, and the ray met nothing: a surface the scene does not hold
//           4 orange   the ray ends behind what was drawn: the nearer surface is not in the scene
//           5 red      the ray ends in front of what was drawn: an instance where the scene drew none
//           6 magenta  the ray met a surface where nothing was drawn
//           7 purple   as 5 or 6, on a surface whose pixels an alpha test may discard (it is traced whole so far)
//           counter 0 is every pixel of the viewport.
#include "rt_bindings.hlsli"

RaytracingAccelerationStructure g_scene : register(t0);
Texture2D<float> g_depth_map : register(t5);
RT_FORMAT(unknown) RWTexture2D<float4> g_view : register(u0);
RWStructuredBuffer<uint> g_counters : register(u6);

cbuffer ViewConstants : register(b0) {
    float4 g_ray;       // rt_eye_ray
    float4 g_depth;     // rt_eye_z
    float4 g_viewport;  // the scene's viewport in target pixels: x, y, width, height
    uint4 g_mode;       // x: the mode; y: the instance mask the ray sees
};

static const float3 kClassColor[8] = {
    float3(0.0f, 0.0f, 0.0f),  float3(0.25f, 0.25f, 0.25f), float3(0.1f, 0.8f, 0.2f), float3(0.1f, 0.3f, 1.0f),
    float3(1.0f, 0.55f, 0.0f), float3(1.0f, 0.05f, 0.05f),  float3(1.0f, 0.0f, 1.0f), float3(0.45f, 0.1f, 0.6f),
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint width, height;
    g_view.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) {
        return;
    }
    const float2 pixel = float2(id.xy) + 0.5f;
    if (pixel.x < g_viewport.x || pixel.y < g_viewport.y || pixel.x >= g_viewport.x + g_viewport.z ||
        pixel.y >= g_viewport.y + g_viewport.w) {
        g_view[id.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }
    // The ray's direction has z = -1, so its length along the ray is the distance in front of the eye.
    RayDesc ray;
    ray.Origin = float3(0.0f, 0.0f, 0.0f);
    ray.Direction = rt_eye_ray(g_ray, pixel);
    ray.TMin = 0.0f;
    ray.TMax = 1.0e30f;
    // The faces the scene's draws show (rt_scene.h Faces).
    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_CULL_BACK_FACING_TRIANGLES> query;
    query.TraceRayInline(g_scene, RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_CULL_BACK_FACING_TRIANGLES, g_mode.y & 0xFFu, ray);
    query.Proceed();
    const bool met = query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    const float shade = met ? 0.7f + 0.3f * float(rt_hash(query.CommittedPrimitiveIndex()) & 255u) / 255.0f : 1.0f;
    if (g_mode.x == 0u) {
        float3 colour = float3(0.02f, 0.02f, 0.05f);
        if (met) {
            const uint h = rt_hash(query.CommittedInstanceID() * 2654435761u + 17u);
            colour = (float3(h & 255u, (h >> 8u) & 255u, (h >> 16u) & 255u) / 255.0f * 0.75f + 0.25f) * shade;
            colour *= query.CommittedTriangleFrontFace() ? 1.0f : 0.6f;
        }
        g_view[id.xy] = float4(colour, 1.0f);
        return;
    }
    const float depth = g_depth_map.Load(int3(id.xy, 0));
    const bool drawn = !rt_far(g_depth, depth);
    const float drawn_at = -rt_eye_z(g_depth, depth);
    const float met_at = met ? query.CommittedRayT() : 0.0f;
    const float slack = max(0.005f * drawn_at, 1.0f);
    uint kind = 1u;
    if (drawn && met) {
        kind = abs(met_at - drawn_at) <= slack ? 2u : met_at > drawn_at ? 4u : 5u;
    } else if (drawn) {
        kind = 3u;
    } else if (met) {
        kind = 6u;
    }
    if ((kind == 5u || kind == 6u) && (query.CommittedInstanceContributionToHitGroupIndex() & kKindCutout) != 0u) {
        kind = 7u;
    }
    InterlockedAdd(g_counters[0], 1u);
    InterlockedAdd(g_counters[kind], 1u);
    g_view[id.xy] = float4(kClassColor[kind] * shade, 1.0f);
}
