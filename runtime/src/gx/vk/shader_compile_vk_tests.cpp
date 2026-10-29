// SPDX-License-Identifier: GPL-3.0-only
// Compiles every shader the Vulkan GX backend uses through annotate() and glslang: the uber shaders' variants, the
// line/point geometry shaders, generated shaders for representative keys, and the EFB conversion shader. Each must
// pass the SPIR-V validator, and every descriptor it declares must sit at a (set, binding) of its layout. Stage
// variables must sit at their semantics' locations, constant-buffer members at the C++ structs' offsets, and matrices
// must be row-major as on D3D12.

#include "galaxy/gx/renderer_common.h"
#include "galaxy/gx/shader_gen.h"
#include "galaxy/gx/uber_constants.h"
#include "galaxy/gx/uber_shaders.h"
#include "galaxy/gx/vk/shader_compile_vk.h"

#include <cstddef>
#include <utility>
#include <cstdio>
#include <map>
#include <set>
#include <string>

using namespace galaxy::gx;
using namespace galaxy::gx::vk;

namespace galaxy::gx {
// The shader source cmake/post_shader_sources.cmake embeds.
extern const unsigned char galaxy_post_game_bloom_hlsl[];
}  // namespace galaxy::gx

namespace {

int failures = 0;

// The constant-buffer members the shaders read, at the byte offsets the C++ structs write them (uber_constants.h).
const std::map<std::string, std::size_t>& constant_offsets() {
    static const std::map<std::string, std::size_t> offsets = {
        {"projection", offsetof(GxVsConstants, projection)},
        {"texgen_config", offsetof(GxVsConstants, texgen_config)},
        {"chan_ambient", offsetof(GxVsConstants, chan_ambient)},
        {"chan_material", offsetof(GxVsConstants, chan_material)},
        {"num_texgens", offsetof(GxVsConstants, num_texgens)},
        {"num_chans", offsetof(GxVsConstants, num_chans)},
        {"mtx_palette_base", offsetof(GxVsConstants, mtx_palette_base)},
        {"matrix_index_a", offsetof(GxVsConstants, matrix_index_a)},
        {"matrix_index_b", offsetof(GxVsConstants, matrix_index_b)},
        {"inline_pos_matrix", offsetof(GxVsConstants, inline_pos_matrix)},
        {"inline_tex_matrices", offsetof(GxVsConstants, inline_tex_matrices)},
        {"inline_post_matrices", offsetof(GxVsConstants, inline_post_matrices)},
        {"line_point_raster", offsetof(GxVsConstants, line_point_raster)},
        {"line_point_tex_offsets", offsetof(GxVsConstants, line_point_tex_offsets)},
        {"uber_vs_header", offsetof(GxVsConstants, uber_vs_header)},
        {"uber_texgen", offsetof(GxVsConstants, uber_texgen)},
        {"uber_post_texgen", offsetof(GxVsConstants, uber_post_texgen)},
        {"uber_channel", offsetof(GxVsConstants, uber_channel)},
        {"stage_config", offsetof(GxPsConstants, stage_config)},
        {"tev_regs", offsetof(GxPsConstants, tev_regs)},
        {"konst", offsetof(GxPsConstants, konst)},
        {"alpha_refs", offsetof(GxPsConstants, alpha_refs)},
        {"fog_params", offsetof(GxPsConstants, fog_params)},
        {"tex_dims", offsetof(GxPsConstants, tex_dims)},
        {"ztex_params", offsetof(GxPsConstants, ztex_params)},
        {"ind_cmd", offsetof(GxPsConstants, ind_cmd)},
        {"ind_mtx", offsetof(GxPsConstants, ind_mtx)},
        {"ind_misc", offsetof(GxPsConstants, ind_misc)},
        {"uber_ps_header", offsetof(GxPsConstants, uber_ps_header)},
    };
    return offsets;
}

// Empty when every named member of a uniform block sits at the C++ offset; else the mismatches.
std::string check_constant_offsets(const std::vector<std::uint32_t>& spirv, unsigned& matched) {
    matched = 0;
    constexpr std::uint32_t kOpMemberName = 6;
    constexpr std::uint32_t kOpMemberDecorate = 72;
    constexpr std::uint32_t kDecorationOffset = 35;
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::string> names;
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> offsets;
    for (std::size_t at = 5; at < spirv.size();) {
        const std::uint32_t count = spirv[at] >> 16u;
        const std::uint32_t op = spirv[at] & 0xffffu;
        if (count == 0u) {
            break;
        }
        if (op == kOpMemberName && count >= 4u) {
            std::string name;
            for (std::size_t w = at + 3u; w < at + count; ++w) {
                for (unsigned b = 0; b < 4u; ++b) {
                    const char c = static_cast<char>((spirv[w] >> (b * 8u)) & 0xffu);
                    if (c == '\0') break;
                    name += c;
                }
            }
            names[{spirv[at + 1], spirv[at + 2]}] = name.substr(0, name.find('\0'));
        } else if (op == kOpMemberDecorate && count >= 5u && spirv[at + 3] == kDecorationOffset) {
            offsets[{spirv[at + 1], spirv[at + 2]}] = spirv[at + 4];
        }
        at += count;
    }
    std::string problems;
    for (const auto& [member, name] : names) {
        const auto expected = constant_offsets().find(name);
        const auto actual = offsets.find(member);
        if (expected != constant_offsets().end() && actual != offsets.end()) {
            ++matched;
        }
        if (expected != constant_offsets().end() && actual != offsets.end() && expected->second != actual->second) {
            problems += name + " at " + std::to_string(actual->second) + ", C++ " + std::to_string(expected->second) +
                "; ";
        }
    }
    return problems;
}

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

// The locations of the module's inputs (storage class 1) or outputs (3).
std::set<std::uint32_t> io_locations(const std::vector<std::uint32_t>& spirv, std::uint32_t storage_class) {
    constexpr std::uint32_t kDecorationLocation = 30;
    std::map<std::uint32_t, std::uint32_t> locations;
    std::set<std::uint32_t> variables;
    for (std::size_t at = 5; at < spirv.size();) {
        const std::uint32_t count = spirv[at] >> 16u;
        const std::uint32_t op = spirv[at] & 0xffffu;
        if (count == 0u) {
            break;
        }
        if (op == kOpDecorate && count >= 4u && spirv[at + 2] == kDecorationLocation) {
            locations[spirv[at + 1]] = spirv[at + 3];
        } else if (op == kOpVariable && count >= 4u && spirv[at + 3] == storage_class) {
            variables.insert(spirv[at + 2]);
        }
        at += count;
    }
    std::set<std::uint32_t> result;
    for (const std::uint32_t variable : variables) {
        if (const auto it = locations.find(variable); it != locations.end()) {
            result.insert(it->second);
        }
    }
    return result;
}

// The patch stages' per-vertex variables span the patch's three vertices (glslang sized a split array member's
// elements by the member's own length).
std::string check_patch_arrays(const std::vector<std::uint32_t>& spirv, ShaderStage stage) {
    constexpr std::uint32_t kOpTypeArray = 28;
    constexpr std::uint32_t kOpTypePointer = 32;
    constexpr std::uint32_t kOpConstant = 43;
    constexpr std::uint32_t kDecorationPatch = 15;
    constexpr std::uint32_t kDecorationLocation = 30;
    std::map<std::uint32_t, std::uint32_t> lengths, constants, pointees;
    std::set<std::uint32_t> located, per_patch;
    struct Variable {
        std::uint32_t id, pointer, storage;
    };
    std::vector<Variable> variables;
    for (std::size_t at = 5; at < spirv.size() && (spirv[at] >> 16u) != 0u; at += spirv[at] >> 16u) {
        const std::uint32_t count = spirv[at] >> 16u;
        const std::uint32_t op = spirv[at] & 0xffffu;
        if (op == kOpTypeArray && count == 4u) {
            lengths[spirv[at + 1]] = spirv[at + 3];
        } else if (op == kOpConstant && count == 4u) {
            constants[spirv[at + 2]] = spirv[at + 3];
        } else if (op == kOpTypePointer && count == 4u) {
            pointees[spirv[at + 1]] = spirv[at + 3];
        } else if (op == kOpDecorate && count >= 3u && spirv[at + 2] == kDecorationLocation) {
            located.insert(spirv[at + 1]);
        } else if (op == kOpDecorate && count >= 3u && spirv[at + 2] == kDecorationPatch) {
            per_patch.insert(spirv[at + 1]);
        } else if (op == kOpVariable && count >= 4u && (spirv[at + 3] == 1u || spirv[at + 3] == 3u)) {
            variables.push_back({spirv[at + 2], spirv[at + 1], spirv[at + 3]});
        }
    }
    std::string problems;
    for (const Variable& v : variables) {
        if (!located.contains(v.id) || per_patch.contains(v.id) || (stage == ShaderStage::Domain && v.storage == 3u)) {
            continue;
        }
        const auto length = lengths.find(pointees[v.pointer]);
        if (length == lengths.end() || constants[length->second] != 3u) {
            problems += "%" + std::to_string(v.id) + " is not an array over the patch's 3 vertices; ";
        }
    }
    return problems;
}

// Stage variables sit at their semantics' locations even when the shader leaves others unused (glslang alone would
// number the used ones from 0).
void expect_semantic_locations() {
    constexpr const char* kVs = R"(
struct VSIn { float3 pos : POSITION; float3 nrm : NORMAL; uint color0 : COLOR0; uint color1 : COLOR1; };
struct VSOut { float4 pos : SV_Position; float4 col0 : COLOR0; float3 uv3 : TEXCOORD3; };
VSOut main(VSIn input) {
    VSOut o;
    o.pos = float4(input.pos, 1.0);
    o.col0 = 0.0;
    o.uv3 = float3(input.color1, 0.0, 0.0);
    return o;
})";
    constexpr const char* kPs = R"(
struct PSIn { float4 pos : SV_Position; float3 uv3 : TEXCOORD3; };
float4 main(PSIn input) : SV_Target0 { return float4(input.uv3, 1.0); })";
    std::string error;
    const auto vs = compile_hlsl(kVs, ShaderStage::Vertex, "main", {}, error, {true, false});
    const auto ps = compile_hlsl(kPs, ShaderStage::Fragment, "main", {}, error, {true, false});
    const bool ok = !vs.empty() && !ps.empty() && io_locations(vs, 1) == std::set<std::uint32_t>{0u, 5u} &&
        io_locations(vs, 3) == std::set<std::uint32_t>{0u, 5u} && io_locations(ps, 1) == std::set<std::uint32_t>{5u} &&
        io_locations(ps, 3) == std::set<std::uint32_t>{0u};
    if (!ok) {
        ++failures;
        std::printf("FAIL semantic locations %s\n", error.c_str());
        return;
    }
    std::printf("ok   semantic locations (POSITION 0, COLOR1 5; COLOR0 0, TEXCOORD3 5; SV_Target0 0)\n");
}

// Constant-buffer matrices default to row-major, as the D3D12 compile (D3DCOMPILE_PACK_MATRIX_ROW_MAJOR): a plain
// float4x4 compiles to the same module as one declared row_major (glslang's own default is the transpose).
void expect_row_major_matrices() {
    constexpr const char* kPlain = R"(
cbuffer C : register(b0) { float4x4 m; };
float4 main(float3 p : POSITION) : SV_Position { return mul(m, float4(p, 1.0)); })";
    constexpr const char* kRowMajor = R"(
cbuffer C : register(b0) { row_major float4x4 m; };
float4 main(float3 p : POSITION) : SV_Position { return mul(m, float4(p, 1.0)); })";
    constexpr RegisterBinding bindings[] = {{"b0", 0, 0}};
    std::string error;
    const auto plain = compile_hlsl(annotate(kPlain, bindings), ShaderStage::Vertex, "main", {}, error, {true, false});
    const auto row_major =
        compile_hlsl(annotate(kRowMajor, bindings), ShaderStage::Vertex, "main", {}, error, {true, false});
    if (plain.empty() || plain != row_major) {
        ++failures;
        std::printf("FAIL row-major matrices %s\n", error.c_str());
        return;
    }
    std::printf("ok   row-major matrices (as D3DCOMPILE_PACK_MATRIX_ROW_MAJOR)\n");
}

void expect(const char* what, std::string_view hlsl, ShaderStage stage, std::span<const RegisterBinding> bindings,
            std::initializer_list<const char*> defines = {}, bool dual_source = false) {
    std::string error;
    const std::vector<std::uint32_t> spirv =
        compile_hlsl(annotate(hlsl, bindings), stage, "main", defines, error, {true, dual_source, true});
    if (spirv.empty()) {
        ++failures;
        std::printf("FAIL %s\n%s\n", what, error.c_str());
        return;
    }
    unsigned matched = 0;
    std::string problems = check_bindings(spirv, bindings) + check_constant_offsets(spirv, matched);
    if (stage == ShaderStage::Hull || stage == ShaderStage::Domain) {
        problems += check_patch_arrays(spirv, stage);
    }
    bool uniform_block = false;  // the optimizer drops a block the shader never reads
    for (std::size_t at = 5; at < spirv.size() && (spirv[at] >> 16u) != 0u; at += spirv[at] >> 16u) {
        uniform_block |= (spirv[at] & 0xffffu) == kOpVariable && spirv[at + 3] == kStorageUniform;
    }
    const bool declares = hlsl.find("cbuffer GxVsConstants") != std::string_view::npos ||
        hlsl.find("cbuffer GxPsConstants") != std::string_view::npos;
    if (declares && uniform_block && matched == 0u) {
        problems += "no constant-buffer member names to check; ";
    }
    if (dual_source) {
        // Two colour outputs, both at location 0: the second at index 1 (the blender's SRC1).
        constexpr std::uint32_t kDecorationLocation = 30;
        constexpr std::uint32_t kDecorationIndex = 32;
        std::map<std::uint32_t, std::uint32_t> locations, indices;
        std::set<std::uint32_t> outputs;
        for (std::size_t at = 5; at < spirv.size();) {
            const std::uint32_t count = spirv[at] >> 16u;
            const std::uint32_t op = spirv[at] & 0xffffu;
            if (op == kOpDecorate && count >= 4u && spirv[at + 2] == kDecorationLocation) {
                locations[spirv[at + 1]] = spirv[at + 3];
            } else if (op == kOpDecorate && count >= 4u && spirv[at + 2] == kDecorationIndex) {
                indices[spirv[at + 1]] = spirv[at + 3];
            } else if (op == kOpVariable && count >= 4u && spirv[at + 3] == 3u) {
                outputs.insert(spirv[at + 2]);
            }
            at += count;
        }
        unsigned first = 0, second = 0;
        for (const std::uint32_t output : outputs) {
            if (!locations.contains(output)) {
                continue;  // built-in (SV_Depth)
            }
            const std::uint32_t index = indices.contains(output) ? indices[output] : 0u;
            first += locations[output] == 0u && index == 0u;
            second += locations[output] == 0u && index == 1u;
        }
        if (first != 1u || second != 1u) {
            problems += "dual-source outputs are not location 0 index 0 and 1; ";
        }
    }
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
    expect_semantic_locations();
    expect_row_major_matrices();
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
    // Curved surfaces' hull and domain stages, every vertex output variant.
    for (unsigned variant = 0; variant < 4u; ++variant) {
        const std::string hull = ShaderGenerator::curved_stage_source(variant, false);
        const std::string domain = ShaderGenerator::curved_stage_source(variant, true);
        expect(("curved hs " + std::to_string(variant)).c_str(), hull, ShaderStage::Hull, gx);
        expect(("curved ds " + std::to_string(variant)).c_str(), domain, ShaderStage::Domain, gx);
    }

    const ShaderGenerator generator;
    // The forms PipelineCacheVk generates: per-component byte masks always, constant TEV zeros on Qualcomm.
    ShaderGenerator scalar_masks, constant_zero;
    {
        ShaderGenOptions options;
        options.scalar_byte_masks = true;
        scalar_masks.set_options(options);
        options.tev_zero_from_constant = true;
        constant_zero.set_options(options);
    }
    VertexShaderKey vs{};
    PixelShaderKey ps{};
    expect("generated vs (default key)", generator.generate_vs(vs), ShaderStage::Vertex, gx);
    expect("generated ps (default key)", generator.generate_ps(ps), ShaderStage::Fragment, gx);
    expect("generated ps (scalar masks)", scalar_masks.generate_ps(ps), ShaderStage::Fragment, gx);
    expect("generated vs (point size)", with_point_size(generator.generate_vs(vs)), ShaderStage::Vertex, gx);
    expect("uber vs (point size)", with_point_size(uber_vs_source()), ShaderStage::Vertex, gx);
    expect("generated ps (constant TEV zero)", constant_zero.generate_ps(ps), ShaderStage::Fragment, gx);

    // argv[1]: keys a session captured (GALAXY_TEST_SHADER_KEY_CAPTURE): 'V' or 'P', then the key's bytes.
    if (argc > 1) {
        std::FILE* file = std::fopen(argv[1], "rb");
        if (file == nullptr) {
            std::printf("FAIL cannot open %s\n", argv[1]);
            return 1;
        }
        unsigned vs_count = 0, ps_count = 0;
        const int failures_before = failures;
        // argv[2]: a folder to write each captured key's HLSL (vs_N.hlsl, ps_N.hlsl, its description first) and SPIR-V
        // (vs_N.spv, ps_N.spv) to, for instruction counts (fxc, RGA).
        const auto dump = [&](const char* stage, unsigned index, const std::string& description,
                              const std::string& source) {
            if (argc <= 2) {
                return;
            }
            const std::string base = std::string(argv[2]) + "/" + stage + "_" + std::to_string(index);
            const bool pixel = stage[0] == 'p';
            std::string error;
            const std::vector<std::uint32_t> spirv =
                compile_hlsl(annotate(source, gx), pixel ? ShaderStage::Fragment : ShaderStage::Vertex, "main", {},
                             error, {false, pixel && (ps.flags & 0x80u) != 0u});
            if (std::FILE* out = std::fopen((base + ".spv").c_str(), "wb")) {
                std::fwrite(spirv.data(), sizeof(std::uint32_t), spirv.size(), out);
                std::fclose(out);
            }
            const std::string path = base + ".hlsl";
            if (std::FILE* out = std::fopen(path.c_str(), "wb")) {
                std::fprintf(out, "// %s\n%s", description.c_str(), source.c_str());
                std::fclose(out);
            }
        };
        for (int tag; (tag = std::fgetc(file)) != EOF;) {
            if (tag == 'V' && std::fread(&vs, sizeof(vs), 1, file) == 1) {
                dump("vs", vs_count, ShaderGenerator::describe(vs), generator.generate_vs(vs));
                const std::string label = "captured vs " + std::to_string(vs_count++);
                expect(label.c_str(), generator.generate_vs(vs), ShaderStage::Vertex, gx);
            } else if (tag == 'P' && std::fread(&ps, sizeof(ps), 1, file) == 1) {
                dump("ps", ps_count, ShaderGenerator::describe(ps), generator.generate_ps(ps));
                const std::string label = "captured ps " + std::to_string(ps_count++);
                const bool dual = (ps.flags & 0x80u) != 0u;
                expect(label.c_str(), generator.generate_ps(ps), ShaderStage::Fragment, gx, {}, dual);
                expect((label + " scalar masks").c_str(), scalar_masks.generate_ps(ps), ShaderStage::Fragment, gx,
                       {}, dual);
                expect((label + " constant TEV zero").c_str(), constant_zero.generate_ps(ps), ShaderStage::Fragment,
                       gx, {}, dual);
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
    expect("display filter ps", display_filter_shader_source(), ShaderStage::Fragment, conversion);

    // The utility passes: one texture and sampler, constants as push constants.
    constexpr RegisterBinding utility[] = {{"b0", 0, 0, true}, {"t0", 0, 0}, {"s0", 0, 1}};
    expect("blit vs", blit_vertex_shader_source(), ShaderStage::Vertex, utility);
    expect("blit ps", blit_pixel_shader_source(), ShaderStage::Fragment, utility);
    expect("clear ps", clear_pixel_shader_source(), ShaderStage::Fragment, utility);
    expect("clear depth ps", clear_depth_pixel_shader_source(), ShaderStage::Fragment, utility);
    expect("depth carry by load ps", depth_carry_load_pixel_shader_source(), ShaderStage::Fragment, utility);
    expect("depth carry ps", depth_carry_pixel_shader_source(), ShaderStage::Fragment, utility);
    expect("ui textured vs", ui_textured_quad_vertex_shader_source(), ShaderStage::Vertex, utility);
    expect("ui textured ps", ui_textured_quad_pixel_shader_source(), ShaderStage::Fragment, utility);
    expect("ui solid vs", ui_solid_quad_vertex_shader_source(), ShaderStage::Vertex, utility);
    expect("ui solid ps", ui_solid_quad_pixel_shader_source(), ShaderStage::Fragment, utility);

    // The game's bloom (game_bloom_vk.cpp): each pass at its entry point, its work images in their storage format.
    {
        static constexpr RegisterBinding kBloom[] = {{"b0", 0, 0, true}, {"t0", 0, 0}, {"t1", 0, 1}, {"t2", 0, 2},
                                                     {"u0", 0, 3},       {"u1", 0, 4}, {"u2", 0, 5}, {"s0", 1, 0}};
        const std::string source =
            annotate(reinterpret_cast<const char*>(galaxy::gx::galaxy_post_game_bloom_hlsl), kBloom);
        const std::pair<const char*, ShaderStage> passes[] = {
            {"DownCS", ShaderStage::Compute}, {"BoxCS", ShaderStage::Compute}, {"BlurCS", ShaderStage::Compute},
            {"CompositeVS", ShaderStage::Vertex}, {"CompositePS", ShaderStage::Fragment}};
        for (const auto& [entry, stage] : passes) {
            std::string error;
            if (compile_hlsl(source, stage, entry, {"BLOOM_STORAGE_FORMAT=1"}, error, {true, false, true}).empty()) {
                ++failures;
                std::printf("FAIL bloom %s\n%s\n", entry, error.c_str());
            }
        }
    }

    std::printf("%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
