// SPDX-License-Identifier: GPL-3.0-only
// The opaque layer's specular picture the programs read (program.hlsli traced_colour: colour = base x
// keep x (Light + D) + this): rgb the surface's specular radiance, lobes.mirror x Spec + the sun's highlight, a 1.
//   Spec  where the reflections pass traced the lobe (R held: a > 0), R filtered, which already carries the local
//         lights' highlights over the specular albedo (rt_reflect.hlsl); elsewhere (a lobe past the preset's
//         roughness, Medium's pixels with no R yet) I, the sky and bounce light, the mean radiance over the surface's
//         half of the world (the limit of a rough lobe), with the local lights' highlights over it and its four
//         neighbours on the same object.
//   The sun's highlight: the shared model's GGX lobe widened by the disc (shade_highlight), times the sun's light on a
//         face square to it and its visibility V. It is the sun's mirror image: no ray adds the disc.
// lobes are the pixel's material (rt_mirror.hlsli mirror_material, from the surface images and item word 15) at its
// n.v. A surface that shows itself (emissive, painted) has no lobe: 0. Glass and water on the second layer are the light
// pass's (rt_light.hlsl).
// Pass constants: g_pass.w bit 0: R is the reference's mean, and the local lights' highlights join it here.
#include "rt_bindings.hlsli"
#include "rt_frame.hlsli"
#include "rt_mirror.hlsli"

Texture2D<float4> g_reflect : register(t9);
Texture2D<float4> g_guide : register(t10);
Texture2D<float4> g_normals : register(t11);
Texture2D<float4> g_material : register(t12);
Texture2D<float4> g_base : register(t13);
Texture2D<float4> g_lights_spec : register(t14);  // the local lights' highlights (rt_lights.hlsl), with their albedo
Texture2D<float4> g_sun_v : register(t15);        // V as the sun's filter leaves it (SunOut)
Texture2D<float4> g_indirect : register(t16);     // I (rt_indirect_up.hlsl), where g_more.z bit 8 says
RT_FORMAT(rgba16f) RWTexture2D<float4> g_mirror : register(u0);
ByteAddressBuffer g_items : register(t1);    // the item table (rt_common.hlsli's)
Texture2D<uint2> g_item_map : register(t6);  // the pixel's item, from 1

float3 unoct(float2 o) {
    float3 n = float3(o, 1.0f - abs(o.x) - abs(o.y));
    const float t = saturate(-n.z);
    n.xy += float2(n.x >= 0.0f ? -t : t, n.y >= 0.0f ? -t : t);
    return normalize(n);
}

// The local lights' highlight at the pixel, over it and its four neighbours on the same object (one light a pixel is
// noisy; where R is traced its filter takes that noise).
float3 highlight_near(int2 pixel, float4 guide, uint width, uint height) {
    float3 sum = g_lights_spec.Load(int3(pixel, 0)).rgb;
    float taps = 1.0f;
    const int2 steps[4] = {int2(1, 0), int2(-1, 0), int2(0, 1), int2(0, -1)};
    for (uint k = 0u; k < 4u; ++k) {
        const int2 at = pixel + steps[k];
        if (at.x < 0 || at.y < 0 || at.x >= int(width) || at.y >= int(height)) {
            continue;
        }
        const float4 near = g_guide.Load(int3(at, 0));
        if (near.a != 0.0f && near.g == guide.g && near.b == guide.b) {
            sum += g_lights_spec.Load(int3(at, 0)).rgb;
            taps += 1.0f;
        }
    }
    return sum / taps;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint width, height;
    g_mirror.GetDimensions(width, height);
    if (id.x >= width || id.y >= height || g_pass.x != 0u) {
        return;
    }
    const int2 pixel = int2(id.xy);
    const int3 here = int3(pixel, 0);
    const float4 guide = g_guide.Load(here);
    const float4 material = g_material.Load(here);
    const uint bits = uint(material.b * 255.0f + 0.5f);
    if (guide.a == 0.0f || !mirror_lit(bits) || (g_more.z & 128u) != 0u) {  // bit 7: the emitters alone
        g_mirror[id.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }
    const float4 normals = g_normals.Load(here);
    const float3 look = normalize(rt_eye_ray(g_ray, float2(pixel) + 0.5f));
    const float3 smooth_ = unoct(normals.rg);
    const float3 n = dot(smooth_, look) < 0.0f ? smooth_ : unoct(normals.ba);
    const uint item = g_item_map.Load(here).x;
    const uint word = item != 0u ? g_items.Load(g_at.x + (item - 1u) * kItemBytes + 60u) : 0u;
    const ShadeMaterial m = mirror_material(bits, material.r, material.g, g_base.Load(here).rgb, word);
    const ShadeLobes lobes = shade_lobes(m, max(-dot(n, look), kShadeNvLeast));
    const float3 i = (g_more.z & 256u) != 0u ? max(g_indirect.Load(here).rgb, 0.0f) : g_sky_light.rgb;
    const float3 v = (g_more.z & 1u) != 0u ? g_sun_v.Load(here).rgb : rt_untraced_v(i);
    const float3 sun = shade_highlight(m, lobes, n, -look, g_sun.xyz, g_sun.w) * g_sun_light.rgb * v;
    float4 r = g_reflect.Load(here);
    float3 spec;
    if (r.a > 0.0f) {
        if ((g_pass.w & 1u) != 0u) {
            r.rgb += g_lights_spec.Load(here).rgb / max(lobes.mirror, 0.02f);
        }
        spec = lobes.mirror * r.rgb;
    } else {
        spec = lobes.mirror * i + highlight_near(pixel, guide, width, height);
    }
    g_mirror[id.xy] = float4(spec + sun, 1.0f);
}
