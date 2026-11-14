// SPDX-License-Identifier: GPL-3.0-only
// A mod's programs on Direct3D 11 (mod_shaders.h): not yet. The Direct3D 11 renderer draws programs of the bound-table form
// in shader model 5.0, compiled at build (compile_programs.py dxbc); a mod that brings programs of its own is refused with
// these words and the rest of the mod loads without them.
#include "galaxy/render/mod_shaders.h"

namespace galaxy::render {

std::uint32_t mod_shader_compiler_revision() noexcept {
    return 0u;
}

bool mod_shader_has_bound_form() noexcept {
    return false;
}

std::string mod_shader_compiler_missing(const std::filesystem::path&) {
    return {};  // a mod's own programs are refused one by one (below), not every mod
}

std::vector<std::uint8_t> compile_mod_shader_stage(const std::string&, const std::filesystem::path&, bool,
                                                   const std::vector<const char*>&, std::string& error, bool) {
    error = "mods' own programs are not compiled for Direct3D 11 yet";
    return {};
}

}  // namespace galaxy::render
