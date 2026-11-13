// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The game's compiled programs (tools/dev/compile_programs.py): named by a generated text's hash, or for a written shader
// NEBULA_PROGRAM_SITE | shader << 1 (a vertex program's | 1 is its widened lines and points' form).

#include <cstddef>
#include <cstdint>
#include <vector>

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
    // Ray tracing's forms of a pixel program, after the bound-table forms and never in one: its prepass (its item and
    // depth, nothing else) and its traced forms (its colour and lights by the light pass's shares).
    kProgramPrepass = 10u,
    kProgramTraced = 11u,
    kProgramTracedMotion = 12u,
    kProgramTracedReactive = 13u,  // a blended draw's: + the reactive mask
    kProgramTracedLayer = 14u,     // + the reactive mask and the effects layer's targets
    kProgramPrepassItem = 15u,     // the prepass naming its item only (Vulkan without geometry shaders: D11)
    kProgramStored = 16u,
};

// A scene draw's "aux": the targets it writes beside the colour (0 none, 1 the motion, 2 the reactive mask, 3 that and
// the effects layer, 4 the motion target bound and left alone), and with ray tracing on the tracer's forms:
inline constexpr unsigned kAuxPrepass = 5u;        // the prepass: depth and the item target, no colour
inline constexpr unsigned kAuxPrepassMotion = 6u;  // the same through the vertex stage's motion form (a draw whose own
                                                   // aux is 1: one vertex stage, so one depth, in both passes)
inline constexpr unsigned kAuxTraced = 8u;         // + 0 to 4: that aux's targets, the pixel program's traced form
// With kAuxTraced: an added clear shell's traced form, drawn premultiplied (FB b94, EVERYTHING 6.7: the exchange; the
// pipelines' third page set).
inline constexpr unsigned kAuxExchange = 16u;
// The vertex stage's motion form (and its stream of last frame's positions).
[[nodiscard]] constexpr bool aux_motion_form(unsigned aux) noexcept {
    return aux == 1u || aux == kAuxPrepassMotion || aux == (kAuxTraced | 1u);
}
// The targets an aux binds: 0-4, or kAuxPrepass.
[[nodiscard]] constexpr unsigned aux_targets(unsigned aux) noexcept {
    return aux >= kAuxTraced ? aux & 7u : aux == kAuxPrepassMotion ? kAuxPrepass : aux;
}

// The form program_bytes returns for pixel programs: true, the bound-table form (a Vulkan device without descriptor
// indexing, set once the device is chosen); false, the bindless form (Direct3D 12, and Vulkan with descriptor indexing).
void program_use_bound_tables(bool bound) noexcept;
[[nodiscard]] bool program_bound_tables() noexcept;
// The prepass reads its triangle's number (true: Direct3D 12, and Vulkan with geometry shaders); false, the prepass
// takes the form that names its item only and the surface pass finds the triangle by a ray (D11).
void program_use_triangle_ids(bool ids) noexcept;

// The pixel program's form for a draw of `aux`.
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
// It sums its light set's key light at the pixel: the sun's traced share shadows it through the program's own sum.
[[nodiscard]] bool program_sums_key(std::uint64_t pixel) noexcept;
// It has the traced forms (every program of this build; a mod's built before them may not).
[[nodiscard]] bool program_traced(std::uint64_t pixel) noexcept;

// The file of this build's graphics interface in a mod's shaders folder: "dxil.shaders" or "spirv.shaders".
[[nodiscard]] const char* program_file_name() noexcept;
// SHA-1 of the programs' headers.
[[nodiscard]] const char* program_interface() noexcept;

// A dev run's file (FA l64): with --diagnostics, dev/<name> beside the program, whole, into `into`; false (and `into`
// empty) without --diagnostics or the file. Used for dev/programs.bin and dev/rt_shaders.bin (tools/f_stage.sh).
[[nodiscard]] bool dev_shader_file(const char* name, std::vector<unsigned char>& into);

}  // namespace galaxy::render
