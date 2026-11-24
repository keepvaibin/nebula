// SPDX-License-Identifier: GPL-3.0-only
// The Metal renderer's shaders that are not materials: the Vulkan renderer's (vk/utility_spirv_table.inc,
// vk/post_programs_vk.inc), turned to MSL at build (platform/apple/tools/utility_msl.cpp), and MetalFX's depth copy. Each
// is one function named `main0`. Their bindings are plain indices: binding n of Vulkan's set s is buffer, texture or
// sampler index kUtilitySetStride * s + n in that function, and their push constants are the buffer at kUtilityPushBuffer.
//
// The same tool writes every text the renderer makes to utility_msl.bin, and platform/apple/tools/compile_metallib.py
// compiles them into the app's utility.metallib, each as the function utility_function_name gives it: at run time a text's
// function is taken from that library (renderer_function), and a text is compiled only in a build made without Apple's
// Metal compiler.
#pragma once

#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string_view>
#include <vector>

namespace galaxy::render::mtl {

inline constexpr std::uint32_t kUtilitySetStride = 8u;
inline constexpr std::uint32_t kUtilityPushBuffer = 24u;

enum class UtilityShader {
#define X(name, source, stage, entry, defines) name,
#include "galaxy/render/vk/utility_spirv_table.inc"
#undef X
    Count
};

enum class UtilityStage : std::uint8_t { Vertex, Fragment, Compute };

// The shader's MSL text (zero-terminated) and its stage.
[[nodiscard]] const char* utility_msl(UtilityShader shader) noexcept;
[[nodiscard]] UtilityStage utility_stage(UtilityShader shader) noexcept;

// A presentation pass's program (post_programs_vk.inc's order) at an SMAA quality, 0-3, and its stage.
[[nodiscard]] const char* post_msl(unsigned program, unsigned smaa_quality) noexcept;
[[nodiscard]] UtilityStage post_stage(unsigned program) noexcept;
[[nodiscard]] unsigned post_program_count() noexcept;

// The utility shaders the Metal renderer uses: the others are the Vulkan renderer's alone (its layer merge, and ambient
// occlusion's half-size and two-channel forms).
[[nodiscard]] constexpr bool metal_uses(UtilityShader shader) noexcept {
    switch (shader) {
    case UtilityShader::LayerMergePs:
    case UtilityShader::AoUpsamplePs:
    case UtilityShader::AoOcclusionRgCs:
    case UtilityShader::AoSsaoRgCs:
    case UtilityShader::AoDenoiseRgCs:
    case UtilityShader::AoDenoiseRCs:
        return false;
    default:
        return true;
    }
}

// The presentation programs the Metal renderer uses: EASU's half-precision form is Vulkan's (Metal runs EASU from
// kEasuPs, post_process_mtl.mm).
[[nodiscard]] constexpr bool metal_uses_post(unsigned program) noexcept {
    constexpr std::string_view kNames[] = {
#define POST(name, pass, library, quality, stage) #name,
#include "galaxy/render/vk/post_programs_vk.inc"
#undef POST
    };
    return program < std::size(kNames) && kNames[program] != "kEasuHalfPs";
}

// MetalFX's depth copy (metalfx_mtl.mm): the scene's depth, read as a colour, written as the scaler's depth.
inline constexpr char kDepthCopyVsMsl[] = R"MSL(#include <metal_stdlib>
using namespace metal;
struct Corner { float4 position [[position]]; };
vertex Corner main0(uint id [[vertex_id]]) {
    Corner out;
    out.position = float4(id == 1u ? 3.0 : -1.0, id == 2u ? -3.0 : 1.0, 0.0, 1.0);
    return out;
}
)MSL";
inline constexpr char kDepthCopyPsMsl[] = R"MSL(#include <metal_stdlib>
using namespace metal;
struct Corner { float4 position [[position]]; };
struct Depth { float depth [[depth(any)]]; };
fragment Depth main0(Corner in [[stage_in]], texture2d<float> scene [[texture(0)]]) {
    Depth out;
    out.depth = scene.read(uint2(in.position.xy)).r;
    return out;
}
)MSL";

// Every text the Metal renderer makes, once each: the utility shaders it uses, each presentation program at each SMAA
// quality that changes it, and the depth copy's two. utility.metallib holds exactly these (utility_msl_check).
[[nodiscard]] std::vector<const char*> renderer_msl_texts();

// A text's name in utility.metallib: FNV-1a's 64 bits of its bytes (without the ending zero), and the function
// `u<those bits, 16 hex digits>::m` (compile_metallib.py puts each text in a namespace of its own and names its main0 `m`).
[[nodiscard]] constexpr std::uint64_t utility_msl_hash(std::string_view text) noexcept {
    std::uint64_t hash = 14695981039346656037ull;
    for (const char c : text) {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= 1099511628211ull;
    }
    return hash;
}
// FNV-1a's published value for "abc"; compile_metallib.py's fnv1a64 gives the same.
static_assert(utility_msl_hash("abc") == 0xe71fa2190541574bull);
struct UtilityFunctionName {
    char text[24];
};
[[nodiscard]] inline UtilityFunctionName utility_function_name(std::string_view text) noexcept {
    UtilityFunctionName name{};
    std::snprintf(name.text, sizeof(name.text), "u%016llx::m", static_cast<unsigned long long>(utility_msl_hash(text)));
    return name;
}

// ---- At run time (utility_library_mtl.mm) ----

// The id<MTLFunction> of a text of renderer_msl_texts(), retained for the caller (who releases it), or null when it cannot
// be made. From utility.metallib (in the app's Resources, or beside a program that is not in an app); a text the library
// lacks is compiled (MSL 2.4, the arithmetic as written), which is logged. Kept for the device: a second ask is a lookup.
[[nodiscard]] void* renderer_function(const char* text);
// Every text of renderer_msl_texts() taken now: the renderer's start, on a worker.
void prepare_renderer_functions();
// The functions taken so far, as one log line ("[metal] renderer shaders: ..."): how many from the library, how many
// compiled from their text.
void report_renderer_functions();

}  // namespace galaxy::render::mtl
