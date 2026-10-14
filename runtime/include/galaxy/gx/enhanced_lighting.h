// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Enhanced lighting (RenderConfig::enhanced_lighting): the game's own lights and materials shaded
// per pixel. A lit colour channel keeps its ambient (shadow side) and ambient-plus-light (fully lit)
// colours; each light adds in linear light, along a blend of its authored falloff and the linear
// Lambert ramp between those ends, under a soft shoulder instead of the channel clamp; the ambient
// becomes a sky/ground hemisphere with a sky rim; lit draws get a dielectric highlight of their key
// light; environment-mapped materials (the game's metals, crystals and ice) get a Fresnel gain on
// their environment lookups and a metal highlight tinted by their own colour. Unlit,
// non-environment-mapped draws (baked map geometry) are unchanged.

#include "galaxy/gx/shader_keys.h"

#include <cstdint>

namespace galaxy::gx {

struct EnhancedLightingTuning {
    float ramp = 0.35f;         // a light's falloff: 0 as authored (gamma space), 1 linear Lambert
    float hemi = 0.30f;         // ambient: 1 + hemi * N.up (view up), mean 1 over the sphere
    float rim = 1.0f;           // sky rim: ambient * rim * (1 - N.V)^3 on the upper half
    float knee = 0.80f;         // linear level where the shoulder starts
    float sheen = 1.5f;         // dielectric highlight strength (F0 0.04)
    float sheen_rough = 0.45f;  // its perceptual roughness
    float metal = 0.70f;        // metal highlight strength (F0 the surface colour)
    float metal_rough = 0.35f;
    float metal_f0 = 0.50f;     // least peak of a metal's F0 (its colour raised in its own hue)
    float env_base = 1.0f;      // environment lookup gain facing the viewer
    float env_graze = 0.80f;    // gain added at grazing angles, Schlick (1 - N.V)^5
    std::uint32_t hash = 0u;    // nonzero when GALAXY_ENHANCED_LIGHTING_TUNE changes a value
};

// The tuning, parsed once from GALAXY_ENHANCED_LIGHTING_TUNE ("name=value,..." with the member
// names above); the defaults without it.
[[nodiscard]] const EnhancedLightingTuning& enhanced_lighting_tuning();

// The ind_reserved material word of an enhanced draw (shader_keys.h kEnhanced*), zero when the
// draw is neither lit nor environment-mapped (enhanced lighting leaves it as the game draws it).
// The keys are the draw's own, before the enhanced bits are set.
[[nodiscard]] std::uint32_t enhanced_lighting_material(const VertexShaderKey& vs,
                                                       const PixelShaderKey& ps) noexcept;

}  // namespace galaxy::gx
