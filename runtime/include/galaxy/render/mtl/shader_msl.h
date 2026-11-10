// SPDX-License-Identifier: GPL-3.0-only
// SPIR-V to the Metal Shading Language, through SPIRV-Cross (docs/APPLE.md, "Shaders"). The Metal renderer's shaders
// are the Vulkan renderer's: one HLSL source, compiled to SPIR-V by shader_compile_vk.cpp, then to MSL here, with
// each Vulkan binding at the Metal index below. Built into the build's tools (the programs' file, the utility
// shaders) and into the game for mods' materials.
#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace galaxy::render::mtl {

// A draw's buffers, the same in the vertex and the fragment function (Metal numbers each function's own):
// shader_compile_vk.cpp's draw bindings (set 0: b0, b1, b2, t16, t17, t18; push constants b3), then the two arrays of
// every texture and sampler (sets 1 and 2, as argument buffers) where the device reads them so, then the vertex streams.
inline constexpr std::uint32_t kBufferView = 0u;          // b0
inline constexpr std::uint32_t kBufferVertexRows = 1u;    // b1
inline constexpr std::uint32_t kBufferPixelRows = 2u;     // b2
inline constexpr std::uint32_t kBufferRows = 3u;          // t16
inline constexpr std::uint32_t kBufferInstances = 4u;     // t17
inline constexpr std::uint32_t kBufferTextureLists = 5u;  // t18
inline constexpr std::uint32_t kBufferDraw = 6u;          // b3
inline constexpr std::uint32_t kBufferTextureArray = 7u;  // set 1, every texture
inline constexpr std::uint32_t kBufferSamplerArray = 8u;  // set 2, every sampler
inline constexpr std::uint32_t kBufferStream0 = 10u;      // gpu::StreamSlot n at 10 + n

enum class MslStage { Vertex, Fragment, Compute };

struct MslOptions {
    bool ios = false;  // iOS and iPadOS (the simulator too), else macOS
    // The programs' bound-table form (program.hlsli NEBULA_BOUND_TABLES): sixteen textures and samplers at indices 0 to
    // 15. Else sets 1 and 2 are argument buffers at kBufferTextureArray and kBufferSamplerArray, their arrays without a
    // length (argument buffers tier 2).
    bool bound_tables = false;
    // A shader of the renderer's own (utility_spirv.h): every set's bindings are plain indices, binding n of set s at
    // buffer, texture or sampler index `set_stride * s + n`.
    bool utility = false;
    std::uint32_t set_stride = 8u;
};

// MSL for a SPIR-V module with one entry point (renamed `main0`); empty with `error` set when it cannot be made.
[[nodiscard]] std::string spirv_to_msl(std::span<const std::uint32_t> spirv, MslStage stage, const MslOptions& options,
                                       std::string& error);

// Changes with anything that changes spirv_to_msl's output for the same module.
inline constexpr std::uint32_t kMslRevision = 1u;

}  // namespace galaxy::render::mtl
