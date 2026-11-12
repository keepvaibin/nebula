// SPDX-License-Identifier: GPL-3.0-only
// How much of what a surface mirrors reaches the picture (the master plan's 12.4), shared by the reflections pass
// (which carries the local lights' highlights through the filter as radiance: highlight / weight) and its composite.
//   ADD (water; glass, metal and gloss with no painted environment map): F = F0 * A + B, the split sum's environment
//     term over the surface's lobe (mirror_terms: Schlick's rise where it is smooth, far less at a glance where it is
//     rough; FB b47; under roughness 0.03 Schlick's own rise, FB b75); F0 = ((ior - 1) / (ior + 1))^2 exactly (glass
//     0.04, water 0.0201: LIGHT-MODEL.md 6, A4 W5), a metal's own colour, gloss 0.04; F never over 1. The colour under
//     it keeps 1 - F; under a metal none (a metal has no body: A4 W9).
//   SUBSTITUTE (a painted environment map): until the programs fetch R in place of the painted map (work list 22.13),
//     the first cut's multiply: tint * (the head-on share * A + B), the colour under it 1 - 0.6 * the painted share
//     (rt_light.hlsl's numbers, so only R itself changes).
#ifndef NEBULA_RT_MIRROR_HLSLI
#define NEBULA_RT_MIRROR_HLSLI

#include "rt_frame.hlsli"  // rt_dev_or (l63)

static const uint kMirrorGloss = 1u, kMirrorMetal = 2u, kMirrorGlass = 3u, kMirrorWater = 9u;
static const uint kMirrorPainted = 64u;  // S.material: a painted reflection (NEBULA_HIT_ENVIRONMENT)

// The split sum's environment term (A, B: what a surface mirrors over its lobe is F0 * A + B) by roughness and n.v,
// Karis's analytic fit ("Physically Based Shading on Mobile", 2014): Schlick's rise where the surface is smooth, far
// less at a glance where it is rough (0.45: about 0.2 at a grazing look). The same fit rt_rr_guides.hlsl gives the
// denoiser as the specular albedo (FB b47).
float2 mirror_terms(float rough, float facing) {
    if (rough < 0.03f) {
        // A mirror: Schlick's F0 + (1 - F0) (1 - n.v)^5 exactly (the fit is 14 to 29 % over it here; A4 K1, FB b75).
        const float g = 1.0f - saturate(facing);
        const float p = g * g * g * g * g;
        return float2(1.0f - p, p);
    }
    const float4 r = rough * float4(-1.0f, -0.0275f, -0.572f, 0.022f) + float4(1.0f, 0.0425f, 1.04f, -0.04f);
    const float a004 = min(r.x * r.x, exp2(-9.28f * facing)) * r.x + r.y;
    return float2(-1.04f, 1.04f) * a004 + r.zw;
}

// sRGB's decode for a metal's colour (M1; FB b59b): its own name, this header stands without rt_common.hlsli.
float3 mirror_lin(float3 c) {
    c = max(c, 0.0f);
    return lerp(pow((c + 0.055f) / 1.055f, 2.4f), c / 12.92f, step(c, 0.04045f));
}

// rgb what of R reaches the picture, a what stays of the colour under it. `bits` S.material.b as a byte, `rough` its
// roughness, `share` its painted share, `colour` S.base, `glance` 1 - n.v.
float4 mirror_weight(uint bits, float rough, float share, float3 colour, float glance) {
    const uint klass = bits & 15u;
    const float2 terms = mirror_terms(saturate(rough), saturate(1.0f - glance));
    if ((bits & kMirrorPainted) != 0u && klass != kMirrorWater) {
        float head_on = 0.0f;
        float3 tint = float3(1.0f, 1.0f, 1.0f);
        if (klass == kMirrorGlass) {
            head_on = rt_dev_or(kDevPaintedGlass, 0.1f);
        } else if (klass == kMirrorMetal) {
            head_on = rt_dev_or(kDevPaintedMetal, 0.55f);
            tint = lerp(float3(1.0f, 1.0f, 1.0f), mirror_lin(colour), 0.5f);
        } else if (klass == kMirrorGloss) {
            head_on = rt_dev_or(kDevPaintedGloss, 0.05f) * (1.0f - rough);
        }
        return float4(tint * saturate(head_on * terms.x + terms.y), 1.0f - rt_dev_or(kDevPaintedMirror, 0.6f) * share);
    }
    float3 f0 = float3(0.04f, 0.04f, 0.04f);
    if (klass == kMirrorGlass) {
        f0 = float3(0.04f, 0.04f, 0.04f);  // ior 1.5, Schlick's exactly (A4 W5; FB b75)
    } else if (klass == kMirrorWater) {
        f0 = float3(0.0201f, 0.0201f, 0.0201f);  // ior 1.33 (A4 W5; FB b75)
    } else if (klass == kMirrorMetal) {
        f0 = mirror_lin(colour);
    }
    const float3 f = saturate(f0 * terms.x + terms.y);
    // What stays of the colour under it: 1 - F; a metal none, all its light is the mirror's (A4 W9; FB b75).
    return float4(f, klass == kMirrorMetal ? 0.0f : 1.0f - dot(f, float3(0.2126f, 0.7152f, 0.0722f)));
}

#endif
