// SPDX-License-Identifier: GPL-3.0-only
// The guides an outside denoiser that takes the composited picture reads (DLSS Ray
// Reconstruction through Streamline), written only while the tracer hands its signals over raw (g_more.z bit 2048),
// from the surface pass's images of this frame, at the picture's size:
//   diffuse albedo: lobes.diffuse, the base the diffuse light is shown on (the composite's base x keep: S.base in
//     linear light, (1 - metal)(1 - lobes.mirror)); white where the sky is and on a surface that shows itself;
//   specular albedo: lobes.mirror, the split sum's F0 A + B with the multiple-scattering lobe (shade_core.hlsli
//     shade_lobes on the pixel's material, rt_mirror.hlsli mirror_material), the weight the composite puts on R
//     (rt_reflect_out.hlsl); 0 at the sky;
//   normal and roughness: the shading normal in the world (S.normal's, whole, where g_pass.w bit 1 says it is bound at
//     t13; else the guide's packed one; rows of the eye-to-world turn in g_pass_value), roughness in w (Streamline's
//     packed mode);
//   the reflection ray's length (R raw's a; 0 where nothing reflects or the reflections pass did not run: g_pass.w
//     bit 0 says it ran; 65504 where it met nothing and where no surface is, the guide's sky value).
// The albedos in linear light in a UNORM target (the DLSS-RR Integration Guide 3.4.1-3.4.2: sRGB formats unsupported).
#include "rt_bindings.hlsli"
#include "rt_frame.hlsli"
#include "rt_mirror.hlsli"  // mirror_material: the composite's own lobes

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
    // The shading normal whole (the water's ripple too), else the guide's packed one.
    const float3 eye_normal = (g_pass.w & 2u) != 0u ? rt_unoct(g_normals.Load(here).rg) : rt_unpack_normal(guide.a);
    const float3 look = normalize(rt_eye_ray(g_ray, float2(id.xy) + 0.5f));
    const uint bits = uint(material.b * 255.0f + 0.5f);
    const uint item = g_item_map.Load(here).x;
    const uint word15 = item != 0u ? g_items.Load(g_at.x + (item - 1u) * kItemBytes + 60u) : 0u;
    const ShadeMaterial m = mirror_material(bits, roughness, material.g, base, word15);
    const ShadeLobes lobes = shade_lobes(m, max(dot(eye_normal, -look), kShadeNvLeast));
    const bool lit = mirror_lit(bits);
    const float3 world_normal = normalize(float3(dot(g_pass_value[0].xyz, eye_normal), dot(g_pass_value[1].xyz, eye_normal),
                                                 dot(g_pass_value[2].xyz, eye_normal)));
    float hit = 0.0f;
    if ((g_pass.w & 1u) != 0u) {
        hit = max(g_reflect_raw.Load(here).a, 0.0f);
    }
    // In linear light, as the specular guide and the radiance they divide (S.base holds the colour encoded).
    g_diffuse[id.xy] = float4(lit ? saturate(lobes.diffuse) : float3(1.0f, 1.0f, 1.0f), 1.0f);
    g_specular[id.xy] = float4(lit ? saturate(lobes.mirror) : float3(0.0f, 0.0f, 0.0f), 1.0f);
    g_normal_roughness[id.xy] = float4(world_normal, roughness);
    // A miss is kNoOccluder in R raw (rt_reflect.hlsl), FP16_MAX: the guide's own sky value (DLSS-RR 3.4.9).
    g_hit[id.xy] = min(hit, kNoOccluder);
}
