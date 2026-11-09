// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The game's compiled programs (tools/dev/compile_programs.py): named by a generated text's hash, or for a written shader
// NEBULA_PROGRAM_SITE | shader << 1 (a vertex program's | 1 is its widened lines and points' form).

#include <cstddef>
#include <cstdint>

namespace galaxy::render {

struct ProgramBytes {
    const void* data = nullptr;
    std::size_t size = 0u;
};

enum ProgramVariant : unsigned {
    kProgramBase = 0u,
    kProgramReactive = 1u,  // + the reactive mask
    kProgramLayer = 2u,     // + the reactive mask and the effects layer's targets
    kProgramMotion = 3u,    // + the motion vector
    kProgramSample = 4u,    // an alpha-tested program under MSAA, per sample
    kProgramVariants = 5u,
    // A Vulkan device without descriptor indexing runs each pixel program in a second form, reading its draw's bound table
    // (program.hlsli NEBULA_BOUND_TABLES): stored beside the others at this offset.
    kProgramBoundOffset = 5u,
    kProgramStored = 10u,
};

// The form program_bytes returns for pixel programs: true, the bound-table form (a Vulkan device without descriptor
// indexing, set once the device is chosen); false, the bindless form (Direct3D 12, and Vulkan with descriptor indexing).
void program_use_bound_tables(bool bound) noexcept;
[[nodiscard]] bool program_bound_tables() noexcept;

// `aux`: the draw's targets beside the colour (0 none, 1 motion, 2 reactive, 3 layer).
[[nodiscard]] unsigned program_variant(std::uint64_t pixel, unsigned aux, unsigned samples_log2) noexcept;
// Empty when the build has none. A vertex program has only the base and motion forms: others read those.
[[nodiscard]] ProgramBytes program_bytes(std::uint64_t id, unsigned variant, bool pixel) noexcept;
// Its text reads the block's world rows.
[[nodiscard]] bool program_reads_world(std::uint64_t vertex) noexcept;
// The texture coordinates a written program's vertex stage reads (its NATIVE_COORDS); 0 for a generated program, whose
// inputs its form names.
[[nodiscard]] std::uint32_t program_coords(std::uint64_t vertex) noexcept;
// It lights at the pixel: a form drawn only under per-pixel or enhanced lighting.
[[nodiscard]] bool program_lit(std::uint64_t pixel) noexcept;
// How many programs the build holds, for the census.
[[nodiscard]] std::uint32_t program_count(bool pixel) noexcept;

// The file of this build's graphics interface in a mod's shaders folder: "dxil.shaders" or "spirv.shaders".
[[nodiscard]] const char* program_file_name() noexcept;
// SHA-1 of the programs' headers.
[[nodiscard]] const char* program_interface() noexcept;

}  // namespace galaxy::render
