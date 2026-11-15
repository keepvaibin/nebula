// SPDX-License-Identifier: GPL-3.0-only
// One stage of a mod's material as SPIR-V, through the renderer's own HLSL front end, as galaxy_native_spirv does at build.
#include "galaxy/render/mod_post.h"
#include "galaxy/render/mod_shaders.h"
#include "galaxy/render/vk/shader_compile_vk.h"

#include <cstring>

namespace galaxy::render {

std::uint32_t mod_shader_compiler_revision() noexcept {
    return vk::kCompilerRevision;
}

bool mod_shader_has_bound_form() noexcept {
    return true;
}

std::vector<std::uint8_t> compile_mod_shader_stage(const std::string& source, const std::filesystem::path& includes, bool pixel,
                                                   const std::vector<const char*>& defines, std::string& problem, bool bound_tables) {
    const std::vector<std::uint32_t> spirv = vk::compile_mod_stage(source, includes, pixel, defines, problem, bound_tables);
    std::vector<std::uint8_t> out(spirv.size() * sizeof(std::uint32_t));
    if (!out.empty()) {
        std::memcpy(out.data(), spirv.data(), out.size());
    }
    return out;
}

std::vector<std::uint8_t> compile_mod_post_stage(const std::string& source, const std::filesystem::path&, bool pixel,
                                                 std::string& problem) {
    // The prelude declares its own bindings (mod_post.h): no annotate, no draw layout. The entry is "main", as every
    // module the renderer makes pipelines from.
    namespace vk = galaxy::render::vk;
    const std::string head = pixel ? "#define program_ps main\n" : "#define program_vs main\n";
    const std::vector<std::uint32_t> spirv = vk::compile_hlsl(head + source, pixel ? vk::ShaderStage::Fragment : vk::ShaderStage::Vertex,
                                                              "main", {}, problem);
    std::vector<std::uint8_t> out(spirv.size() * sizeof(std::uint32_t));
    if (!out.empty()) {
        std::memcpy(out.data(), spirv.data(), out.size());
    }
    return out;
}

}  // namespace galaxy::render
