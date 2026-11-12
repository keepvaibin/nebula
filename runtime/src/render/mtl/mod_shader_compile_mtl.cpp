// SPDX-License-Identifier: GPL-3.0-only
// A mod's programs on Metal (mod_shaders.h): not yet. A mod's HLSL is compiled on the player's machine by the other
// renderers (Direct3D's compiler; Vulkan's HLSL front end); the Metal renderer's programs are made at build
// (docs/APPLE.md, "Shaders"), so a mod that brings programs of its own is refused with these words and the rest of
// the mod loads without them.
#include "galaxy/render/mod_shaders.h"

namespace galaxy::render {

std::uint32_t mod_shader_compiler_revision() noexcept {
    return 0u;
}

bool mod_shader_has_bound_form() noexcept {
    return false;
}

std::string mod_shader_compiler_missing(const std::filesystem::path&) {
    return {};  // nothing compiles a mod's programs here yet: each is refused (compile_mod_shader_stage), not every mod
}

std::vector<std::uint8_t> compile_mod_shader_stage(const std::string&, const std::filesystem::path&, bool,
                                                   const std::vector<const char*>&, std::string& error, bool) {
    error = "mods' own programs are not compiled on Apple devices yet";
    return {};
}

}  // namespace galaxy::render
