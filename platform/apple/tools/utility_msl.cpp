// SPDX-License-Identifier: GPL-3.0-only
// Build tool: the Metal renderer's shaders that are not materials. Takes the SPIR-V tools/dev/utility_spirv.cpp made
// (linked in: vk/utility_spirv.cpp over utility_spirv_data.h) and writes a header of MSL texts, each through
// runtime/src/render/mtl/shader_msl.cpp with the utility bindings (utility_msl.h).
//
//   utility_msl <output header> macos|ios
#include "galaxy/render/mtl/shader_msl.h"
#include "galaxy/render/mtl/utility_msl.h"
#include "galaxy/render/vk/utility_spirv.h"

#include <cstdio>
#include <cstring>
#include <span>
#include <string>

using namespace galaxy::render;

namespace {

constexpr mtl::MslStage Vertex = mtl::MslStage::Vertex, Fragment = mtl::MslStage::Fragment, Compute = mtl::MslStage::Compute;

bool emit(std::FILE* out, const std::string& name, std::span<const std::uint32_t> spirv, mtl::MslStage stage, bool ios) {
    mtl::MslOptions options;
    options.ios = ios;
    options.utility = true;
    options.set_stride = mtl::kUtilitySetStride;
    std::string error;
    const std::string msl = mtl::spirv_to_msl(spirv, stage, options, error);
    if (msl.empty()) {
        std::fprintf(stderr, "%s: %s\n", name.c_str(), error.c_str());
        return false;
    }
    if (msl.find(")NEBULA\"") != std::string::npos) {
        std::fprintf(stderr, "%s: its text holds the header's own delimiter\n", name.c_str());
        return false;
    }
    // In pieces: a compiler's longest string literal is shorter than a long shader.
    std::fprintf(out, "static const char g_msl_%s[] =", name.c_str());
    constexpr std::size_t kPiece = 8192u;
    for (std::size_t at = 0; at < msl.size(); at += kPiece) {
        std::fprintf(out, "\nR\"NEBULA(%s)NEBULA\"", msl.substr(at, kPiece).c_str());
    }
    std::fprintf(out, ";\n");
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: utility_msl <output header> macos|ios\n");
        return 2;
    }
    const bool ios = std::strcmp(argv[2], "ios") == 0;
    std::FILE* out = std::fopen(argv[1], "wb");
    if (out == nullptr) {
        return 2;
    }
    bool ok = true;
#define X(name, source, stage, entry, defines) \
    ok = emit(out, #name, vk::utility_spirv(vk::UtilitySpirv::name), stage, ios) && ok;
#include "galaxy/render/vk/utility_spirv_table.inc"
#undef X
    std::fprintf(out, "static const char* const kUtilityMsl[] = {\n");
#define X(name, source, stage, entry, defines) std::fprintf(out, "    g_msl_" #name ",\n");
#include "galaxy/render/vk/utility_spirv_table.inc"
#undef X
    std::fprintf(out, "};\n");

    // A pass at each SMAA quality: the SPIR-V table names the same module where the quality does not matter.
    unsigned program = 0u;
#define POST(name, pass, library, quality, stage)                                                        \
    for (unsigned q = 0u; q < ((quality) ? 4u : 1u); ++q) {                                              \
        ok = emit(out, std::string(#name "_") + std::to_string(q), vk::post_spirv(program, q), stage, ios) && ok; \
    }                                                                                                    \
    ++program;
#include "galaxy/render/vk/post_programs_vk.inc"
#undef POST
    std::fprintf(out, "static const char* const kPostMsl[][4] = {\n");
#define POST(name, pass, library, quality, stage)                                                                  \
    std::fprintf(out, "    {g_msl_" #name "_0, g_msl_" #name "_%u, g_msl_" #name "_%u, g_msl_" #name "_%u},\n", \
                 (quality) ? 1u : 0u, (quality) ? 2u : 0u, (quality) ? 3u : 0u);
#include "galaxy/render/vk/post_programs_vk.inc"
#undef POST
    std::fprintf(out, "};\n");
    std::fclose(out);
    if (!ok) {
        std::remove(argv[1]);
    }
    return ok ? 0 : 1;
}
