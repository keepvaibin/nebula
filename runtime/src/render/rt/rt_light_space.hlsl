// SPDX-License-Identifier: GPL-3.0-only
// Compiled twice.
// As rt_light_space.hlsl: the opaque layer's Light picture, composed once a frame as the last step
// (g_pass.z 1) from the pictures the filters left: rgb the diffuse light in light units, the sun's light by its
// filtered visibility V and the surface's facing (the light pass's Facing), and I, the sky and bounce light (the
// lighting sky's mean where that pass is off); a keep, what of the base the diffuse body shows,
// (1 - metal)(1 - lobes.mirror) in luminance (rt_mirror.hlsli: the pixel's material at its n.v). A surface that shows
// itself (emissive, painted) takes 1 and keep 1: its colour as drawn. The local lights' D is the Added picture (the
// RT_LIGHTS variant's last step). With --diagnostics rt-motion the point's motion instead, with rt-sky the sky picture.
// As rt_lights_space.hlsl (RT_LIGHTS): the local lights' light (red, green, blue in r, b, a; g the frames averaged) over
// space, after the average over time (rt_light_time.hlsl): an a-trous step, 5 by 5 taps `step` pixels apart, run with
// steps 1, 2 and 4. A neighbour counts by how surely it is the same surface and the same light: the same object; in
// the pixel's own plane (its place against the pixel's normal); facing the same way; and a luminance no farther from
// the pixel's than the noise the pixel is known to have (the variance rt_lights_time.hlsl keeps, smoothed over three
// by three pixels, halved in reach by each step as the noise itself is: the weight of Schied et al. 2017). The last
// step writes red, green, blue as the programs read them.
#include "rt_bindings.hlsli"
#include "rt_frame.hlsli"

Texture2D<float4> g_guide : register(t11);  // eye distance, the object's slot in two parts, the normal packed (0: no surface)
RT_FORMAT(rgba16f) RWTexture2D<float4> g_out : register(u0);
#ifndef RT_LIGHTS
#include "rt_mirror.hlsli"
Texture2D<float4> g_facing : register(t9);       // r: how the surface faces the sun (rt_light.hlsl)
Texture2D<float4> g_material : register(t10);
Texture2D<float4> g_motion_map : register(t12);  // for --diagnostics rt-motion
Texture2D<float4> g_indirect : register(t13);    // I (rt_indirect_up.hlsl), when bit 8 of g_more.z says
Texture2D<float4> g_sun_out : register(t14);     // the sun's V, filtered (rgb)
Texture2D<float4> g_base : register(t15);
Texture2D<float4> g_normals : register(t16);
#include "rt_sky.hlsli"  // for --diagnostics rt-sky
SamplerState g_wrap : register(s0);

float3 unoct(float2 o) {
    float3 n = float3(o, 1.0f - abs(o.x) - abs(o.y));
    const float t = saturate(-n.z);
    n.xy += float2(n.x >= 0.0f ? -t : t, n.y >= 0.0f ? -t : t);
    return normalize(n);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint width, height;
    g_out.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) {
        return;
    }
    const int2 pixel = int2(id.xy);
    const int3 here = int3(pixel, 0);
    const float4 guide = g_guide.Load(here);
    const float4 material = g_material.Load(here);
    const uint bits = uint(material.b * 255.0f + 0.5f);
    if (guide.a == 0.0f || !mirror_lit(bits)) {
        g_out[id.xy] = float4(1.0f, 1.0f, 1.0f, 1.0f);
        return;
    }
    const float3 i = (g_more.z & 256u) != 0u ? max(g_indirect.Load(here).rgb, 0.0f) : g_sky_light.rgb;
    const float3 v = (g_more.z & 1u) != 0u ? g_sun_out.Load(here).rgb : rt_untraced_v(i);
    float3 light = g_sun_light.rgb * v * g_facing.Load(here).r + i;
    const float4 normals = g_normals.Load(here);
    const float3 look = normalize(rt_eye_ray(g_ray, float2(pixel) + 0.5f));
    const float3 smooth_ = unoct(normals.rg);
    const float3 n = dot(smooth_, look) < 0.0f ? smooth_ : unoct(normals.ba);
    const ShadeMaterial m = mirror_material(bits, material.r, material.g, g_base.Load(here).rgb, 0u);
    const ShadeLobes lobes = shade_lobes(m, max(-dot(n, look), kShadeNvLeast));
    float4 out_ = float4(light, (1.0f - m.metal) * (1.0f - dot(lobes.mirror, float3(0.2126f, 0.7152f, 0.0722f))));
    if ((g_more.z & 128u) != 0u) {
        out_.rgb = float3(0.0f, 0.0f, 0.0f);  // the emitters alone (--diagnostics rt-added)
    }
    if ((g_more.z & 512u) != 0u) {
        // --diagnostics rt-motion: the move across in red, down in green, eight pixels a frame full; blue where where
        // the point was is not known.
        const float4 motion = g_motion_map.Load(here);
        out_ = float4(saturate(abs(motion.x) / 8.0f), saturate(abs(motion.y) / 8.0f), motion.z > 0.0f ? 0.0f : 1.0f, 1.0f);
    }
    if ((g_more.z & 16384u) != 0u) {
        // --diagnostics rt-sky: the sky picture along the pixel's eye ray.
        out_ = float4(rt_sky_picture_by(g_wrap, look), 1.0f);
    }
    g_out[id.xy] = out_;
}
#else
Texture2D<float4> g_in : register(t9);
Texture2D<float4> g_moments : register(t10);  // b: the variance of the average's luminance
static const float3 kLuminance = float3(0.2126f, 0.7152f, 0.0722f);

// The step (b1, rt_frame.hlsli): g_pass.y pixels between taps; g_pass.z 1 the last step.

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint width, height;
    g_out.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) {
        return;
    }
    const int2 pixel = int2(id.xy);
    const float4 guide = g_guide.Load(int3(pixel, 0));
    const float4 centre = g_in.Load(int3(pixel, 0));
    const float frames = g_pass.z != 0u ? 1.0f : centre.g;
    if (guide.a == 0.0f) {
        if (rt_second_layer()) {
            return;  // the added light there is the opaque layer's
        }
        g_out[id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
    static const float kTap[3] = {6.0f / 16.0f, 4.0f / 16.0f, 1.0f / 16.0f};
    const int step_ = int(g_pass.y);
    const float3 normal = rt_unpack_normal(guide.a);
    const float3 place = rt_eye_ray(g_ray, float2(pixel) + 0.5f) * guide.r;
    // How far a neighbour may stand off the pixel's plane: a pixel's width there, and the depth's own steps.
    const float off_plane = 1.5f * abs(g_ray.x) * guide.r + 0.002f * guide.r + 0.25f;
    // The pixel's noise: its variance and its neighbours' of the same object, three by three.
    static const float kNear[2] = {0.5f, 0.25f};
    float variance = 0.0f, counted = 0.0f;
    for (int vy = -1; vy <= 1; ++vy) {
        for (int vx = -1; vx <= 1; ++vx) {
            const int2 at = clamp(pixel + int2(vx, vy), int2(0, 0), int2(width, height) - 1);
            const float4 near = g_guide.Load(int3(at, 0));
            if (near.a != 0.0f && near.g == guide.g && near.b == guide.b) {
                const float w = kNear[abs(vx)] * kNear[abs(vy)];
                variance += g_moments.Load(int3(at, 0)).b * w;
                counted += w;
            }
        }
    }
    const float unsure = sqrt(max(variance / max(counted, 1e-4f), 0.0f)) / float(g_pass.y);
    const float like = 1.0f / (4.0f * unsure + 0.004f);
    const float bright = dot(centre.rba, kLuminance);
    float3 sum = centre.rba * (kTap[0] * kTap[0]);
    float weight = kTap[0] * kTap[0];
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            if ((x == 0 && y == 0) || (g_more.z & 2048u) != 0u) {  // raw signals: the centre tap alone
                continue;
            }
            const int2 at = pixel + int2(x, y) * step_;
            if (at.x < 0 || at.y < 0 || at.x >= int(width) || at.y >= int(height)) {
                continue;
            }
            const float4 near = g_guide.Load(int3(at, 0));
            if (near.a == 0.0f || near.g != guide.g || near.b != guide.b) {
                continue;
            }
            const float3 there = rt_eye_ray(g_ray, float2(at) + 0.5f) * near.r;
            const float apart = abs(dot(normal, there - place));
            const float facing = saturate(dot(normal, rt_unpack_normal(near.a)));
            const float same = saturate(1.0f - apart / (off_plane * float(step_))) * facing * facing * facing * facing;
            if (same <= 0.0f) {
                continue;
            }
            const float3 value = g_in.Load(int3(at, 0)).rba;
            const float w = kTap[abs(x)] * kTap[abs(y)] * same * exp(-abs(dot(value, kLuminance) - bright) * like);
            sum += value * w;
            weight += w;
        }
    }
    const float3 now = sum / weight;
    g_out[id.xy] = g_pass.z != 0u ? float4(now.x, now.y, now.z, 1.0f) : float4(now.x, frames, now.y, now.z);
}
#endif
