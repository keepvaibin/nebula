// SPDX-License-Identifier: GPL-3.0-only
// Compiles every shader the Vulkan GX backend uses through annotate() and glslang: the uber shaders' variants, the
// line/point geometry shaders, generated shaders for representative keys, and the EFB conversion shader. Each must
// pass the SPIR-V validator, and every descriptor it declares must sit at a (set, binding) of its layout.

#include "galaxy/gx/renderer_common.h"
#include "galaxy/gx/shader_gen.h"
#include "galaxy/gx/uber_shaders.h"
#include "galaxy/gx/vk/shader_compile_vk.h"

#include <cstdio>
#include <map>
#include <string>

using namespace galaxy::gx;
using namespace galaxy::gx::vk;

namespace {

int failures = 0;

// SPIR-V opcodes, decorations and storage classes the binding check reads.
constexpr std::uint32_t kOpVariable = 59;
constexpr std::uint32_t kOpDecorate = 71;
constexpr std::uint32_t kDecorationBinding = 33;
constexpr std::uint32_t kDecorationDescriptorSet = 34;
constexpr std::uint32_t kStorageUniformConstant = 0;
constexpr std::uint32_t kStorageUniform = 2;
constexpr std::uint32_t kStoragePushConstant = 9;
constexpr std::uint32_t kStorageStorageBuffer = 12;

// Empty when every descriptor variable has a set and binding the layout names; else what is wrong.
std::string check_bindings(const std::vector<std::uint32_t>& spirv, std::span<const RegisterBinding> bindings) {
    std::map<std::uint32_t, std::uint32_t> sets, slots, storage;
    for (std::size_t at = 5; at < spirv.size();) {
        const std::uint32_t count = spirv[at] >> 16u;
        const std::uint32_t op = spirv[at] & 0xffffu;
        if (count == 0u || at + count > spirv.size()) {
            return "malformed SPIR-V";
        }
        if (op == kOpDecorate && count >= 4u && spirv[at + 2] == kDecorationDescriptorSet) {
            sets[spirv[at + 1]] = spirv[at + 3];
        } else if (op == kOpDecorate && count >= 4u && spirv[at + 2] == kDecorationBinding) {
            slots[spirv[at + 1]] = spirv[at + 3];
        } else if (op == kOpVariable && count >= 4u) {
            storage[spirv[at + 2]] = spirv[at + 3];
        }
        at += count;
    }
    std::string problems;
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> taken;  // (set, binding) -> variable
    for (const auto& [id, cls] : storage) {
        if (cls == kStoragePushConstant) {
            bool declared = false;
            for (const RegisterBinding& binding : bindings) {
                declared |= binding.push_constant;
            }
            if (!declared) {
                problems += "push constant block outside the layout; ";
            }
            continue;
        }
        if (cls != kStorageUniformConstant && cls != kStorageUniform && cls != kStorageStorageBuffer) {
            continue;
        }
        if (!sets.contains(id) || !slots.contains(id)) {
            problems += "%" + std::to_string(id) + " has no set/binding; ";
            continue;
        }
        if (const auto [it, inserted] = taken.emplace(std::pair{sets[id], slots[id]}, id); !inserted) {
            problems += "%" + std::to_string(id) + " and %" + std::to_string(it->second) + " share (" +
                std::to_string(sets[id]) + ", " + std::to_string(slots[id]) + "); ";
        }
        bool found = false;
        for (const RegisterBinding& binding : bindings) {
            found |= !binding.push_constant && binding.set == sets[id] && binding.binding == slots[id];
        }
        if (!found) {
            problems += "%" + std::to_string(id) + " at (" + std::to_string(sets[id]) + ", " +
                std::to_string(slots[id]) + ") outside the layout; ";
        }
    }
    return problems;
}

void expect(const char* what, std::string_view hlsl, ShaderStage stage, std::span<const RegisterBinding> bindings,
            std::initializer_list<const char*> defines = {}, bool dual_source = false) {
    std::string error;
    const std::vector<std::uint32_t> spirv =
        compile_hlsl(annotate(hlsl, bindings), stage, "main", defines, error, {true, dual_source});
    if (spirv.empty()) {
        ++failures;
        std::printf("FAIL %s\n%s\n", what, error.c_str());
        return;
    }
    const std::string problems = check_bindings(spirv, bindings);
    if (!problems.empty()) {
        ++failures;
        std::printf("FAIL %s: %s\n", what, problems.c_str());
        return;
    }
    std::printf("ok   %s (%zu words)\n", what, spirv.size());
}

}  // namespace

int main(int argc, char** argv) {
    const auto gx = gx_draw_bindings();
    expect("uber vs", uber_vs_source(), ShaderStage::Vertex, gx);
    // The six output variants, defined as PipelineCacheVk defines them.
    for (unsigned variant = 0; variant < kUberPsVariants; ++variant) {
        const std::string label = "uber ps variant " + std::to_string(variant);
        const bool dual = uber_ps_variant_dual_source(variant);
        expect(label.c_str(), uber_ps_source(), ShaderStage::Fragment, gx,
               {dual ? "UBER_DUAL_SRC=1" : "UBER_DUAL_SRC=0",
                uber_ps_variant_export_depth(variant) ? "UBER_EXPORT_DEPTH=1" : "UBER_EXPORT_DEPTH=0",
                uber_ps_variant_early_depth(variant) ? "UBER_EARLY_DEPTH=1" : "UBER_EARLY_DEPTH=0"},
               dual);
    }
    expect("line gs", ShaderGenerator::line_geometry_shader_source(), ShaderStage::Geometry, gx);
    expect("point gs", ShaderGenerator::point_geometry_shader_source(), ShaderStage::Geometry, gx);

    const ShaderGenerator generator;
    VertexShaderKey vs{};
    PixelShaderKey ps{};
    expect("generated vs (default key)", generator.generate_vs(vs), ShaderStage::Vertex, gx);
    expect("generated ps (default key)", generator.generate_ps(ps), ShaderStage::Fragment, gx);

    // argv[1]: keys a session captured (GALAXY_TEST_SHADER_KEY_CAPTURE): 'V' or 'P', then the key's bytes.
    if (argc > 1) {
        std::FILE* file = std::fopen(argv[1], "rb");
        if (file == nullptr) {
            std::printf("FAIL cannot open %s\n", argv[1]);
            return 1;
        }
        unsigned vs_count = 0, ps_count = 0;
        const int failures_before = failures;
        for (int tag; (tag = std::fgetc(file)) != EOF;) {
            if (tag == 'V' && std::fread(&vs, sizeof(vs), 1, file) == 1) {
                const std::string label = "captured vs " + std::to_string(vs_count++);
                expect(label.c_str(), generator.generate_vs(vs), ShaderStage::Vertex, gx);
            } else if (tag == 'P' && std::fread(&ps, sizeof(ps), 1, file) == 1) {
                const std::string label = "captured ps " + std::to_string(ps_count++);
                expect(label.c_str(), generator.generate_ps(ps), ShaderStage::Fragment, gx, {},
                       (ps.flags & 0x80u) != 0u);
            } else {
                ++failures;
                std::printf("FAIL %s is truncated or malformed\n", argv[1]);
                break;
            }
        }
        std::fclose(file);
        std::printf("captured: %u vertex and %u pixel shaders, %d failure(s)\n", vs_count, ps_count,
                    failures - failures_before);
    }

    constexpr RegisterBinding conversion[] = {{"b0", 0, 0, true}, {"t0", 0, 0}, {"s0", 0, 1}, {"s1", 0, 2}};
    expect("efb conversion ps", efb_conversion_shader_source(), ShaderStage::Fragment, conversion);

    // The utility passes: one texture and sampler, constants as push constants.
    constexpr RegisterBinding utility[] = {{"b0", 0, 0, true}, {"t0", 0, 0}, {"s0", 0, 1}};
    expect("blit vs", blit_vertex_shader_source(), ShaderStage::Vertex, utility);
    expect("blit ps", blit_pixel_shader_source(), ShaderStage::Fragment, utility);
    expect("clear ps", clear_pixel_shader_source(), ShaderStage::Fragment, utility);
    expect("depth carry ps", depth_carry_pixel_shader_source(), ShaderStage::Fragment, utility);
    expect("ui textured vs", ui_textured_quad_vertex_shader_source(), ShaderStage::Vertex, utility);
    expect("ui textured ps", ui_textured_quad_pixel_shader_source(), ShaderStage::Fragment, utility);
    expect("ui solid vs", ui_solid_quad_vertex_shader_source(), ShaderStage::Vertex, utility);
    expect("ui solid ps", ui_solid_quad_pixel_shader_source(), ShaderStage::Fragment, utility);

    std::printf("%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
