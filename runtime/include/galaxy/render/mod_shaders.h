// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Shaders a converted mod brings (a .luma's shaders/ folder): the renderer asks native_shader_bytes for a material, and a
// number past the built-in set is answered from here. One file per graphics interface, compiled when the mod was converted.
//   Header: "NBSHADER", version u32, count u32, the shader interface it was compiled for (40 hexadecimal digits). Entry:
//   material number u32 (as a cooked file holds it), variant u8, stage u8 (0 vertex, 1 pixel), 2 reserved, offset u64 from
//   the file's start, size u64.

#include "galaxy/render/native_shaders.h"

#include <filesystem>

namespace galaxy::render {

enum class ModShadersResult { Loaded, Unreadable, OtherInterface };
// Loads one file before the renderer starts; anything else adds nothing. OtherInterface: compiled for a build whose shader
// interface differs (the headers changed since): the mod must be converted again.
ModShadersResult load_mod_shaders(const std::filesystem::path& file);
[[nodiscard]] NativeShaderBytes mod_shader_bytes(std::uint32_t shader, unsigned variant, bool pixel) noexcept;
// The file of this build's graphics interface in a mod's shaders/ folder: "dxil.shaders" or "spirv.shaders".
[[nodiscard]] const char* native_shader_file_name() noexcept;
// SHA-1 of this build's material headers.
[[nodiscard]] const char* native_shader_interface() noexcept;

}  // namespace galaxy::render
