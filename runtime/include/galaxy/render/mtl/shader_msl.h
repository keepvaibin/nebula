// SPDX-License-Identifier: GPL-3.0-only
// SPIR-V to the Metal Shading Language, through SPIRV-Cross. The Metal renderer's shaders
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

// The tracer's passes (rt_bindings.hlsli; set 0 as compile_rt_shaders.py places the registers: t at its number, s at
// 100, u at 200, b at 300, the bindings rt_backend_vk.cpp's layout makes). Buffers:
inline constexpr std::uint32_t kRtBufferScene = 0u;         // t0, the top-level structure
inline constexpr std::uint32_t kRtBufferRenderer = 1u;      // t1-t4 at 1-4
inline constexpr std::uint32_t kRtBufferMesh = 5u;          // t21-t29 at 5-13
inline constexpr std::uint32_t kRtBufferCounters = 14u;     // u8
inline constexpr std::uint32_t kRtBufferWritten = 15u;      // u9
inline constexpr std::uint32_t kRtBufferFrame = 16u;        // b0
inline constexpr std::uint32_t kRtBufferPass = 17u;         // b1
inline constexpr std::uint32_t kRtBufferTextureArray = 18u; // set 1 (t0 in space1): every texture, an argument buffer
inline constexpr std::uint32_t kRtBufferProbeOut = 19u;     // u0 where it is a buffer (rt_probe.hlsl)
// Textures:
inline constexpr std::uint32_t kRtTextureRenderer = 0u;     // t5-t8 at 0-3
inline constexpr std::uint32_t kRtTextureRead = 4u;         // t9-t20 at 4-15
inline constexpr std::uint32_t kRtTextureWritten = 16u;     // u0-u7 at 16-23
// s0 is a sampler of the text's own (linear, repeating, as rt_backend_vk.cpp's): nothing binds it.

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
    // A shader of the tracer's (rt_shaders.h, compute): the kRt* indices, set 1 an argument buffer of a runtime array,
    // MSL 3.1 (macOS 14, iOS 17: the tracer's instances need them, rt_backend_mtl.mm).
    bool tracer = false;
};

// MSL for a SPIR-V module with one entry point (renamed `main0`); empty with `error` set when it cannot be made.
[[nodiscard]] std::string spirv_to_msl(std::span<const std::uint32_t> spirv, MslStage stage, const MslOptions& options,
                                       std::string& error);

// Changes with anything that changes spirv_to_msl's output for the same module.
inline constexpr std::uint32_t kMslRevision = 1u;

}  // namespace galaxy::render::mtl
