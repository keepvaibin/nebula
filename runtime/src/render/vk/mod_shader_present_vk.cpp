// SPDX-License-Identifier: GPL-3.0-only
// Whether the compiler of mods' programs is there, for the start check (mod_start_check.cpp), on Vulkan: the HLSL front
// end (shader_compile_vk.cpp) is linked into the windowed program, so it always is. Apart from the front end so a
// program without it (nebula_native_host on Linux and Android) links the same answer.
#include "galaxy/render/mod_shaders.h"

namespace galaxy::render {

std::string mod_shader_compiler_missing(const std::filesystem::path&) {
    return {};
}

}  // namespace galaxy::render
