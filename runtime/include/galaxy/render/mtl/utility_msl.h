// SPDX-License-Identifier: GPL-3.0-only
// The Metal renderer's shaders that are not materials: the Vulkan renderer's (vk/utility_spirv_table.inc,
// vk/post_programs_vk.inc), turned to MSL at build (platform/apple/tools/utility_msl.cpp). Each is one function named
// `main0`. Their bindings are plain indices: binding n of Vulkan's set s is buffer, texture or sampler index
// kUtilitySetStride * s + n in that function, and their push constants are the buffer at kUtilityPushBuffer.
#pragma once

#include <cstdint>

namespace galaxy::render::mtl {

inline constexpr std::uint32_t kUtilitySetStride = 8u;
inline constexpr std::uint32_t kUtilityPushBuffer = 24u;

enum class UtilityShader {
#define X(name, source, stage, entry, defines) name,
#include "galaxy/render/vk/utility_spirv_table.inc"
#undef X
    Count
};

enum class UtilityStage : std::uint8_t { Vertex, Fragment, Compute };

// The shader's MSL text (zero-terminated) and its stage.
[[nodiscard]] const char* utility_msl(UtilityShader shader) noexcept;
[[nodiscard]] UtilityStage utility_stage(UtilityShader shader) noexcept;

// A presentation pass's program (post_programs_vk.inc's order) at an SMAA quality, 0-3, and its stage.
[[nodiscard]] const char* post_msl(unsigned program, unsigned smaa_quality) noexcept;
[[nodiscard]] UtilityStage post_stage(unsigned program) noexcept;
[[nodiscard]] unsigned post_program_count() noexcept;

}  // namespace galaxy::render::mtl
