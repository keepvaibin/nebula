// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Programs a converted mod brings (a .luma's shaders/ folder): the program table asks here for an id it doesn't hold. One file
// per graphics interface, compiled when the mod was converted.
//   Header: "NBSHADER", version u32, count u32, the program interface it was compiled for (40 hexadecimal digits).
//   Entry: program id u64 (its text's hash), variant u8, stage u8 (0 vertex, 1 pixel), 6 reserved, offset u64 from the file's
//   start, size u64.

#include "galaxy/render/program_table.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace galaxy::render {

enum class ModShadersResult { Loaded, Unreadable, OtherInterface };
// Loads one file before the renderer starts; anything else adds nothing. OtherInterface: compiled for a build whose program
// headers differ: the mod must be converted again.
ModShadersResult load_mod_shaders(const std::filesystem::path& file);
[[nodiscard]] ProgramBytes mod_program_bytes(std::uint64_t id, unsigned variant, bool pixel) noexcept;
// What building a mod's missing programs came to.
struct ModShaderBuild {
    unsigned wanted = 0;  // programs neither the build nor `brought` holds
    unsigned built = 0;
    std::string problem;  // the first thing that went wrong
};
// Every program in `sources` (the cooker's v<hash>.hlsl and p<hash>.hlsl, and the headers they include) that neither this build
// nor a file of `brought` holds, built into `output` in this format.
ModShaderBuild build_missing_mod_shaders(const std::filesystem::path& sources, const std::vector<std::filesystem::path>& brought,
                                         const std::filesystem::path& output, const std::function<void(unsigned, unsigned)>& progress);
// What, besides the program interface, decides compile_mod_shader_stage's bytes for the same text: Vulkan's HLSL front
// end and its location rules (vk::kCompilerRevision); 0 for D3D12, whose programs bind by name. Shaders built for
// mods on this device are built again when it changes.
[[nodiscard]] std::uint32_t mod_shader_compiler_revision() noexcept;
// Vulkan: a pixel program is also built in its bound-table form (program.hlsli NEBULA_BOUND_TABLES), for a device without
// descriptor indexing, stored at the variant + kProgramBoundOffset. The cooking machine need not be the one that plays.
[[nodiscard]] bool mod_shader_has_bound_form() noexcept;
// Whether the compiler compile_mod_shader_stage uses is here and runs (contract start-check, section 2): "" when it is,
// else what is missing, in English for the developer's log. Direct3D 12: Microsoft's dxc beside the game, its version run
// once per install of it (the files' sizes and times kept in `cache_folder`); Vulkan: its front end is linked in, always
// ""; Metal: "" (a mod's own programs are refused one by one there, not every mod).
[[nodiscard]] std::string mod_shader_compiler_missing(const std::filesystem::path& cache_folder);
// One stage of a program for this build's graphics interface; empty, with the reason, when it can't be. `bound_tables`: the
// bound-table form (only where mod_shader_has_bound_form).
std::vector<std::uint8_t> compile_mod_shader_stage(const std::string& source, const std::filesystem::path& includes, bool pixel,
                                                   const std::vector<const char*>& defines, std::string& problem,
                                                   bool bound_tables = false);

}  // namespace galaxy::render
