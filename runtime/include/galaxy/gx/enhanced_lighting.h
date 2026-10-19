// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Enhanced lighting (RenderConfig::enhanced_lighting): the game's own lights and materials shaded
// per pixel. A lit colour channel keeps its ambient (shadow side) and ambient-plus-light (fully lit)
// colours; each light keeps its authored falloff and the lights add in linear light under a soft
// shoulder instead of the channel clamp. The ambient is the sky: a hemisphere about the player's up
// (the game's gravity; the view's up without it), and its reflection off the surface (Fresnel with
// the surface's roughness, Karis' fit of the split-sum lookup) takes its share from the diffuse
// light. The key light's highlight is GGX at physical strength, its lobe widened by the normal's
// change across the pixel (Tokuyoshi and Kaplanyan's specular antialiasing). Environment-mapped
// materials (the game's metals, crystals and ice) get a Fresnel gain on their environment lookups and
// a metal highlight in their own colour. Unlit, non-environment-mapped draws (baked map geometry)
// are unchanged; ambient occlusion (ambient_occlusion.h) darkens them with the rest of the scene.

#include "galaxy/gx/shader_keys.h"

#include <cstdint>

namespace galaxy::gx {

struct EnhancedLightingTuning {
    float ramp = 0.0f;          // a light's falloff: 0 as authored (gamma space), 1 linear Lambert
    float hemi = 0.30f;         // ambient: 1 + hemi * N.up, mean 1 over the sphere
    float sky = 1.0f;           // the sky's reflection: 1 physical (F0 0.04 at the surface's roughness)
    float knee = 0.80f;         // linear level where the shoulder starts
    float sheen = 3.14159265f;  // dielectric highlight: pi for a light's colour as GX's irradiance / pi
    float sheen_rough = 0.45f;  // its perceptual roughness without a material table entry
    float metal = 3.14159265f;  // metal highlight (F0 the surface colour)
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
struct RtMaterial;
// The material table's roughness and metalness for an enhanced draw's uber header (kEnhancedTable*;
// 0 without an entry or without either value).
[[nodiscard]] std::uint32_t enhanced_table_material(const RtMaterial* material) noexcept;
[[nodiscard]] std::uint32_t enhanced_lighting_material(const VertexShaderKey& vs,
                                                       const PixelShaderKey& ps) noexcept;

}  // namespace galaxy::gx
