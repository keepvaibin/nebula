// SPDX-License-Identifier: GPL-3.0-only
// A mod's glTF material (glTF 2.0 metallic-roughness) in a program's lit form: the template the generator writes for a
// material with MaterialIr pbr (crates/nebula-import/src/cook/programs.rs pbr_*; plan 3.3, contract materials). The
// material's stages make its base colour (factor x base map x vertex colour, display-encoded, as G's cook writes them);
// this lights it: the light set's ambient and lights summed as the game's lit channel sums them (each light by its
// traced share in the traced form, the ambient by AMBIENT_TERM: MOD-MATERIALS-FC.md 1), Lambert for the diffuse part,
// GGX with the material's roughness for the first light's highlight, and a reflection of the stage along the mirrored
// view: the stage's light as an environment (its fill as the sky about the player's up, its ambient as the ground),
// through ENV_FETCH, so where the tracer owns the pixel the traced scene stands in its place (program.hlsli env_fetch).
// Metal from the factor and the map's blue, roughness from the factor and its green, occlusion on the indirect light.

#ifndef PROGRAM_PBR_HLSLI
#define PROGRAM_PBR_HLSLI

#include "program_lit.hlsli"

static const float kPbrRoughMin = 0.045;   // least perceptual roughness: a highlight stays a few pixels wide
static const float kPbrGroundShare = 0.5;  // the ground under the horizon by the ambient: half the sky side
static const float kPbrHorizon = 0.08;     // the horizon width of a mirror-smooth reflection, in N.up

struct PbrShade {
    float3 ambient;  // the ambient, linear (irradiance / pi): what occlusion takes
    float3 direct;   // the lights, linear, by their facing
    float3 key;      // the first lit light: radiance, linear, by its attenuation
    float3 key_dir;  // and its direction in eye space
    float3 fill;     // light 1: its colour, linear (the sky of the environment)
    float3 amb_lin;  // the ambient colour, linear (the ground of the environment)
    bool has_key;
};

static PbrShade g_pbr;

// The ambient of the light set: the sky hemisphere as the enhanced channel takes it.
void pbr_begin(float3 amb) {
    g_pbr = (PbrShade)0;
    g_pbr.amb_lin = enh_lin(amb);
    g_pbr.ambient = AMBIENT_TERM(g_pbr.amb_lin * enh_sky(g_enh));
}

// Light n of the material's channel, summed as enh_add_light sums it (the same traced share, c52 and c53); the first one
// with a facing is the key whose highlight the material takes.
void pbr_add_light(uint base, uint n, bool spot, uint diffuse, bool first, float3 eye, float3 amb) {
    float3 ldir;
    float attn;
    float3 lcol;
    enh_light(base, n, spot, eye, ldir, attn, lcol);
    if (n == 1u) {
        g_pbr.fill = enh_lin(lcol);
    }
    float facing = diffuse == 1u ? dot(g_enh.N, ldir) : diffuse == 2u ? max(dot(g_enh.N, ldir), 0.0) : 1.0;
#if NATIVE_TRACED
    lcol *= traced_light(n);
    facing = traced_key_facing(n, diffuse, facing);
    facing = traced_fill_facing(n, diffuse, facing, g_enh.N);
#endif
    const float x = attn * facing;
    const float3 amb_lin = g_pbr.amb_lin;
    g_pbr.direct += lerp(enh_lin(amb + lcol * x) - amb_lin, (enh_lin(amb + lcol) - amb_lin) * x, kEnhRamp);
    if (first && diffuse != 0u && g_enh.valid) {
#if NATIVE_TRACED
        ldir = traced_key_dir(n, ldir);
#endif
        g_pbr.key = enh_lin(lcol) * attn;
        g_pbr.key_dir = ldir;
        g_pbr.has_key = true;
    }
}

// The diffuse light level, display-encoded: the traced form takes the colour under it as c / level (TRACED_LEVEL).
float3 pbr_level() {
    return enh_srgb(enh_shoulder(max(g_pbr.ambient + g_pbr.direct, 0.0)));
}

// The stage seen along R, linear, blurred by the roughness: its fill light as the sky above the horizon about the
// player's up, its ambient below; a rough surface sees their mean.
float3 pbr_environment(float3 R, float rough) {
    const float3 sky = g_pbr.amb_lin * (1.0 + kEnhHemi) + g_pbr.fill;
    const float3 ground = g_pbr.amb_lin * ((1.0 - kEnhHemi) * kPbrGroundShare);
    const float w = kPbrHorizon + rough;
    const float t = smoothstep(-w, w, dot(R, enh_up()));
    return lerp(lerp(ground, sky, t), 0.5 * (ground + sky), rough * rough);
}

// The reflection the material takes of the stage, display-encoded for ENV_FETCH: the environment by the split-sum
// environment BRDF (Karis analytic fit, "Physically Based Shading on Mobile", 2014) at F0 lerp(0.04, base, metal), by
// the specular occlusion.
float4 pbr_reflection(float3 base_srgb, float metal, float rough, float ao) {
    if (!g_enh.valid) {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    const float r = max(rough, kPbrRoughMin);
    const float3 f0 = lerp(float3(0.04, 0.04, 0.04), enh_lin(saturate(base_srgb)), metal);
    const float4 k = r * float4(-1.0, -0.0275, -0.572, 0.022) + float4(1.0, 0.0425, 1.04, -0.04);
    const float a004 = min(k.x * k.x, exp2(-9.28 * g_enh.NoV)) * k.x + k.y;
    const float2 ab = float2(-1.04, 1.04) * a004 + k.zw;
    // Lagarde's specular occlusion from the ambient occlusion and the view ("Moving Frostbite to PBR", 2014).
    const float so = saturate(pow(g_enh.NoV + ao, exp2(-16.0 * r - 1.0)) - 1.0 + ao);
    const float3 R = reflect(-g_enh.V, g_enh.Nv);
    return float4(saturate(enh_srgb(pbr_environment(R, r) * (f0 * ab.x + ab.y) * so)), 1.0);
}

// The lit colour, display-encoded, 0 to 1: the base colour (the stages' result) lit, plus the key highlight, plus the
// reflection (linear, as ENV_FETCH gave it back). Metal takes no diffuse light; a dielectric keeps what its Fresnel
// leaves.
float3 pbr_finish(float3 base_srgb, float metal, float rough, float ao, float3 reflection_srgb) {
    const float3 base = enh_lin(saturate(base_srgb));
    if (!g_enh.valid) {
        return saturate(base_srgb);
    }
    const float r = max(rough, kPbrRoughMin);
    const float3 f0 = lerp(float3(0.04, 0.04, 0.04), base, metal);
    float3 lit = base * (1.0 - metal) * (g_pbr.ambient * ao + g_pbr.direct);
    if (g_pbr.has_key) {
        const float NoL = saturate(dot(g_enh.Nv, g_pbr.key_dir));
        if (NoL > 0.0) {
            const float a2 = min(r * r * r * r + g_enh.a2_filter, 1.0);
            const float a = sqrt(a2);
            const float3 H = normalize(g_pbr.key_dir + g_enh.V);
            const float NoH = saturate(dot(g_enh.Nv, H));
            const float fw = pow(1.0 - saturate(dot(g_enh.V, H)), 5.0);
            const float3 F = f0 + (1.0 - f0) * fw;
            // A light colour is its irradiance over pi (the diffuse term is colour x N.L): the lobe takes pi.
            lit += g_pbr.key * F * (NoL * enh_ggx(NoH, a2) * enh_vis(NoL, g_enh.NoV, a) * 3.14159265);
        }
    }
    lit += enh_lin(saturate(reflection_srgb));
    return saturate(enh_srgb((view_flags & 4u) != 0u ? lit : enh_shoulder(lit)));
}

#endif  // PROGRAM_PBR_HLSLI
