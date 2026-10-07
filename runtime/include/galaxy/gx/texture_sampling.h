#pragma once

#include "galaxy/gx/gx_state.h"
#include "galaxy/gx/uber_constants.h"

#include <algorithm>
#include <cstdint>

namespace galaxy::gx {

// Adapted from Aurora (MIT, encounter/aurora@77326d4, lib/gx/shader_info.cpp,
// build_shader_info), which adds each used indirect stage's texmap to the
// sampled texture set. The specialized shader samples every configured
// indirect stage, so each needs a binding and a memory snapshot even when no
// TEV stage names its map.
[[nodiscard]] inline std::uint8_t sampled_texture_map_mask(
    const GxState& state) noexcept {
    const GenMode mode = state.gen_mode();
    std::uint8_t mask = 0;
    unsigned indirect_stages = std::min<unsigned>(mode.num_ind_stages, 4u);
    for (unsigned stage = 0; stage < mode.num_tev_stages; ++stage) {
        const std::uint32_t command = state.bp(
            static_cast<std::uint8_t>(bp::kIndCmdBase + stage));
        if (command != 0u) {
            indirect_stages = std::max<unsigned>(
                indirect_stages, (command & 0x3u) + 1u);
        }
    }
    indirect_stages = std::min(indirect_stages, 4u);

    const bool z_texture = ((state.bp(bp::kTevZEnv1) >> 2u) & 0x3u) != 0u;
    for (unsigned stage = 0; stage < mode.num_tev_stages; ++stage) {
        const TevOrder order = state.tev_order(stage);
        if (mode.num_texgens == 0u || !order.tex_enable ||
            order.texmap >= kMaxTextureMaps) {
            continue;
        }
        // Match PS-key canonicalization: when z-textures and indirect stages
        // are absent, a sample contributes only through a TEXC/TEXA combiner
        // input. Binding/snapshotting leftover enabled maps otherwise still
        // decodes unused images even though their shader samples are dead.
        // Keep every configured direct map for either exceptional path:
        // z-textures use the last raw sample; indirect coordinates carry state
        // between stages independently of their color/alpha inputs.
        bool needed = z_texture || indirect_stages != 0u;
        if (!needed) {
            const std::uint32_t color = state.bp(static_cast<std::uint8_t>(
                bp::kTevColorEnvBase + 2u * stage));
            const std::uint32_t alpha = state.bp(static_cast<std::uint8_t>(
                bp::kTevAlphaEnvBase + 2u * stage));
            for (unsigned input = 0; input < 4u; ++input) {
                const auto color_arg = static_cast<TevColorArg>(
                    (color >> (input * 4u)) & 15u);
                const auto alpha_arg = static_cast<TevAlphaArg>(
                    (alpha >> (4u + input * 3u)) & 7u);
                if (color_arg == TevColorArg::TexColor ||
                    color_arg == TevColorArg::TexAlpha ||
                    alpha_arg == TevAlphaArg::TexAlpha) {
                    needed = true;
                    break;
                }
            }
        }
        if (needed) {
            mask |= static_cast<std::uint8_t>(1u << order.texmap);
        }
    }
    const std::uint32_t refs = state.bp(bp::kIndRef);
    for (unsigned stage = 0; stage < indirect_stages; ++stage) {
        const unsigned texmap = (refs >> (stage * 6u)) & 0x7u;
        mask |= static_cast<std::uint8_t>(1u << texmap);
    }
    return mask;
}

}  // namespace galaxy::gx
