// SPDX-License-Identifier: GPL-3.0-only
// One stage of a mod's material as SPIR-V, through the renderer's own HLSL front end, as galaxy_native_spirv does at build.
#include "galaxy/render/mod_shaders.h"
#include "galaxy/render/vk/shader_compile_vk.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>

namespace galaxy::render {

std::uint32_t mod_shader_compiler_revision() noexcept {
    return vk::kCompilerRevision;
}

bool mod_shader_has_bound_form() noexcept {
    return true;
}

std::vector<std::uint8_t> compile_mod_shader_stage(const std::string& source, const std::filesystem::path& includes, bool pixel,
                                                   const std::vector<const char*>& defines, std::string& problem, bool bound_tables) {
    std::string text = source;
    std::vector<std::string> included;
    for (std::size_t at = text.find("#include \""); at != std::string::npos; at = text.find("#include \"", at)) {
        const std::size_t open = at + 10u;
        const std::size_t close = text.find('"', open);
        if (close == std::string::npos) {
            problem = "a program's include has no end";
            return {};
        }
        const std::string name = text.substr(open, close - open);
        std::string body;
        if (std::find(included.begin(), included.end(), name) == included.end()) {
            std::ifstream in(includes / name, std::ios::binary);
            if (!in) {
                problem = name + " is missing";
                return {};
            }
            body.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            included.push_back(name);
        }
        text.replace(at, close + 1u - at, body);
    }
    std::string head;
    for (const char* define : defines) {
        head += std::string("#define ") + define + " 1\n";
    }
    namespace vk = galaxy::render::vk;
    head += pixel ? "#define program_ps main\n" : "#define program_vs main\n";
    const std::vector<std::uint32_t> spirv =
        vk::compile_hlsl(vk::annotate(head + text, vk::draw_bindings()), pixel ? vk::ShaderStage::Fragment : vk::ShaderStage::Vertex, "main", {}, problem,
                         {.bound_tables = bound_tables});
    std::vector<std::uint8_t> out(spirv.size() * sizeof(std::uint32_t));
    if (!out.empty()) {
        std::memcpy(out.data(), spirv.data(), out.size());
    }
    return out;
}

}  // namespace galaxy::render
