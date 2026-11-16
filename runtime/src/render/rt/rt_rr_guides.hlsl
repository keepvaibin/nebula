// SPDX-License-Identifier: GPL-3.0-only
// The guides an outside denoiser that takes the composited picture reads (DLSS Ray
// Reconstruction through Streamline), written only while the tracer hands its signals over raw (g_more.z bit 2048),
// from the surface pass's images of this frame, at the picture's size:
//   diffuse albedo: S.base in linear light where there is a surface, white where the sky is (it is shown as it is);
//   specular albedo: the split-sum environment term of what the surface mirrors, F0 * A + B, with A and B from Karis's
//     analytic fit ("Physically Based Shading on Mobile", 2014) of roughness and n.v: the weight the composite puts on
//     R (rt_mirror.hlsli mirror_weight_word, as rt_reflect_out.hlsl); 0 at the sky;
//   normal and roughness: the shading normal in the world (S.normal's, whole, where g_pass.w bit 1 says it is bound at
//     t13; else the guide's packed one; rows of the eye-to-world turn in g_pass_value), roughness in w (Streamline's
//     packed mode);
//   the reflection ray's length (R raw's a; 0 where nothing reflects or the reflections pass did not run: g_pass.w
//     bit 0 says it ran; 65504 where it met nothing and where no surface is, the guide's sky value).
// The albedos in linear light in a UNORM target (the DLSS-RR Integration Guide 3.4.1-3.4.2: sRGB formats unsupported).
#include "rt_bindings.hlsli"
#include "rt_frame.hlsli"
#include "rt_mirror.hlsli"  // mirror_terms: the split sum's fit, the composite's own

Texture2D<float4> g_material : register(t9);   // S.material: r roughness, b the class and its flags (bytes)
Texture2D<float4> g_base : register(t10);      // S.base
Texture2D<float4> g_guide : register(t11);     // eye distance, the object's slot, the normal packed (a 0: no surface)
Texture2D<float4> g_reflect_raw : register(t12);  // R raw: a the ray's length (0 none, -1 not traced)
Texture2D<float4> g_normals : register(t13);     // S.normal: rg the shading normal (rt_oct), where g_pass.w bit 1
RT_FORMAT(rgba8) RWTexture2D<float4> g_diffuse : register(u0);
RT_FORMAT(rgba8) RWTexture2D<float4> g_specular : register(u1);
RT_FORMAT(rgba16f) RWTexture2D<float4> g_normal_roughness : register(u2);
RT_FORMAT(r16f) RWTexture2D<float> g_hit : register(u3);
ByteAddressBuffer g_items : register(t1);      // the item table (rt_common.hlsli's): word 15 bits 12-19 the index
Texture2D<uint2> g_item_map : register(t6);    // the pixel's item, from 1

static const uint kGloss = 1u, kGlass = 3u, kWater = 9u, kMetal = 2u;

// The specular albedo the denoiser takes (its integration guide's appendix, the preintegrated environment term of
// Ray Tracing Gems chapter 32): F0 `f0`, alpha = roughness squared, n.v `nv`.
float3 rr_specular_albedo(float3 f0, float alpha, float nv) {
    nv = abs(nv);
    const float4 x = float4(1.0f, nv, nv * nv, nv * nv * nv);
    const float4 y = float4(1.0f, alpha, alpha * alpha, alpha * alpha * alpha);
    const float2x2 m1 = float2x2(0.99044f, -1.28514f, 1.29678f, -0.755907f);
    const float3x3 m2 = float3x3(1.0f, 2.92338f, 59.4188f, 20.3225f, -27.0302f, 222.592f, 121.563f, 626.13f, 316.627f);
    const float2x2 m3 = float2x2(0.0365463f, 3.32707f, 9.0632f, -9.04756f);
    const float3x3 m4 = float3x3(1.0f, 3.59685f, -1.36772f, 9.04401f, -16.3174f, 9.22949f, 5.56589f, 19.7886f, -20.2123f);
    float bias = dot(mul(m1, x.xy), y.xy) * rcp(dot(mul(m2, x.xyw), y.xyw));
    const float scale = dot(mul(m3, x.xy), y.xy) * rcp(dot(mul(m4, x.xzw), y.xyw));
    bias *= saturate(f0.g * 50.0f);  // no bias where the reflectance is 0
    return saturate(f0 * max(scale, 0.0f) + max(bias, 0.0f));
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint width, height;
    g_diffuse.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) {
        return;
    }
    const int3 here = int3(id.xy, 0);
    const float4 guide = g_guide.Load(here);
    if (guide.a == 0.0f) {
        g_diffuse[id.xy] = float4(1.0f, 1.0f, 1.0f, 1.0f);
        g_specular[id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        g_normal_roughness[id.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        g_hit[id.xy] = kNoOccluder;  // the sky: FP16_MAX (DLSS-RR Integration Guide 3.4.9)
        return;
    }
    const float4 material = g_material.Load(here);
    const float3 base = g_base.Load(here).rgb;
    const float roughness = material.r;
    const uint klass = uint(material.b * 255.0f + 0.5f) & 15u;
    // The shading normal whole (the water's ripple too), else the guide's packed one.
    const float3 eye_normal = (g_pass.w & 2u) != 0u ? rt_unoct(g_normals.Load(here).rg) : rt_unpack_normal(guide.a);
    const float3 look = normalize(rt_eye_ray(g_ray, float2(id.xy) + 0.5f));
    const float facing = saturate(dot(eye_normal, -look));
    // The F0 the composite weighs R by (rt_reflect_out.hlsl, mirror_weight_word with the item's word 15): glass and
    // water by the item's index, a metal by its kind, a painted gloss or metal by the art's F0.
    const bool mirrors = klass == kGlass || klass == kWater || klass == kMetal || klass == kGloss;
    const uint item = mirrors ? g_item_map.Load(here).x : 0u;
    const uint word15 = item != 0u ? g_items.Load(g_at.x + (item - 1u) * kItemBytes + 60u) : 0u;
    // A class that mirrors nothing has no traced R to weigh: its sheen's F0 0.04 (mirror_f0), painted or not.
    const uint bits = uint(material.b * 255.0f + 0.5f);
    const float3 f0 = mirror_f0_word(mirrors ? bits : bits & ~kMirrorPainted, roughness, material.a, base, word15);
    const float3 world_normal = normalize(float3(dot(g_pass_value[0].xyz, eye_normal), dot(g_pass_value[1].xyz, eye_normal),
                                                 dot(g_pass_value[2].xyz, eye_normal)));
    float hit = 0.0f;
    if ((g_pass.w & 1u) != 0u) {
        hit = max(g_reflect_raw.Load(here).a, 0.0f);
    }
    // In linear light, as the specular guide and the radiance they divide (S.base holds the colour encoded).
    g_diffuse[id.xy] = float4(klass == kMetal ? float3(0.0f, 0.0f, 0.0f) : mirror_lin(base), 1.0f);
    g_specular[id.xy] = float4(rr_specular_albedo(f0, roughness * roughness, facing), 1.0f);
    g_normal_roughness[id.xy] = float4(world_normal, roughness);
    // A miss is kNoOccluder in R raw (rt_reflect.hlsl), FP16_MAX: the guide's own sky value (DLSS-RR 3.4.9).
    g_hit[id.xy] = min(hit, kNoOccluder);
}
