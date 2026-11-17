// SPDX-License-Identifier: GPL-3.0-only
// The light pass, each layer. What the programs read (program.hlsli traced_colour):
//   Light   rgb the diffuse light, light units: the sun's light by its visibility V and the surface's facing, and I,
//           the sky and bounce light; a keep, what of the base the diffuse body shows: (1 - metal)(1 - lobes.mirror)
//           in luminance. The local lights' D is the Added picture (rt_lights_space.hlsl). The opaque layer's is the
//           compose step's (rt_light_space.hlsl, after the filters); the second layer's is written here (no filter
//           follows it).
//   Mirror  rgb the specular radiance, a what it is: 1 a surface's lobe (lobes.mirror x Spec + the sun's highlight),
//           4 + F glass or water (F R by the exact Fresnel at the pixel, + the highlights). The opaque layer's is the
//           reflections pass's (rt_reflect_out.hlsl) where it runs (g_more.z bit 10); elsewhere written here from I,
//           the mean radiance over the surface's half of the world.
//   Facing  r how the surface faces the sun, max(n.s, 0); a sheet drawn from both sides lit from either, a two-sided
//           cutout's or a crown's far side by its share of light through (the compose step reads it).
#include "rt_shade.hlsli"   // rt_mirror_seen: what a mirror shows by the shared hit shading
#include "rt_mirror.hlsli"  // mirror_material: the pixel's material on the shared model

Texture2D<float4> g_guide : register(t9);
Texture2D<float4> g_normals : register(t10);
Texture2D<float4> g_motion : register(t11);
Texture2D<float4> g_sun_v : register(t12);        // rgb V as the sun's filter leaves it (SunOut; each layer its own)
Texture2D<float4> g_material : register(t13);
Texture2D<float4> g_lights_spec : register(t14);  // the local lights' highlights (rt_lights.hlsl), with their albedo
Texture2D<float4> g_point_i : register(t16);      // the opaque layer's I (Indirect; g_pass.z 1 where bound)
Texture2D<float4> g_base : register(t17);         // S.base
RT_FORMAT(rgba16f) RWTexture2D<float4> g_out : register(u0);
RT_FORMAT(rgba16f) RWTexture2D<float4> g_mirror : register(u1);
RT_FORMAT(rgba8) RWTexture2D<float4> g_facing : register(u2);

// The local lights' highlight at the pixel, over it and its four neighbours on the same object (one light a pixel is
// noisy).
float3 highlight_near(int2 pixel, float4 guide) {
    uint width, height;
    g_lights_spec.GetDimensions(width, height);
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

// How a surface faces the sun: max(n.s, 0) by `n`; a sheet drawn from both sides (item flag thin) lit from either; its
// far side, where it is a crown of leaves or a cloud (item word 14 bits 26-31: its share of light through) or a
// two-sided cutout (kCardThrough), by that share.
float sun_facing(uint item, uint flags, float3 n) {
    const float turned = dot(n, g_sun.xyz);
    if (turned >= 0.0f) {
        return saturate(turned);
    }
    const bool two_sided = (flags & kSurfaceTwoSided) != 0u;
    const bool card = two_sided && item != 0u && (g_items.Load(g_at.x + (item - 1u) * kItemBytes + 4u) & kItemAlphaTested) != 0u;
    const float cooked = item != 0u ? float(g_items.Load(g_at.x + (item - 1u) * kItemBytes + 56u) >> 26u) / 63.0f : 0.0f;
    const float through = cooked > 0.0f ? cooked : card ? kCardThrough : 0.0f;
    return saturate(through > 0.0f ? -through * turned : two_sided ? -turned : 0.0f);
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
    const bool second = rt_second_layer();
    const uint item = g_item_map.Load(here).x;
    if (second && item == 0u) {
        return;  // no blended surface here: the pictures keep the opaque layer's
    }
    const float4 guide = g_guide.Load(here);
    if (guide.a == 0.0f) {
        g_facing[id.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        if (!second && (g_more.z & 1024u) == 0u) {
            g_mirror[id.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        }
        return;
    }
    const RtSurface s = rt_surface(pixel, guide, g_normals.Load(here), g_motion.Load(here).w);
    const float4 material = g_material.Load(here);
    const uint bits = uint(material.b * 255.0f + 0.5f);
    const uint klass = bits & 15u;
    const bool water = klass == kMirrorWater;
    const bool clear = second && (water || klass == kMirrorGlass);
    // Water's body takes the sun by its surface, not a ripple's tilt (rt_sun.hlsl). No sun's direction: all of it.
    const float facing = dot(g_sun.xyz, g_sun.xyz) > 0.25f ? sun_facing(item, bits, water ? s.normal : s.smooth_) : 1.0f;
    g_facing[id.xy] = float4(facing, 0.0f, 0.0f, 0.0f);
    const bool emitters_alone = (g_more.z & 128u) != 0u;
    const bool writes_mirror = second || (g_more.z & 1024u) == 0u;
    if (!second && !writes_mirror) {
        return;  // the opaque layer's Light is the compose step's, its Mirror the reflections pass's
    }
    // I at the pixel: the opaque layer's where the sky and bounce light pass ran (the second layer in a room, under an
    // overhang: the light the opaque layer has here; under open sky the lighting sky), else the lighting sky's mean.
    float3 i = g_sky_light.rgb;
    if (g_pass.z != 0u && (!second || rt_room_point(s, s.smooth_))) {
        i = max(g_point_i.Load(here).rgb, 0.0f);
    }
    const float3 v = (g_more.z & 1u) != 0u ? g_sun_v.Load(here).rgb : rt_untraced_v(i);
    const float3 look = normalize(s.at);
    const float3 n = dot(s.smooth_, look) < 0.0f ? s.smooth_ : s.normal;
    const uint word = item != 0u ? g_items.Load(g_at.x + (item - 1u) * kItemBytes + 60u) : 0u;
    const ShadeMaterial m = mirror_material(bits, material.r, material.g, g_base.Load(here).rgb, word);
    const float nv = max(-dot(n, look), kShadeNvLeast);
    const ShadeLobes lobes = shade_lobes(m, nv);
    const bool lit = mirror_lit(bits);
    const float3 sun_spec = lit ? shade_highlight(m, lobes, n, -look, g_sun.xyz, g_sun.w) * g_sun_light.rgb * v : float3(0.0f, 0.0f, 0.0f);
    const float3 h = lit ? highlight_near(pixel, guide) : float3(0.0f, 0.0f, 0.0f);
    float4 mirror = float4(lit ? lobes.mirror * i + h + sun_spec : float3(0.0f, 0.0f, 0.0f), 1.0f);
    if (clear && (g_more.z & 32u) != 0u && g_pass.y != 0u) {
        // Glass and water: F R by the exact Fresnel of the item's index at the pixel. The camera under water (g_more.z
        // bit 12) sees the surface from inside: the index inverted, all mirrored past the critical angle.
        rt_shade_begin(g_pass.w);  // the shading frame (rt_pass_light)
        const bool inside = water && (g_more.z & 4096u) != 0u;
        const float f = shade_fresnel_exact(nv, inside ? 1.0f / m.ior : m.ior);
        float3 turned = reflect(look, n);
        if (dot(turned, s.normal) <= 0.0f) {
            turned = reflect(turned, s.normal);  // under the geometric surface (a ripple's tilt): up
        }
        uint seed = rt_seed(id.xy, g_more.y, 53u);
        const float3 r = rt_mirror_seen(rt_ray_origin(s, turned), turned, abs(g_ray.x), kRtShadeSunRay | kRtShadeLightRay, seed);
        mirror = float4(f * r + h + sun_spec, 4.0f + f);
    }
    if (emitters_alone) {
        mirror.rgb = float3(0.0f, 0.0f, 0.0f);
    }
    g_mirror[id.xy] = mirror;
    if (second) {
        // The second layer's Light: no compose step follows it. A surface that shows itself (emissive, painted: its
        // colours are its light) takes 1, its colour as drawn.
        const float keep = (1.0f - m.metal) * (1.0f - dot(lobes.mirror, float3(0.2126f, 0.7152f, 0.0722f)));
        const float3 light = !lit ? float3(1.0f, 1.0f, 1.0f)
                             : emitters_alone ? float3(0.0f, 0.0f, 0.0f)
                                              : g_sun_light.rgb * v * facing + i + rt_fill(n, rt_untraced_v(i).r);
        g_out[id.xy] = float4(light, lit ? keep : 1.0f);
    }
}
