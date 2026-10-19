// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The GX TEV program of a draw on the CPU, with the uber pixel shader's integer arithmetic (rt_tev.hlsli tev_evaluate
// is the GPU's copy): what a draw shows for given texture and rasterised colours. The ray tracer's de-baking solves
// with it for the light level at which a draw shows its texture as authored.

#include <array>
#include <cstdint>

#include "galaxy/gx/rt_types.h"

namespace galaxy::gx {

// The program's output (RGBA, 0-255) with every enabled texture reading `texel` (RGBA 0-255, before its stage's swap)
// and colour channels 0 and 1 reading `ras0` and `ras1` (RGBA 0-255, before the stages' swaps).
[[nodiscard]] std::array<int, 4> rt_tev_output(const RtTevMaterial& material, const RtTevStage* stages,
                                               const std::array<int, 4>& texel, const std::array<int, 4>& ras0,
                                               const std::array<int, 4>& ras1) noexcept;

// De-baking (GxBackend::debake_channels): the light level (0-255) at which colour channel `channel` makes the program
// show a mid-grey texture as itself (its luminance 128), the other channel as recorded. Unlit (`lit` false): the level
// of its grey vertex colour, the baked light; lit: the level of the light on its material (its vertex colour's hue,
// white, or its register). 255 where the program never brightens past it; 0 where it never darkens to it.
[[nodiscard]] std::uint32_t rt_tev_neutral_light(const RtTevMaterial& material, const RtTevStage* stages,
                                                 unsigned channel, bool lit) noexcept;

}  // namespace galaxy::gx
