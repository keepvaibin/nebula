// SPDX-License-Identifier: GPL-3.0-only
// Builds without the native materials' compiled shaders (platform/sdl until its build compiles them): none, so their
// draws are skipped.
#include "galaxy/render/native_shaders.h"

namespace galaxy::render {

NativeShaderBytes native_shader_bytes(std::uint32_t, unsigned, bool) noexcept {
    return {};
}

NativeShaderBytes native_curved_bytes(bool, bool) noexcept {
    return {};
}

}  // namespace galaxy::render
