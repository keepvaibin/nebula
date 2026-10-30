#pragma once

// The set of texture maps a draw samples, from its TEV and indirect state.

#include "galaxy/render/draw_state.h"
#include "galaxy/render/uber_constants.h"

#include <algorithm>
#include <cstdint>

namespace galaxy::render {

// Adapted from Aurora (MIT, encounter/aurora@77326d4, lib/gx/shader_info.cpp, build_shader_info): the shader
// samples every configured indirect stage, so each needs a binding and a memory snapshot even when no TEV stage
// names its map.
[[nodiscard]] inline std::uint8_t sampled_texture_map_mask(
    const NativeDrawState& state) noexcept {
    const GenMode mode = state.gen;
    std::uint8_t mask = 0;
    unsigned indirect_stages = std::min<unsigned>(mode.num_ind_stages, 4u);
    for (unsigned stage = 0; stage < mode.num_tev_stages; ++stage) {
        const std::uint32_t command = ind_cmd_word(state.ind[stage]);
        if (command != 0u) {
            indirect_stages = std::max<unsigned>(
                indirect_stages, (command & 0x3u) + 1u);
        }
    }
    indirect_stages = std::min(indirect_stages, 4u);

    const bool z_texture = state.z_texture.op != 0u;
    for (unsigned stage = 0; stage < mode.num_tev_stages; ++stage) {
        const TevStageConfig& config = state.tev[stage];
        const TevOrder order = config.order;
        if (mode.num_texgens == 0u || !order.tex_enable ||
            order.texmap >= kMaxTextureMaps) {
            continue;
        }
        // Matches the pixel-shader key's canonicalization. Without z-textures or indirect stages a sample
        // contributes only through a TEXC/TEXA combiner input, so other enabled maps are not bound (that would
        // decode images whose samples are dead). With either, every enabled map is kept: z-textures use the last
        // raw sample, and indirect coordinates carry state between stages regardless of the combiner inputs.
        bool needed = z_texture || indirect_stages != 0u;
        if (!needed) {
            const TevColorArg colors[4] = {config.color_a, config.color_b, config.color_c, config.color_d};
            const TevAlphaArg alphas[4] = {config.alpha_a, config.alpha_b, config.alpha_c, config.alpha_d};
            for (unsigned input = 0; input < 4u; ++input) {
                if (colors[input] == TevColorArg::TexColor || colors[input] == TevColorArg::TexAlpha ||
                    alphas[input] == TevAlphaArg::TexAlpha) {
                    needed = true;
                    break;
                }
            }
        }
        if (needed) {
            mask |= static_cast<std::uint8_t>(1u << order.texmap);
        }
    }
    for (unsigned stage = 0; stage < indirect_stages; ++stage) {
        mask |= static_cast<std::uint8_t>(1u << state.ind_order[stage].map);
    }
    return mask;
}

}  // namespace galaxy::render
