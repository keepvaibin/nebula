#pragma once

#include "galaxy/gx/gx_state.h"
#include "galaxy/gx/uber_constants.h"

#include <algorithm>
#include <cstdint>

namespace galaxy::gx {

// Aurora (MIT, encounter/aurora@77326d4, lib/gx/shader_info.cpp,
// build_shader_info) includes each used indirect stage's texmap in its sampled
// texture set. Adapt that rule to the raw BP state and to the stages emitted
// by Galaxy's specialized shader generator. The shader currently samples all
// configured indirect stages, so each one needs a binding and a memory
// snapshot even when no ordinary TEV stage names its map.
[[nodiscard]] inline std::uint8_t sampled_texture_map_mask(
    const GxState& state) noexcept {
    const GenMode mode = state.gen_mode();
    std::uint8_t mask = 0;
    for (unsigned stage = 0; stage < mode.num_tev_stages; ++stage) {
        const TevOrder order = state.tev_order(stage);
        if (mode.num_texgens != 0u && order.tex_enable && order.texmap < kMaxTextureMaps) {
            mask |= static_cast<std::uint8_t>(1u << order.texmap);
        }
    }

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
    const std::uint32_t refs = state.bp(bp::kIndRef);
    for (unsigned stage = 0; stage < indirect_stages; ++stage) {
        const unsigned texmap = (refs >> (stage * 6u)) & 0x7u;
        mask |= static_cast<std::uint8_t>(1u << texmap);
    }
    return mask;
}

}  // namespace galaxy::gx
