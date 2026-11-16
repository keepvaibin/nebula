// SPDX-License-Identifier: GPL-3.0-only
// The one shading model under ray tracing: what a surface is (ShadeMaterial), what it does with the light that reaches
// it (shade_lobes, shade_surface, shade_highlight), and what water does to light inside it (ShadeMedium). The
// programs on screen and every pass of the tracer that shades a hit include this file, so a surface shows the same on
// screen, in a mirror, through water and in a bounce.
//
// Units: linear radiance, one scale; a white Lambert surface square to the stage's sun, open to its whole sky, shows 1
// (the host turns the game's light set into these inputs once a frame). Light arriving at a surface is given as
// "light": what a white Lambert surface shows under it, irradiance / pi (the tracer's signals are all in it). Lengths
// in game units (1 unit = 1 cm).
//
// The model:
//   diffuse   Lambert;
//   specular  GGX (Trowbridge-Reitz) with height-correlated Smith masking (Heitz 2014), Schlick's Fresnel for the
//             lobe and the exact dielectric Fresnel where a clear surface is a mirror;
//   energy    the split sum (Karis 2013) from a measured table (shade_dfg.hlsli), multiple scattering in the lobe
//             and the diffuse's share of what the lobe leaves (Fdez-Aguera 2019), so a surface never sends back more
//             than it receives and rough metal does not go dark;
//   media     Beer-Lambert extinction and single scattering with the analytic integral along a straight way,
//             isotropic and Henyey-Greenstein phases.
//
// Written so C++ compiles it too (runtime/src/render/shade_core_tests.cpp): no out parameters, no swizzles, no
// HLSL-only intrinsics beyond those the test defines.
#ifndef NEBULA_SHADE_CORE_HLSLI
#define NEBULA_SHADE_CORE_HLSLI

#include "shade_dfg.hlsli"

static const float kShadePi = 3.14159265f;
// The least n.v the lobe's terms take: a look within 0.3 degrees of the surface (the table's first column).
static const float kShadeNvLeast = 0.005f;
// The least GGX alpha a light's highlight takes (perceptual roughness 0.045): a point light on a mirror is a dot of a
// few pixels, not an infinitely thin peak.
static const float kShadeAlphaLeast = 0.002f;
// The most a surface's base colour reflects (fresh snow 0.9; no real surface sends back all it receives).
static const float kShadeBaseMost = 0.9f;
// Index of refraction of water and the default of a dielectric.
static const float kShadeIorWater = 1.33f;
static const float kShadeIorDefault = 1.5f;

// What lies behind a surface (ShadeMaterial::behind).
static const uint kShadeBehindOpaque = 0u;  // nothing is seen through it
static const uint kShadeBehindCutout = 1u;  // an alpha test: holes, solid elsewhere
static const uint kShadeBehindSheet = 2u;   // a thin clear wall: bends nothing, tints by its colour (both faces)
static const uint kShadeBehindBody = 3u;    // a clear body: refracts at its faces, a medium inside
static const uint kShadeBehindWater = 4u;   // water: a clear body with water's medium

struct ShadeMaterial {
    float3 base;   // linear albedo (dielectric) or specular colour (metal), at most kShadeBaseMost
    float rough;   // perceptual roughness 0..1 (GGX alpha = rough^2)
    float metal;   // 0 dielectric .. 1 metal
    float ior;     // a dielectric's index of refraction
    float3 emit;   // emitted radiance
    uint behind;   // kShadeBehind*
};

ShadeMaterial shade_material(float3 base, float rough, float metal, float ior, float3 emit, uint behind) {
    ShadeMaterial m;
    m.base = clamp(base, float3(0.0f, 0.0f, 0.0f), float3(kShadeBaseMost, kShadeBaseMost, kShadeBaseMost));
    m.rough = saturate(rough);
    m.metal = saturate(metal);
    m.ior = max(ior, 1.0f);
    m.emit = max(emit, float3(0.0f, 0.0f, 0.0f));
    m.behind = behind;
    return m;
}

// ---- Fresnel ----

// A dielectric's reflectance head-on from its index (in air).
float shade_f0_of_ior(float ior) {
    const float r = (ior - 1.0f) / (ior + 1.0f);
    return r * r;
}

// The reflectance head-on: a dielectric's from its index, a metal's its colour.
float3 shade_f0(ShadeMaterial m) {
    const float d = shade_f0_of_ior(m.ior);
    return lerp(float3(d, d, d), m.base, m.metal);
}

float3 shade_schlick(float3 f0, float cos_i) {
    const float g = 1.0f - saturate(cos_i);
    const float g5 = g * g * g * g * g;
    return f0 + (float3(1.0f, 1.0f, 1.0f) - f0) * g5;
}

// The cosine of the refracted ray's angle for a ray meeting a face at cos_i (>= 0) from a medium of index n_i into
// one of n_t, eta = n_t / n_i; -1 where the ray is totally reflected.
float shade_refracted_cos(float cos_i, float eta) {
    const float c = saturate(cos_i);
    const float s2 = (1.0f - c * c) / (eta * eta);
    return s2 >= 1.0f ? -1.0f : sqrt(1.0f - s2);
}

// The exact reflectance of a smooth dielectric face for unpolarized light (the mean of s and p), eta = n_t / n_i;
// 1 under total internal reflection.
float shade_fresnel_exact(float cos_i, float eta) {
    const float ci = saturate(cos_i);
    const float ct = shade_refracted_cos(ci, eta);
    if (ct < 0.0f) {
        return 1.0f;
    }
    const float rs = (ci - eta * ct) / (ci + eta * ct);
    const float rp = (eta * ci - ct) / (eta * ci + ct);
    return 0.5f * (rs * rs + rp * rp);
}

// ---- Metals ----

// A metal's F0 by its kind (the cooker's hit_record.rs METAL_KINDS, the same numbers; linear): gold, silver, copper,
// iron, chrome, brass, platinum; 0 or past 7: none (the caller takes the art's colour).
static const float kShadeMetalTint = 0.25f;  // hit_record.rs METAL_TINT: how far the art's hue tints a kind's F0
float3 shade_metal_f0(uint kind) {
    if (kind == 1u) { return float3(1.000f, 0.766f, 0.336f); }
    if (kind == 2u) { return float3(0.972f, 0.960f, 0.915f); }
    if (kind == 3u) { return float3(0.955f, 0.638f, 0.538f); }
    if (kind == 4u) { return float3(0.562f, 0.565f, 0.578f); }
    if (kind == 5u) { return float3(0.550f, 0.556f, 0.554f); }
    if (kind == 6u) { return float3(0.910f, 0.778f, 0.423f); }
    return float3(0.673f, 0.637f, 0.585f);
}
// A metal's colour (its F0) from its kind and the art's linear colour: a kind's F0 tinted kShadeMetalTint toward the
// art's hue; with no kind, the art's hue at no less than half its strength (a dark painted base under a painted
// reflection is a metal whose look the reflection carried).
float3 shade_metal_colour(uint kind, float3 art) {
    const float top = max(max(art.x, art.y), art.z);
    const float3 hue = top > 1e-4f ? art / top : float3(1.0f, 1.0f, 1.0f);
    if (kind >= 1u && kind <= 7u) {
        return shade_metal_f0(kind) * lerp(float3(1.0f, 1.0f, 1.0f), hue, kShadeMetalTint);
    }
    return hue * max(top, 0.5f);
}

// ---- The lobe's terms over the environment (split sum) ----

// A and B at n.v and the perceptual roughness: what the lobe reflects of even light is F0 A + B. Bilinear between the
// table's nodes (n.v = least + (1 - least) (i / 31)^2, roughness j / 31).
float2 shade_dfg(float nv, float rough) {
    const float u = sqrt(saturate((max(nv, kShadeNvLeast) - kShadeNvLeast) / (1.0f - kShadeNvLeast))) * 31.0f;
    const float w = saturate(rough) * 31.0f;
    const uint x0 = min(uint(u), 30u);
    const uint y0 = min(uint(w), 30u);
    const float fx = u - float(x0);
    const float fy = w - float(y0);
    const float2 a = lerp(kShadeDfg[y0 * 32u + x0], kShadeDfg[y0 * 32u + x0 + 1u], fx);
    const float2 b = lerp(kShadeDfg[(y0 + 1u) * 32u + x0], kShadeDfg[(y0 + 1u) * 32u + x0 + 1u], fx);
    return lerp(a, b, fy);
}

// How a surface splits what reaches it, at one n.v (Fdez-Aguera 2019, "A Multiple-Scattering Microfacet Model for
// Real-Time Image Based Lighting"):
//   mirror   what it mirrors of even light over its lobe, the lobe's multiple scattering included: times the radiance
//            the reflection brings (filtered over the lobe);
//   diffuse  what its body sends back of the light that enters it, times irradiance / pi: the base colour times what
//            the lobe leaves (a metal none);
//   boost    the factor a light's single-scattering highlight takes for the lobe's multiple scattering: mirror over
//            the single-scattering share, so a light's highlight summed over every direction is what the surface
//            mirrors of even light (the same energy on both ways).
struct ShadeLobes {
    float3 mirror;
    float3 diffuse;
    float3 boost;
};

ShadeLobes shade_lobes(ShadeMaterial m, float nv) {
    const float2 dfg = shade_dfg(nv, m.rough);
    const float3 f0 = shade_f0(m);
    const float3 single = f0 * dfg.x + float3(dfg.y, dfg.y, dfg.y);  // F E: the single-scattering share
    const float e = max(dfg.x + dfg.y, 1e-4f);                         // E: the lobe's energy at F = 1
    const float3 f_avg = f0 + (float3(1.0f, 1.0f, 1.0f) - f0) / 21.0f;
    const float3 f_ms = single * f_avg / (float3(1.0f, 1.0f, 1.0f) - (1.0f - e) * f_avg);
    ShadeLobes l;
    l.mirror = single + f_ms * (1.0f - e);
    l.diffuse = m.base * (1.0f - m.metal) * (float3(1.0f, 1.0f, 1.0f) - l.mirror);
    l.boost = l.mirror / max(single, float3(1e-4f, 1e-4f, 1e-4f));
    return l;
}

// ---- A light's highlight ----

float shade_ggx_d(float nh, float alpha) {
    const float a2 = alpha * alpha;
    const float t = nh * nh * (a2 - 1.0f) + 1.0f;
    return a2 / (kShadePi * t * t);
}

// The height-correlated Smith visibility G2 / (4 n.l n.v) (Heitz 2014).
float shade_smith_vis(float nv, float nl, float alpha) {
    const float a2 = alpha * alpha;
    const float gv = nl * sqrt(nv * nv * (1.0f - a2) + a2);
    const float gl = nv * sqrt(nl * nl * (1.0f - a2) + a2);
    return 0.5f / max(gv + gl, 1e-7f);
}

// The radiance a light sends to the eye off the lobe, per unit of the light's light on a face square to it (its
// irradiance there / pi): pi D Vis F n.l with the multiple-scattering boost. `spread`: the light's radius over its distance; the lobe widens
// by half of it and keeps its energy (Karis 2013, sphere lights: (alpha / alpha')^2), so a lamp's highlight on a mirror
// is the lamp's size.
float3 shade_highlight(ShadeMaterial m, ShadeLobes lobes, float3 n, float3 v, float3 l, float spread) {
    const float nl = dot(n, l);
    const float nv = dot(n, v);
    if (nl <= 0.0f || nv <= 0.0f) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    const float3 h = normalize(l + v);
    const float alpha = max(m.rough * m.rough, kShadeAlphaLeast);
    const float wide = saturate(alpha + 0.5f * spread);
    const float keep = (alpha / wide) * (alpha / wide);
    const float3 f = shade_schlick(shade_f0(m), dot(v, h));
    return f * lobes.boost * (kShadePi * shade_ggx_d(saturate(dot(n, h)), wide) * keep * shade_smith_vis(nv, nl, wide) * nl);
}

// ---- The surface ----

// What a surface sends to the eye:
//   light        the light that reaches it from every source but its mirror (the sun by its visibility and n.l, the
//                local lights, the sky and the bounce light), on its face, as light (irradiance / pi);
//   reflected    the radiance its mirror brings, filtered over its lobe;
//   highlights   the lights' highlights (shade_highlight times each light's light square to it, shadowed).
float3 shade_surface(ShadeMaterial m, ShadeLobes lobes, float3 light, float3 reflected, float3 highlights) {
    return lobes.diffuse * light + lobes.mirror * reflected + highlights + m.emit;
}

// ---- Thin clear sheets (glass panes, bubbles' walls) ----

// A thin slab of reflectance r a face and inner transmittance tau, every inner reflection summed (Remix's thin-walled
// series): what it lets through and what it mirrors.
float3 shade_sheet_through(float r, float3 tau) {
    const float3 rt = r * tau;
    return (1.0f - r) * (1.0f - r) * tau / (float3(1.0f, 1.0f, 1.0f) - rt * rt);
}

float3 shade_sheet_mirror(float r, float3 tau) {
    const float3 rt = r * tau;
    return float3(r, r, r) + (1.0f - r) * (1.0f - r) * r * tau * tau / (float3(1.0f, 1.0f, 1.0f) - rt * rt);
}

// ---- Media ----

// A medium's coefficients per unit of length.
//   absorb   absorption a;
//   water    scattering by the medium itself, isotropic (b_w);
//   grains   scattering by particles in it, forward by Henyey-Greenstein g (b_p);
//   g        the particles' asymmetry.
struct ShadeMedium {
    float3 absorb;
    float3 water;
    float3 grains;
    float g;
};

// Water: pure water's absorption (Pope and Fry 1997: 0.340, 0.0565, 0.0092 per metre at 650, 550, 450 nm), scaled so
// green's 1/e way is `clear` units (1770: pure water's own); its own scattering (Morel 1974: 0.00288 (lambda /
// 500)^-4.32 per metre); clear ocean's particles (0.05 per metre at 550 nm, (lambda / 550)^-1; Petzold's phase, g 0.92).
ShadeMedium shade_water(float clear) {
    ShadeMedium w;
    w.absorb = float3(0.00340f, 0.000565f, 0.000092f) * (1770.0f / max(clear, 1.0f));
    w.water = float3(0.0000093f, 0.000018f, 0.000045f);
    w.grains = float3(0.0005f * 550.0f / 650.0f, 0.0005f, 0.0005f * 550.0f / 450.0f);
    w.g = 0.92f;
    return w;
}

// Extinction of light that goes on (the similarity rule: light scattered a few degrees forward stays in the beam).
float3 shade_extinction(ShadeMedium w) {
    return w.absorb + w.water + w.grains * (1.0f - w.g);
}

// What of the light comes through `d` units of the medium.
float3 shade_beer(float3 sigma, float d) {
    return exp(-sigma * d);
}

float shade_phase_hg(float g, float cos_theta) {
    const float g2 = g * g;
    const float t = max(1.0f + g2 - 2.0f * g * cos_theta, 1e-6f);
    return (1.0f - g2) / (4.0f * kShadePi * t * sqrt(t));
}

// The integral of exp(-k s) over s in [0, d]: what a straight way of length d gathers of a source that is 1 at its
// start, each part dimmed by k over the way back (k < 0 where the source grows along the way faster than the way
// dims it: looking up toward the surface from under water). d may be huge (no floor): the limit 1 / k.
float shade_gathered1(float k, float d) {
    const float kd = k * d;
    if (abs(kd) < 0.01f) {
        return d * (1.0f - kd * (0.5f - kd / 6.0f));  // the series: 1 - exp loses its digits there in floats
    }
    return (1.0f - exp(-k * d)) / k;
}

float3 shade_gathered(float3 source, float3 k, float d) {
    return source * float3(shade_gathered1(k.x, d), shade_gathered1(k.y, d), shade_gathered1(k.z, d));
}

// The light water scatters toward the eye along a way of length d inside it, before the way's own extinction to the
// eye (shade_beer) is applied to what lies at its end. The light is as it is at the way's start (the end nearer the
// eye: the surface for a look from above, the eye under water):
//   sun_in      the sun's light there (irradiance / pi), after the surface's Fresnel, on a face square to its refracted
//               direction, with its visibility;
//   sun_cos     the cosine of the refracted sun's angle to the surface's normal (> 0);
//   sun_theta   the cosine of the angle between the refracted sun's direction of travel and the way toward the eye;
//   sky_in      the sky's and the local lights' light entering the water on a face square to the normal;
//   depth_rate  how much deeper the way goes a unit of its length (the refracted eye ray's cosine to the normal for a
//               look from above; negative toward the surface from below);
//   from_above  the eye looks into the water from the air (the diffuse light's back-scattered share), else from
//               within it.
// Each source fades with depth as it goes down (k = sigma + K depth_rate, K = sigma / cos for the sun and sigma / 0.8,
// the mean cosine of diffuse light under water, for the sky).
float3 shade_water_gathered(ShadeMedium w, float3 sun_in, float sun_cos, float sun_theta, float3 sky_in,
                            float depth_rate, bool from_above, float d) {
    const float3 sigma = shade_extinction(w);
    const float3 b_sun = w.water / (4.0f * kShadePi) + w.grains * shade_phase_hg(w.g, sun_theta);
    const float3 k_sun = sigma + sigma / max(sun_cos, 0.05f) * depth_rate;
    const float3 b_back = 0.5f * w.water + (from_above ? 0.018f : 0.982f) * w.grains;
    const float3 k_sky = sigma + sigma / 0.8f * depth_rate;
    // Irradiance pi * light; the sky's back-scattered share spread over the half of directions toward the eye's side.
    return shade_gathered(b_sun * (kShadePi * sun_in), k_sun, d) + shade_gathered(b_back * (0.5f * sky_in), k_sky, d);
}

#endif
