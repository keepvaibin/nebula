// SPDX-License-Identifier: GPL-3.0-only
// How much of what a surface mirrors reaches the picture, shared by the reflections pass
// (which carries the local lights' highlights through the filter as radiance: highlight / weight) and its composite.
//   ADD (water; glass, metal and gloss with no painted environment map): F = F0 + (1 - F0) * (1 - n.v)^5 (Schlick);
//     F0 = ((ior - 1) / (ior + 1))^2 * kShine (glass 2.0, water 1.5: fully reflective, clear and
//     shiny), a metal's own colour, gloss 0.04; F never over 1. The colour under it keeps 1 - F.
//   SUBSTITUTE (a painted environment map): until the programs fetch R in place of the painted map,
//     the first cut's multiply: tint * (head-on share + Schlick's rise), the colour under it 1 - 0.6 * the painted
//     share (rt_light.hlsl's numbers, so only R itself changes).
#ifndef NEBULA_RT_MIRROR_HLSLI
#define NEBULA_RT_MIRROR_HLSLI

static const uint kMirrorGloss = 1u, kMirrorMetal = 2u, kMirrorGlass = 3u, kMirrorWater = 9u;
static const uint kMirrorPainted = 64u;  // S.material: a painted reflection (NEBULA_HIT_ENVIRONMENT)

// rgb what of R reaches the picture, a what stays of the colour under it. `bits` S.material.b as a byte, `rough` its
// roughness, `share` its painted share, `colour` S.base, `glance` 1 - n.v.
float4 mirror_weight(uint bits, float rough, float share, float3 colour, float glance) {
    const uint klass = bits & 15u;
    const float glance5 = glance * glance * glance * glance * glance;
    if ((bits & kMirrorPainted) != 0u && klass != kMirrorWater) {
        float head_on = 0.0f;
        float3 tint = float3(1.0f, 1.0f, 1.0f);
        if (klass == kMirrorGlass) {
            head_on = 0.1f;
        } else if (klass == kMirrorMetal) {
            head_on = 0.55f;
            tint = lerp(float3(1.0f, 1.0f, 1.0f), colour, 0.5f);
        } else if (klass == kMirrorGloss) {
            head_on = 0.05f * (1.0f - rough);
        }
        return float4(tint * (head_on + (1.0f - head_on) * glance5), 1.0f - 0.6f * share);
    }
    float3 f0 = float3(0.04f, 0.04f, 0.04f);
    if (klass == kMirrorGlass) {
        f0 = (0.04f * 2.0f).xxx;    // ior 1.5, kShine 2.0
    } else if (klass == kMirrorWater) {
        f0 = (0.0201f * 1.5f).xxx;  // ior 1.33, kShine 1.5
    } else if (klass == kMirrorMetal) {
        f0 = colour;
    }
    const float3 f = min(f0 + (1.0f - f0) * glance5, 1.0f);
    return float4(f, 1.0f - dot(f, float3(0.2126f, 0.7152f, 0.0722f)));
}

#endif
