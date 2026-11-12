// SPDX-License-Identifier: GPL-3.0-only
// The sun filter's blur (plan 7.3), run twice (g_pass.y 0, then 1: the disc turned a quarter and half as wide): V
// spread over a disc as wide as the penumbra really is at the pixel. The sun's disc seen from the surface is
// 2 * tan(its half angle) wide a unit of way, so a shadow whose caster stands d away has a penumbra
// w = 2 * g_sun.w * d wide: sharp where the caster touches the ground, wide under a high branch (the method of
// NVIDIA's SIGMA, from its public descriptions). Eight taps on a Poisson disc laid in the surface's own plane (so it
// does not run off an edge at a grazing look), turned a pixel by the blue noise; a tap counts if it is the same
// object, within the penumbra's width of the pixel's plane and facing its way. A pixel with under four frames behind
// it takes a disc twice as wide. Outside the penumbra tiles V is copied through.
#include "rt_bindings.hlsli"
#include "rt_sample.hlsli"

Texture2D<float4> g_in : register(t9);         // rgb V, a frames / 16
Texture2D<float> g_distance : register(t10);   // the occluder's distance, averaged (0: none)
Texture2D<float4> g_guide : register(t11);
Texture2D<float4> g_normals : register(t12);   // S.normal: rg the smooth normal (rt_oct)
Texture2D<float> g_tiles : register(t14);
RT_FORMAT(rgba16f) RWTexture2D<float4> g_out : register(u0);

static const float2 kDisc[8] = {float2(-0.613f, 0.617f), float2(0.170f, -0.040f), float2(-0.299f, -0.792f), float2(0.645f, 0.493f),
                                float2(-0.651f, -0.111f), float2(0.418f, -0.766f), float2(0.034f, 0.915f), float2(0.873f, -0.137f)};

bool in_penumbra(uint2 pixel) {
    uint across, down;
    g_tiles.GetDimensions(across, down);
    const int2 tile = int2(pixel / 16u);
    float most = 0.0f;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            const int2 at = clamp(tile + int2(x, y), int2(0, 0), int2(across, down) - 1);
            most = max(most, g_tiles.Load(int3(at, 0)));
        }
    }
    return most > 0.75f;
}

float3 unoct(float2 o) {
    float3 n = float3(o, 1.0f - abs(o.x) - abs(o.y));
    const float t = saturate(-n.z);
    n.xy += float2(n.x >= 0.0f ? -t : t, n.y >= 0.0f ? -t : t);
    return normalize(n);
}

// The pixel a point of eye space falls on (rt_eye_ray undone).
float2 pixel_of(float3 q) {
    const float2 slope = q.xy / max(-q.z, 1e-4f);
    return float2((slope.x - g_ray.y) / g_ray.x, (slope.y - g_ray.w) / g_ray.z);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint width, height;
    g_out.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) {
        return;
    }
    const int2 pixel = int2(id.xy);
    const float4 centre = g_in.Load(int3(pixel, 0));
    const float4 guide = g_guide.Load(int3(pixel, 0));
    if (guide.a == 0.0f || !in_penumbra(id.xy) || (g_more.z & 2048u) != 0u) {  // raw signals (FB, 22.20): copied through
        g_out[id.xy] = centre;
        return;
    }
    const float away = guide.r;
    const float pixel_wide = abs(g_ray.x) * away;  // a pixel's width at the surface
    const float3 n = unoct(g_normals.Load(int3(pixel, 0)).rg);
    // The penumbra on the surface: 2 tan(d) x the caster's distance, over how squarely the sun meets the surface (B4;
    // a sheet lit from behind by |n.s|).
    const float penumbra = 2.0f * g_sun.w * g_distance.Load(int3(pixel, 0)) / max(abs(dot(n, g_sun.xyz)), 0.2f);
    float radius = clamp(penumbra / (2.0f * pixel_wide), 1.0f, 24.0f);
    if (centre.a < 4.0f / 16.0f) {
        radius *= 2.0f;
    }
    const bool second = g_pass.y != 0u;
    if (second) {
        radius *= 0.5f;
    }
    // The disc in the surface's plane, turned by the noise (a quarter more on the second run).
    const float3 side = normalize(abs(n.y) < 0.9f ? cross(n, float3(0.0f, 1.0f, 0.0f)) : cross(n, float3(1.0f, 0.0f, 0.0f)));
    const float3 up = cross(n, side);
    const float turn = 6.2831853f * rt_sample(id.xy, g_more.y, kSampleBlur).x + (second ? 1.5707963f : 0.0f);
    const float2 cs = float2(cos(turn), sin(turn));
    const float3 place = rt_eye_ray(g_ray, float2(pixel) + 0.5f) * away;
    const float3 normal = rt_unpack_normal(guide.a);
    const float off_plane = 0.5f * penumbra + 1.5f * pixel_wide + 0.002f * away + 0.25f;
    float3 sum = centre.rgb;
    float weight = 1.0f;
    for (uint k = 0u; k < 8u; ++k) {
        const float2 d = float2(kDisc[k].x * cs.x - kDisc[k].y * cs.y, kDisc[k].x * cs.y + kDisc[k].y * cs.x);
        const float3 q = place + (side * d.x + up * d.y) * (radius * pixel_wide);
        const int2 at = int2(floor(pixel_of(q)));
        if (at.x < 0 || at.y < 0 || at.x >= int(width) || at.y >= int(height)) {
            continue;
        }
        const float4 near = g_guide.Load(int3(at, 0));
        if (near.a == 0.0f || near.g != guide.g || near.b != guide.b) {
            continue;
        }
        const float3 there = rt_eye_ray(g_ray, float2(at) + 0.5f) * near.r;
        if (abs(dot(normal, there - place)) > off_plane || dot(normal, rt_unpack_normal(near.a)) < 0.9f) {
            continue;
        }
        const float w = exp(-2.0f * dot(kDisc[k], kDisc[k]));
        sum += g_in.Load(int3(at, 0)).rgb * w;
        weight += w;
    }
    g_out[id.xy] = float4(sum / weight, centre.a);
}
