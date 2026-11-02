// SPDX-License-Identifier: GPL-3.0-only
// The Vulkan renderer's shaders that are not materials, compiled at build (E-19): utility_spirv_table.inc,
// post_programs_vk.inc; tools/dev/utility_spirv.cpp.
#pragma once

#include <cstdint>
#include <span>

namespace galaxy::render::vk {

enum class UtilitySpirv {
#define X(name, source, stage, entry, defines) name,
#include "galaxy/render/vk/utility_spirv_table.inc"
#undef X
};

[[nodiscard]] std::span<const std::uint32_t> utility_spirv(UtilitySpirv shader) noexcept;

// A presentation pass's program (PostProcessorVk::Program) at an SMAA quality, 0-3.
[[nodiscard]] std::span<const std::uint32_t> post_spirv(unsigned program, unsigned smaa_quality) noexcept;

}  // namespace galaxy::render::vk
