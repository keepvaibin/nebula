// SPDX-License-Identifier: GPL-3.0-only
// The specular side of a pixel's material, from the surface images (S.material, S.base) and its item's word 15, on the
// shared model (shade_core.hlsli): what the reflections pass weighs its R by (lobes.mirror, so the local lights'
// highlights ride through the filter demodulated: highlight / lobes.mirror), the compose steps' sun highlight, and the
// denoiser's specular albedo. The programs take the same lobes from their own colour (program.hlsli).
//   metal (class 2)  its colour is its F0: its kind's (item word 15 bits 12-19, 1 to 7) tinted by the art's hue, else the
//                    art's hue at no less than half (shade_metal_colour); no diffuse body;
//   glass, water     dielectrics of the item's index (word 15 bits 12-19 as (n - 1) x 100), else the class's: 1.5, 1.33;
//   any other class  a dielectric of index 1.5 (F0 0.04): every surface has its GGX lobe at its roughness.
#ifndef NEBULA_RT_MIRROR_HLSLI
#define NEBULA_RT_MIRROR_HLSLI

#include "rt_frame.hlsli"
#include "shade_core.hlsli"

static const uint kMirrorGloss = 1u, kMirrorMetal = 2u, kMirrorGlass = 3u, kMirrorEmissive = 6u, kMirrorPainted = 7u,
                  kMirrorWater = 9u;

// sRGB's decode: its own name, this header stands without rt_common.hlsli.
float3 mirror_lin(float3 c) {
    c = max(c, 0.0f);
    return lerp(pow((c + 0.055f) / 1.055f, 2.4f), c / 12.92f, step(c, 0.04045f));
}

// The material at a pixel: `bits` S.material.b as a byte (class in the low four bits), `rough` S.material.r, `metal`
// S.material.g, `colour` S.base (sRGB), `word` the item's word 15 (0 where none).
ShadeMaterial mirror_material(uint bits, float rough, float metal, float3 colour, uint word) {
    const uint klass = bits & 15u;
    const uint ior_bits = word >> 12u & 255u;
    const float3 art = mirror_lin(colour);
    if (klass == kMirrorMetal) {
        return shade_material(shade_metal_colour(ior_bits, art), rough, 1.0f, kShadeIorDefault, float3(0.0f, 0.0f, 0.0f),
                              kShadeBehindOpaque);
    }
    const bool clear = klass == kMirrorGlass || klass == kMirrorWater;
    const float ior = clear && ior_bits != 0u ? 1.0f + float(ior_bits) / 100.0f
                      : klass == kMirrorWater ? kShadeIorWater
                                              : kShadeIorDefault;
    const uint behind = klass == kMirrorWater ? kShadeBehindWater : klass == kMirrorGlass ? kShadeBehindSheet : kShadeBehindOpaque;
    return shade_material(art, rough, saturate(metal), ior, float3(0.0f, 0.0f, 0.0f), behind);
}

// The roughness up to which the reflections pass traces a lobe (rt_reflect.hlsl): past it the lobe is so wide that
// the sky and bounce light I stands for what it mirrors (Lumen's cut).
static const float kTracedRoughHigh = 0.6f, kTracedRoughMedium = 0.25f;

// Whether a pixel's class takes light at all (an emissive or painted surface shows itself: no lobe).
bool mirror_lit(uint bits) {
    const uint klass = bits & 15u;
    return klass != kMirrorEmissive && klass != kMirrorPainted;
}

// The sun's highlight on a pixel, demodulated (over lobes.mirror) as the specular picture holds it, per unit of the
// sun's light on a face square to it: `n` the shading normal toward the eye, `v` toward the eye, `sun` toward the sun,
// `spread` the sun disc's radius over its distance (g_sun.w).
float3 mirror_sun_spec(ShadeMaterial m, ShadeLobes lobes, float3 n, float3 v, float3 sun, float spread) {
    return shade_highlight(m, lobes, n, v, sun, spread) / max(lobes.mirror, float3(1e-3f, 1e-3f, 1e-3f));
}

#endif
