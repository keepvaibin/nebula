// SPDX-License-Identifier: GPL-3.0-only
// The reflections into the mirror picture the programs read (program.hlsli traced_mirror: colour * a + rgb), the
// opaque layer (the master plan's 12.4): rgb R * its weight, a what stays of the colour under it (rt_mirror.hlsli:
// ADD by Fresnel), or where a painted environment map is, its SUBSTITUTE (below). R already carries the
// local lights' highlights (rt_reflect.hlsl); a pixel that does not reflect under the preset takes its highlights
// here, over it and its four neighbours on the same object, as the light pass did.
// SUBSTITUTE (12.4, FC's 22.13 item 2: the programs fetch a painted environment map through ENV_FETCH, program.hlsli
// env_fetch): at a painted pixel that is not water, with a traced R, the mirror picture holds rgb R times
// the painted map's own colour (item word 15 bits 0-11, 4 bits a channel; none: white) and the class's gain, a
// -(1 + keep (1 - saturate(lum(R) / 0.25))): how much of the painted shine comes back where R is dark. The programs
// put R in the painted map's place inside the artist's own stage graph. A painted map that is a capture of the scene
// (item word 11 hit flag 128: the frozen lake's mirror, whose pass High leaves out) keeps none: R holds the scene.
// Pass constants: g_pass.x the layer (0 alone); g_pass.w bit 0: R is the reference's mean (section 18; FB), and the
// local lights' highlights join it here as the reflections pass adds them to its R raw (rt_reflect.hlsl).
#include "rt_bindings.hlsli"
#include "rt_frame.hlsli"
#include "rt_mirror.hlsli"

Texture2D<float4> g_reflect : register(t9);
Texture2D<float4> g_guide : register(t10);
Texture2D<float4> g_normals : register(t11);
Texture2D<float4> g_material : register(t12);
Texture2D<float4> g_base : register(t13);
Texture2D<float4> g_lights_spec : register(t14);  // the local lights' highlights (rt_lights.hlsl)
RT_FORMAT(rgba16f) RWTexture2D<float4> g_mirror : register(u0);
ByteAddressBuffer g_items : register(t1);         // the item table (rt_common.hlsli's)
Texture2D<uint2> g_item_map : register(t6);       // the pixel's item, from 1
static const uint kCaptured = 128u;               // a hit flag (item word 11 bits 8-15): its maps include a capture

// The pixel's index for its mirror's F0 (rt_mirror.hlsli mirror_f0; item word 15 bits 12-19): glass and water only,
// as rt_reflect.hlsl reads it (FB b75b).
uint mirror_ior_bits(int3 here, uint klass) {
    const uint item = klass == kMirrorGlass || klass == kMirrorWater || klass == kMirrorMetal ? g_item_map.Load(here).x : 0u;  // c88
    return item != 0u ? g_items.Load(g_at.x + (item - 1u) * kItemBytes + 60u) >> 12u & 255u : 0u;
}

// The pixel's item word 15 for its mirror (d19): bits 0-11 what the art's material returns of a white environment
// (4 bits a channel; 0: no painted map), bits 12-19 its index or a metal's kind; 0 for a class that mirrors nothing.
uint mirror_word(int3 here, uint klass) {
    const bool mirrors = klass == kMirrorGlass || klass == kMirrorWater || klass == kMirrorMetal || klass == kMirrorGloss;
    const uint item = mirrors ? g_item_map.Load(here).x : 0u;
    return item != 0u ? g_items.Load(g_at.x + (item - 1u) * kItemBytes + 60u) : 0u;
}

float3 unoct(float2 o) {
    float3 n = float3(o, 1.0f - abs(o.x) - abs(o.y));
    const float t = saturate(-n.z);
    n.xy += float2(n.x >= 0.0f ? -t : t, n.y >= 0.0f ? -t : t);
    return normalize(n);
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
    if (guide.a == 0.0f) {
        g_mirror[id.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }
    float4 r = g_reflect.Load(here);
    if (r.a == 0.0f) {
        float3 sum = g_lights_spec.Load(here).rgb;
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
        // Over what the light pass wrote here (a dielectric's sheen, FB b107; else 0, 0, 0, 1).
        const float4 kept = g_mirror[id.xy];
        g_mirror[id.xy] = float4(kept.rgb + sum / taps, kept.a);
        return;
    }
    const float4 material = g_material.Load(here);
    const float4 normals = g_normals.Load(here);
    const float3 look = normalize(rt_eye_ray(g_ray, float2(pixel) + 0.5f));
    const float3 smooth_ = unoct(normals.rg);
    const float3 n = dot(smooth_, look) < 0.0f ? smooth_ : unoct(normals.ba);
    const uint bits = uint(material.b * 255.0f + 0.5f);
    const uint klass = bits & 15u;
    const uint ior_bits = mirror_ior_bits(here, klass);
    const uint word15 = mirror_word(here, klass);  // d19
    if ((g_pass.w & 1u) != 0u) {
        const float4 weight = mirror_weight_word(bits, material.r, material.a, g_base.Load(here).rgb,
                                                 1.0f - saturate(-dot(n, look)), word15);
        r.rgb += g_lights_spec.Load(here).rgb / max(weight.rgb, 0.02f);
    }
    // d19: a painted gloss or metal is ADDED by its Fresnel (below); painted glass keeps the SUBSTITUTE.
    const bool added = mirror_painted_added(bits);
    if ((bits & kMirrorPainted) != 0u && klass != kMirrorWater && !added) {
        const uint item = g_item_map.Load(here).x;
        const uint at = g_at.x + (item != 0u ? item - 1u : 0u) * kItemBytes;
        const uint word = item != 0u ? g_items.Load(at + 60u) : 0u;
        const bool captured = item != 0u && (g_items.Load(at + 44u) & (kCaptured << 8u)) != 0u;
        // A dielectric's mirror has no colour: glass keeps none of the painted map's tint (b75b).
        const float3 tint = (word & 0xFFFu) != 0u && klass != kMirrorGlass
                                ? float3(word & 15u, (word >> 4u) & 15u, (word >> 8u) & 15u) / 15.0f
                                : float3(1.0f, 1.0f, 1.0f);
        const float gain = 1.0f;  // no gain over the mirror (A4 W5; FB b75)
        const float keep = captured ? 0.0f : klass == kMirrorMetal ? rt_dev_or(kDevKeepMetal, 0.5f)
                                           : klass == kMirrorGloss ? rt_dev_or(kDevKeepGloss, 0.25f) : 0.0f;
        const float seen = dot(r.rgb, float3(0.2126f, 0.7152f, 0.0722f));
        g_mirror[id.xy] = float4(r.rgb * tint * gain, -(1.0f + keep * (1.0f - saturate(seen / 0.25f))));
        return;
    }
    const float4 w = mirror_weight_word(uint(material.b * 255.0f + 0.5f), material.r, material.a, g_base.Load(here).rgb,
                                        1.0f - saturate(-dot(n, look)), word15);
    // A painted one added: a = -(12 + what stays of the base), so the program's fetch of the painted map gives black
    // there (program.hlsli env_fetch) and traced_mirror adds F R over (1 - F) of the base.
    g_mirror[id.xy] = float4(r.rgb * w.rgb, added ? -(12.0f + w.a) : w.a);
}
