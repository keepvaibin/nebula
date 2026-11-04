// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The pipelines the game's stages use (shader and render state), generated from tours by tools/dev/pipeline_seed.py:
// both backends build them on background threads at start, so a stage's first frames compile nothing.
#include "galaxy/render/render_state_key.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace galaxy::render {

struct PipelineSeed {
    std::uint32_t shader;
    std::uint32_t blend_bits;
    std::uint16_t zmode_bits;
    std::uint8_t cull, pixfmt, primitive_topology;
};

[[nodiscard]] std::span<const PipelineSeed> pipeline_seeds() noexcept;
[[nodiscard]] inline RenderStateKey seed_render_state(const PipelineSeed& seed) noexcept {
    RenderStateKey rs{};
    rs.blend_bits = seed.blend_bits;
    rs.zmode_bits = seed.zmode_bits;
    rs.cull = seed.cull;
    rs.pixfmt = seed.pixfmt;
    rs.primitive_topology = seed.primitive_topology;
    // Only a texture target's pass keeps alpha.
    rs.samples_log2 = seed.pixfmt == 1u ? kSceneVariantTarget : std::uint8_t{0u};
    return rs;
}
// GALAXY_PIPELINE_SEED_OUT=<file>: each new pipeline a draw takes, appended as a seed row (the generator's input).
void note_pipeline_for_seed(std::uint32_t shader, const RenderStateKey& rs) noexcept;

}  // namespace galaxy::render
