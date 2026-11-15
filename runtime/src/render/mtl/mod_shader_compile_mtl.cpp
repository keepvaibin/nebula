// SPDX-License-Identifier: GPL-3.0-only
// One stage of a mod's program on Metal (mod_shaders.h): the Vulkan renderer's HLSL front end (shader_compile_vk.cpp,
// linked into the Metal renderer for this) makes the SPIR-V, SPIRV-Cross turns it to MSL with the options the game's own
// programs get at build (platform/apple/tools/programs_msl.cpp), and the text is what is stored: pipelines_mtl.mm
// function_of compiles it (`main0`) when a draw first needs it, as it does the game's texts without Xcode at build.
#include "galaxy/render/mod_shaders.h"
#include "galaxy/render/mtl/shader_msl.h"
#include "galaxy/render/vk/shader_compile_vk.h"

#include <TargetConditionals.h>

namespace galaxy::render {

std::uint32_t mod_shader_compiler_revision() noexcept {
    // The front end's revision and SPIRV-Cross's, apart: a change in either builds the stored programs again.
    return vk::kCompilerRevision * 256u + mtl::kMslRevision;
}

bool mod_shader_has_bound_form() noexcept {
    // A GPU with argument buffers tier 1 (or before macOS 13 and iOS 16) draws the bound-table form.
    return true;
}

std::string mod_shader_compiler_missing(const std::filesystem::path&) {
    return {};  // the front end and SPIRV-Cross are linked in
}

std::vector<std::uint8_t> compile_mod_shader_stage(const std::string& source, const std::filesystem::path& includes, bool pixel,
                                                   const std::vector<const char*>& defines, std::string& problem, bool bound_tables) {
    const std::vector<std::uint32_t> spirv = vk::compile_mod_stage(source, includes, pixel, defines, problem, bound_tables);
    if (spirv.empty()) {
        return {};
    }
    mtl::MslOptions options;
    options.ios = TARGET_OS_IPHONE != 0;
    options.bound_tables = bound_tables;
    const std::string msl = mtl::spirv_to_msl(spirv, pixel ? mtl::MslStage::Fragment : mtl::MslStage::Vertex, options, problem);
    return std::vector<std::uint8_t>(msl.begin(), msl.end());
}

}  // namespace galaxy::render
