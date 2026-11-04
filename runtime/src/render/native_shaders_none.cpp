// SPDX-License-Identifier: GPL-3.0-only
// Builds without the native materials' compiled shaders (platform/sdl until its build compiles them): none, so their
// draws are skipped.
#include "galaxy/render/mod_shaders.h"
#include "galaxy/render/native_shaders.h"

namespace galaxy::render {

unsigned native_draw_variant(std::uint32_t, unsigned variant, unsigned, bool, bool) noexcept {
    return variant;
}

NativeShaderBytes native_shader_bytes(std::uint32_t, unsigned, bool) noexcept {
    return {};
}

const char* native_shader_file_name() noexcept {
    return "none.shaders";
}

const char* native_shader_interface() noexcept {
    return "";
}

}  // namespace galaxy::render
