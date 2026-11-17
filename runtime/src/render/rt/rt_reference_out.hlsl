// SPDX-License-Identifier: GPL-3.0-only
// The reference's means (rt_reference.hlsl) as the pictures the programs read, in the real-time passes' own encodings
// (rt_light_space.hlsl, rt_lights_space.hlsl), so a reference picture differs from a preset's only by its signals.
// Runs after the filters, over what they wrote.
//   Light: rgb the sun's light by V and the facing (D's a) and I; a keep (rt_light_space.hlsl's). With no traced sun
//     V from I (rt_untraced_v); the emitters alone (g_more.z bit 7) none.
//   Added: rgb D.   Mirror: not written here (rt_reflect_out.hlsl composites the reference's R; with the reflections
//     part off, the light pass's from I).
//   Indirect: rgb I.
#include "rt_bindings.hlsli"
#include "rt_frame.hlsli"
#include "rt_mirror.hlsli"

Texture2D<float4> g_v : register(t9);
Texture2D<float4> g_d : register(t10);
Texture2D<float4> g_i : register(t11);
Texture2D<float4> g_r : register(t12);
Texture2D<float4> g_guide : register(t13);
Texture2D<float4> g_material : register(t14);
Texture2D<float4> g_base : register(t15);
Texture2D<float4> g_normals : register(t16);
RT_FORMAT(rgba16f) RWTexture2D<float4> g_light : register(u0);
RT_FORMAT(rgba16f) RWTexture2D<float4> g_added : register(u1);
RT_FORMAT(rgba16f) RWTexture2D<float4> g_mirror : register(u2);
RT_FORMAT(rgba16f) RWTexture2D<float4> g_indirect : register(u3);

float3 unoct(float2 o) {
    float3 n = float3(o, 1.0f - abs(o.x) - abs(o.y));
    const float t = saturate(-n.z);
    n.xy += float2(n.x >= 0.0f ? -t : t, n.y >= 0.0f ? -t : t);
    return normalize(n);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint width, height;
    g_light.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) {
        return;
    }
    const int3 here = int3(id.xy, 0);
    const float4 v = g_v.Load(here), d = g_d.Load(here), i = g_i.Load(here);
    const float4 material = g_material.Load(here);
    const uint bits = uint(material.b * 255.0f + 0.5f);
    if (i.a == 0.0f || v.a == 0.0f || !mirror_lit(bits)) {
        g_light[id.xy] = float4(1.0f, 1.0f, 1.0f, 1.0f);
        g_added[id.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        g_mirror[id.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        g_indirect[id.xy] = float4(g_sky_light.rgb, 1.0f);
        return;
    }
    const float3 light_i = max(i.rgb, 0.0f);
    const float3 sun_v = (g_more.z & 1u) != 0u ? v.rgb : rt_untraced_v(light_i);
    float3 light = g_sun_light.rgb * sun_v * saturate(d.a) + light_i;
    const float4 normals = g_normals.Load(here);
    const float3 look = normalize(rt_eye_ray(g_ray, float2(id.xy) + 0.5f));
    const float3 smooth_ = unoct(normals.rg);
    const float3 n = dot(smooth_, look) < 0.0f ? smooth_ : unoct(normals.ba);
    light += rt_fill(n, rt_untraced_v(light_i).r);
    const ShadeMaterial m = mirror_material(bits, material.r, material.g, g_base.Load(here).rgb, 0u);
    const ShadeLobes lobes = shade_lobes(m, max(-dot(n, look), kShadeNvLeast));
    float3 added = min(d.rgb, 32.0f);
    if ((g_more.z & 128u) != 0u) {
        light = float3(0.0f, 0.0f, 0.0f);
    }
    g_light[id.xy] = float4(light, (1.0f - m.metal) * (1.0f - dot(lobes.mirror, float3(0.2126f, 0.7152f, 0.0722f))));
    g_added[id.xy] = float4(added, 1.0f);
    g_indirect[id.xy] = float4(light_i, 1.0f);
}
