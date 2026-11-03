// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Shaders a converted mod brings (a .luma's shaders/ folder): the renderer asks native_shader_bytes for a material, and a
// number past the built-in set is answered from here. One file per graphics interface, compiled when the mod was converted.
//   Header: "NBSHADER", version u32, count u32. Entry: material number u32 (as a cooked file holds it), variant u8,
//   stage u8 (0 vertex, 1 pixel), 2 reserved, offset u64 from the file's start, size u64.

#include "galaxy/render/native_shaders.h"

#include <filesystem>

namespace galaxy::render {

// Loads one file before the renderer starts; false (and nothing added) for a file that is not one or does not exist.
bool load_mod_shaders(const std::filesystem::path& file);
[[nodiscard]] NativeShaderBytes mod_shader_bytes(std::uint32_t shader, unsigned variant, bool pixel) noexcept;
// The file of this build's graphics interface in a mod's shaders/ folder: "dxil.shaders" or "spirv.shaders".
[[nodiscard]] const char* native_shader_file_name() noexcept;

}  // namespace galaxy::render
