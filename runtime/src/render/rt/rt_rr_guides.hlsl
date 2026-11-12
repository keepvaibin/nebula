// SPDX-License-Identifier: GPL-3.0-only
// The guides an outside denoiser that takes the composited picture reads (the master plan's section 17: DLSS Ray
// Reconstruction through Streamline), written only while the tracer hands its signals over raw (g_more.z bit 2048),
// from the surface pass's images of this frame, at the picture's size:
//   diffuse albedo: S.base where there is a surface, white where the sky is (it is shown as it is);
//   specular albedo: the split-sum environment term of what the surface mirrors, F0 * A + B, with A and B from Karis's
//     analytic fit ("Physically Based Shading on Mobile", 2014) of roughness and n.v; F0 rt_mirror.hlsli's mirror_f0
//     (glass and water by the item's index, a metal its colour in linear light, anything else 0.04; FB b75c); 0 at the
//     sky;
//   normal and roughness: the smooth normal in the world (rows of the eye-to-world turn in g_pass_value), roughness in
//     w (Streamline's packed mode);
//   the reflection ray's length (R raw's a; 0 where nothing reflects or the reflections pass did not run: g_pass.w
//     bit 0 says it ran).
#include "rt_bindings.hlsli"
#include "rt_frame.hlsli"
#include "rt_mirror.hlsli"  // mirror_terms: the split sum's fit, the composite's own (FB b47; l61)

Texture2D<float4> g_material : register(t9);   // S.material: r roughness, b the class and its flags (bytes)
Texture2D<float4> g_base : register(t10);      // S.base
Texture2D<float4> g_guide : register(t11);     // eye distance, the object's slot, the normal packed (a 0: no surface)
Texture2D<float4> g_reflect_raw : register(t12);  // R raw: a the ray's length (0 none, -1 not traced)
RT_FORMAT(rgba8) RWTexture2D<float4> g_diffuse : register(u0);
RT_FORMAT(rgba8) RWTexture2D<float4> g_specular : register(u1);
RT_FORMAT(rgba16f) RWTexture2D<float4> g_normal_roughness : register(u2);
RT_FORMAT(r16f) RWTexture2D<float> g_hit : register(u3);
ByteAddressBuffer g_items : register(t1);      // the item table (rt_common.hlsli's): word 15 bits 12-19 the index
Texture2D<uint2> g_item_map : register(t6);    // the pixel's item, from 1

static const uint kGlass = 3u, kWater = 9u, kMetal = 2u;

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
        g_hit[id.xy] = 0.0f;
        return;
    }
    const float4 material = g_material.Load(here);
    const float3 base = g_base.Load(here).rgb;
    const float roughness = material.r;
    const uint klass = uint(material.b * 255.0f + 0.5f) & 15u;
    const float3 eye_normal = rt_unpack_normal(guide.a);
    const float3 look = normalize(rt_eye_ray(g_ray, float2(id.xy) + 0.5f));
    const float facing = saturate(dot(eye_normal, -look));
    // The composite's own F0 (rt_reflect_out.hlsl): glass and water by the item's index (FB b75c).
    const uint item = klass == kGlass || klass == kWater || klass == kMetal ? g_item_map.Load(here).x : 0u;  // c88: metal's kind
    const uint ior_bits = item != 0u ? g_items.Load(g_at.x + (item - 1u) * kItemBytes + 60u) >> 12u & 255u : 0u;
    const float3 f0 = mirror_f0(klass, base, ior_bits);
    const float2 terms = mirror_terms(roughness, facing);
    const float3 world_normal = normalize(float3(dot(g_pass_value[0].xyz, eye_normal), dot(g_pass_value[1].xyz, eye_normal),
                                                 dot(g_pass_value[2].xyz, eye_normal)));
    float hit = 0.0f;
    if ((g_pass.w & 1u) != 0u) {
        hit = max(g_reflect_raw.Load(here).a, 0.0f);
    }
    g_diffuse[id.xy] = float4(klass == kMetal ? float3(0.0f, 0.0f, 0.0f) : base, 1.0f);
    g_specular[id.xy] = float4(saturate(f0 * terms.x + terms.y), 1.0f);
    g_normal_roughness[id.xy] = float4(world_normal, roughness);
    g_hit[id.xy] = min(hit, 65000.0f);
}
