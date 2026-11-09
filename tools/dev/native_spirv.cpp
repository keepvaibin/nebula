// SPDX-License-Identifier: GPL-3.0-only
// Build tool (cmake/native_materials.cmake): a native material's shader as SPIR-V, through the renderer's own HLSL
// front end and bindings (shader_compile_vk.h), written as a C++ header.
//   galaxy_native_spirv <material.hlsl> <materials dir (its includes)> <vs|ps> <array name> <out.h> [DEFINE...]
// The define NEBULA_BOUND_TABLES builds the program for a device without descriptor indexing (the compiler adds it and
// the table's length, kBoundTableSlots).
#include "galaxy/render/vk/shader_compile_vk.h"

#include <algorithm>
#include <cstdio>
#include <vector>
#include <fstream>
#include <sstream>
#include <string>

namespace {

bool read_text(const char* path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::ostringstream text;
    text << in.rdbuf();
    out = text.str();
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 6) {
        std::fprintf(stderr, "usage: galaxy_native_spirv program.hlsl includes vs|ps entry out [DEFINE...]\n");
        return 2;
    }
    std::string source;
    if (!read_text(argv[1], source)) {
        std::fprintf(stderr, "galaxy_native_spirv: cannot read %s\n", argv[1]);
        return 1;
    }
    // Each #include "name" replaced by the materials directory's file of that name (recursively, each once).
    std::vector<std::string> included;
    for (std::size_t at = source.find("#include \""); at != std::string::npos; at = source.find("#include \"", at)) {
        const std::size_t open = at + 10u;
        const std::size_t close = source.find('"', open);
        const std::string name = source.substr(open, close - open);
        std::string text;
        if (std::find(included.begin(), included.end(), name) == included.end()) {
            if (!read_text((std::string(argv[2]) + "/" + name).c_str(), text)) {
                std::fprintf(stderr, "galaxy_native_spirv: %s: cannot read the include %s\n", argv[1], name.c_str());
                return 1;
            }
            included.push_back(name);
        }
        source.replace(at, close + 1u - at, text);
    }
    std::string defines;
    bool bound_tables = false;
    for (int i = 6; i < argc; ++i) {
        if (std::string(argv[i]) == "NEBULA_BOUND_TABLES") {
            bound_tables = true;
            continue;
        }
        defines += "#define " + std::string(argv[i]) + " 1\n";
    }
    namespace vk = galaxy::render::vk;
    const std::string stage_name = argv[3];
    const vk::ShaderStage stage = stage_name == "ps"   ? vk::ShaderStage::Fragment
                                                       : vk::ShaderStage::Vertex;
    // The renderer names every stage's entry point "main".
    defines += "#define " + std::string(argv[4]) + " main\n";
    std::string error;
    const std::vector<std::uint32_t> spirv = vk::compile_hlsl(vk::annotate(defines + source, vk::draw_bindings()),
                                                              stage, "main", {}, error,
                                                              {.validate = true, .bound_tables = bound_tables});
    if (spirv.empty()) {
        std::fprintf(stderr, "galaxy_native_spirv: %s (%s): %s\n", argv[1], argv[3], error.c_str());
        return 1;
    }
    std::ofstream out(argv[5], std::ios::binary);
    out.write(reinterpret_cast<const char*>(spirv.data()), static_cast<std::streamsize>(spirv.size() * sizeof(std::uint32_t)));
    return out ? 0 : 1;
}
