// SPDX-License-Identifier: GPL-3.0-only
// Build tool: the Metal renderer's shaders that are not materials. Takes the SPIR-V tools/dev/utility_spirv.cpp made
// (linked in: vk/utility_spirv.cpp over utility_spirv_data.h) and writes a header of MSL texts, each through
// runtime/src/render/mtl/shader_msl.cpp with the utility bindings (utility_msl.h), and the texts the renderer makes as
// one file for tools/compile_metallib.py, which compiles them into the app's utility.metallib.
//
//   utility_msl <output header> <output texts> macos|ios
//
// The texts' file (utility_msl.bin), little-endian: "NBUMSL01", the count (u32), 0 (u32); then for each text its hash
// (u64, utility_msl_hash), its stage (u32: 0 vertex, 1 fragment, 2 compute), its length (u32) and its bytes. Each text
// once: the utility shaders the Metal renderer uses (metal_uses), each presentation program it uses at each SMAA quality
// that changes it, and MetalFX's depth copy (utility_msl.h). utility_msl_check.cpp compares it with the runtime's list.
#include "galaxy/render/mtl/shader_msl.h"
#include "galaxy/render/mtl/utility_msl.h"
#include "galaxy/render/vk/utility_spirv.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <span>
#include <string>
#include <vector>

using namespace galaxy::render;

namespace {

constexpr mtl::MslStage Vertex = mtl::MslStage::Vertex, Fragment = mtl::MslStage::Fragment, Compute = mtl::MslStage::Compute;

// The texts' file, as it is filled.
struct Texts {
    std::vector<std::uint8_t> bytes;
    std::set<std::uint64_t> hashes;
    std::uint32_t count = 0u;

    void put(std::uint64_t value, unsigned size) {
        for (unsigned i = 0u; i < size; ++i) {
            bytes.push_back(static_cast<std::uint8_t>(value >> (8u * i)));
        }
    }
    void add(const std::string& text, unsigned stage) {
        const std::uint64_t hash = mtl::utility_msl_hash(text);
        if (!hashes.insert(hash).second) {
            return;  // the same text as one before: one function
        }
        put(hash, 8u);
        put(stage, 4u);
        put(text.size(), 4u);
        bytes.insert(bytes.end(), text.begin(), text.end());
        ++count;
    }
};

unsigned stage_number(mtl::MslStage stage) {
    return stage == Vertex ? 0u : stage == Fragment ? 1u : 2u;
}

std::string emit(std::FILE* out, const std::string& name, std::span<const std::uint32_t> spirv, mtl::MslStage stage, bool ios) {
    mtl::MslOptions options;
    options.ios = ios;
    options.utility = true;
    options.set_stride = mtl::kUtilitySetStride;
    std::string error;
    const std::string msl = mtl::spirv_to_msl(spirv, stage, options, error);
    if (msl.empty()) {
        std::fprintf(stderr, "%s: %s\n", name.c_str(), error.c_str());
        return {};
    }
    if (msl.find(")NEBULA\"") != std::string::npos) {
        std::fprintf(stderr, "%s: its text holds the header's own delimiter\n", name.c_str());
        return {};
    }
    // In pieces: a compiler's longest string literal is shorter than a long shader.
    std::fprintf(out, "static const char g_msl_%s[] =", name.c_str());
    constexpr std::size_t kPiece = 8192u;
    for (std::size_t at = 0; at < msl.size(); at += kPiece) {
        std::fprintf(out, "\nR\"NEBULA(%s)NEBULA\"", msl.substr(at, kPiece).c_str());
    }
    std::fprintf(out, ";\n");
    return msl;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: utility_msl <output header> <output texts> macos|ios\n");
        return 2;
    }
    const bool ios = std::strcmp(argv[3], "ios") == 0;
    std::FILE* out = std::fopen(argv[1], "wb");
    if (out == nullptr) {
        return 2;
    }
    bool ok = true;
    Texts texts;
#define X(name, source, stage, entry, defines)                                                                    \
    {                                                                                                              \
        const std::string msl = emit(out, #name, vk::utility_spirv(vk::UtilitySpirv::name), stage, ios);          \
        ok = !msl.empty() && ok;                                                                                   \
        if (!msl.empty() && mtl::metal_uses(mtl::UtilityShader::name)) {                                           \
            texts.add(msl, stage_number(stage));                                                                   \
        }                                                                                                          \
    }
#include "galaxy/render/vk/utility_spirv_table.inc"
#undef X
    std::fprintf(out, "static const char* const kUtilityMsl[] = {\n");
#define X(name, source, stage, entry, defines) std::fprintf(out, "    g_msl_" #name ",\n");
#include "galaxy/render/vk/utility_spirv_table.inc"
#undef X
    std::fprintf(out, "};\n");

    // A pass at each SMAA quality: the SPIR-V table names the same module where the quality does not matter.
    unsigned program = 0u;
#define POST(name, pass, library, quality, stage)                                                                 \
    for (unsigned q = 0u; q < ((quality) ? 4u : 1u); ++q) {                                                       \
        const std::string msl = emit(out, std::string(#name "_") + std::to_string(q), vk::post_spirv(program, q), stage, ios); \
        ok = !msl.empty() && ok;                                                                                  \
        if (!msl.empty() && mtl::metal_uses_post(program)) {                                                      \
            texts.add(msl, stage_number(stage));                                                                  \
        }                                                                                                         \
    }                                                                                                             \
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
    texts.add(mtl::kDepthCopyVsMsl, stage_number(Vertex));
    texts.add(mtl::kDepthCopyPsMsl, stage_number(Fragment));

    if (ok) {
        std::vector<std::uint8_t> file = {'N', 'B', 'U', 'M', 'S', 'L', '0', '1'};
        for (unsigned i = 0u; i < 4u; ++i) {
            file.push_back(static_cast<std::uint8_t>(texts.count >> (8u * i)));
        }
        file.insert(file.end(), 4u, std::uint8_t{0});
        file.insert(file.end(), texts.bytes.begin(), texts.bytes.end());
        std::FILE* blob = std::fopen(argv[2], "wb");
        ok = blob != nullptr && std::fwrite(file.data(), 1u, file.size(), blob) == file.size();
        if (blob != nullptr) {
            ok = std::fclose(blob) == 0 && ok;
        }
        if (!ok) {
            std::fprintf(stderr, "utility_msl: %s could not be written\n", argv[2]);
        }
    }
    if (!ok) {
        std::remove(argv[1]);
        std::remove(argv[2]);
    }
    return ok ? 0 : 1;
}
