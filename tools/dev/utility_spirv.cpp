// Build tool: compiles the Vulkan renderer's shaders that are not materials (galaxy/render/vk/utility_spirv_table.inc,
// post_programs_vk.inc) into a header of SPIR-V arrays: the program ships no shader compiler.
// utility_spirv <output header>

#include "galaxy/render/curved_surfaces.h"
#include "galaxy/render/interp_eval.h"
#include "galaxy/render/renderer_common.h"
#include "galaxy/render/vk/shader_compile_vk.h"
#include "../../runtime/src/render/vk/utility_spirv_sources.h"

#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

using namespace galaxy::render;
using namespace galaxy::render::vk;

namespace {

bool emit(std::FILE* out, const std::string& name, const std::string& source, ShaderStage stage, const char* entry,
          std::initializer_list<const char*> defines) {
    std::string error;
    const std::vector<std::uint32_t> spirv = compile_hlsl(source, stage, entry, defines, error, {.validate = true});
    if (spirv.empty()) {
        std::fprintf(stderr, "%s (%s): %s\n", name.c_str(), entry, error.c_str());
        return false;
    }
    std::fprintf(out, "static const std::uint32_t g_spirv_%s[] = {", name.c_str());
    for (std::size_t i = 0; i < spirv.size(); ++i) {
        std::fprintf(out, "%s%uu,", i % 12u == 0u ? "\n" : "", spirv[i]);
    }
    std::fprintf(out, "\n};\n");
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        return 2;
    }
    std::FILE* out = std::fopen(argv[1], "wb");
    if (out == nullptr) {
        return 2;
    }
    bool ok = true;
#define X(name, source, stage, entry, defines) ok = emit(out, #name, source, ShaderStage::stage, entry, defines) && ok;
#include "galaxy/render/vk/utility_spirv_table.inc"
#undef X
    std::fprintf(out, "static const std::span<const std::uint32_t> kUtilitySpirv[] = {\n");
#define X(name, source, stage, entry, defines) std::fprintf(out, "    g_spirv_" #name ",\n");
#include "galaxy/render/vk/utility_spirv_table.inc"
#undef X
    std::fprintf(out, "};\n");

    // A pass that follows the SMAA quality in each preset, any other once (an SMAA pass at the preset HIGH).
#define POST(name, pass, library, quality, stage)                                                                  \
    for (unsigned q = 0u; q < ((quality) ? 4u : 1u); ++q) {                                                        \
        const char* preset = Library::library != Library::kSmaa ? nullptr                                          \
            : (quality)                                         ? smaa_preset(q)                                   \
                                                                : "SMAA_PRESET_HIGH";                              \
        ok = emit(out, std::string(#name "_") + std::to_string(q),                                                 \
                  annotate(compose(pass, preset, Library::library), kPostBindings), ShaderStage::stage,            \
                  ShaderStage::stage == ShaderStage::Vertex ? "VSMain" : "PSMain", {}) && ok;                      \
    }
#include "galaxy/render/vk/post_programs_vk.inc"
#undef POST
    std::fprintf(out, "static const std::span<const std::uint32_t> kPostSpirv[][4] = {\n");
#define POST(name, pass, library, quality, stage)                                                                  \
    std::fprintf(out, "    {g_spirv_" #name "_0, g_spirv_" #name "_%u, g_spirv_" #name "_%u, g_spirv_" #name "_%u},\n", \
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
