// shader_gen_tests.cpp — offline validation that every ShaderGenerator
// codegen path emits HLSL that FXC accepts.
//
// The generator runs at boot and compiles synchronously on first PSO miss;
// a codegen syntax slip would otherwise surface as invisible geometry deep
// into a boot.  This test walks a key matrix covering: texgen types/sources
// (regular ST/STQ, per-vertex and default matrix indices, dual-tex post
// matrices, color0/1, emboss), per-channel lighting (every attenuation and
// diffuse function, color + alpha controls, multi-light masks), TEV compare
// modes (all four widths, GT and EQ, color and alpha), swap tables, every
// fog type, z-texturing, and alpha-test permutations — and feeds each
// generated shader through D3DCompile.
//
// Exit code = number of failures (0 = pass).

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#pragma warning(push, 0)
#include <Windows.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#pragma warning(pop)

#include "galaxy/gx/shader_gen.h"
#include "galaxy/gx/shader_keys.h"
#include "galaxy/gx/render_config.h"
#include "galaxy/gx/texture_sampling.h"
#include "galaxy/gx/shader_pair_compile.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>
#include <thread>

#pragma comment(lib, "d3dcompiler.lib")

namespace {

int g_failures = 0;

void check_compiles(
    const std::string& src,
    const char* target,
    const char* label) {
    Microsoft::WRL::ComPtr<ID3DBlob> code, errors;
    const HRESULT hr = D3DCompile(
        src.data(), src.size(), label, nullptr, nullptr, "main", target,
        D3DCOMPILE_ENABLE_STRICTNESS |
            D3DCOMPILE_PACK_MATRIX_ROW_MAJOR |
            D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0, &code, &errors);
    if (FAILED(hr)) {
        ++g_failures;
        std::printf("FAILED: %s (%s)\n%s\n--- source ---\n%s\n",
            label, target,
            errors ? static_cast<const char*>(errors->GetBufferPointer())
                   : "(no error blob)",
            src.c_str());
    } else {
        std::printf("ok: %s\n", label);
    }
}

// Raw XF channel-control word (gx_bitfields.h decode_xf_channel_ctrl):
// matsource(0) | enable(1) | lightmask0_3(2-5) | ambsource(6) |
// diffuse(7-8) | attn(9-10) | lightmask4_7(11-14).
std::uint32_t chan_ctrl(
    bool matsource_vtx,
    bool lighting,
    std::uint8_t light_mask,
    bool ambsource_vtx,
    unsigned diffuse,
    unsigned attn) {
    return (matsource_vtx ? 1u : 0u) |
           (lighting ? 2u : 0u) |
           ((light_mask & 0xFu) << 2) |
           (ambsource_vtx ? (1u << 6) : 0u) |
           ((diffuse & 3u) << 7) |
           ((attn & 3u) << 9) |
           ((static_cast<std::uint32_t>(light_mask) >> 4) << 11);
}

// Raw XF texgen word (decode_xf_texgen): stq(1) | abc1(2) | type(4-6) |
// source_row(7-11) | emboss_source(12-14) | emboss_light(15-17).
std::uint32_t texgen_word(
    bool stq,
    bool abc1,
    unsigned type,
    unsigned source_row,
    unsigned emboss_source = 0) {
    return (stq ? 2u : 0u) |
           (abc1 ? 4u : 0u) |
           ((type & 7u) << 4) |
           ((source_row & 0x1Fu) << 7) |
           ((emboss_source & 7u) << 12);
}

// Raw XF post-texgen word: index(0-5) | normalize(8).
std::uint32_t post_word(unsigned index, bool normalize) {
    return (index & 0x3Fu) | (normalize ? (1u << 8) : 0u);
}

void run_vs(const galaxy::gx::VertexShaderKey& key, const char* label) {
    const galaxy::gx::ShaderGenerator gen;
    check_compiles(gen.generate_vs(key), "vs_5_1", label);
}

void run_ps(const galaxy::gx::PixelShaderKey& key, const char* label) {
    const galaxy::gx::ShaderGenerator gen;
    check_compiles(gen.generate_ps(key), "ps_5_1", label);
}

void check_contains(
    const std::string& src,
    const char* expected,
    const char* label) {
    if (src.find(expected) == std::string::npos) {
        ++g_failures;
        std::printf("FAILED: %s missing \"%s\"\n", label, expected);
    } else {
        std::printf("ok: %s\n", label);
    }
}

void check_not_contains(
    const std::string& src,
    const char* unexpected,
    const char* label) {
    if (src.find(unexpected) != std::string::npos) {
        ++g_failures;
        std::printf("FAILED: %s unexpectedly contains \"%s\"\n",
                    label, unexpected);
    } else {
        std::printf("ok: %s\n", label);
    }
}

// One TEV stage with sane defaults: d=Zero c=Zero b=Zero a=Zero, lerp mode,
// clamp, dest PREV, texture enabled on map/coord 0, ras chan 0.
galaxy::gx::TevStagePacked basic_stage() {
    galaxy::gx::TevStagePacked st{};
    // color_env: a=TexColor(8) b=RasColor(10) c=Konst(14) d=CPrev(0),
    // bias=0, op add, clamp, scale x1, dest PREV.
    st.color_env = (0u) | (14u << 4) | (10u << 8) | (8u << 12) |
                   (0u << 16) | (1u << 19);
    // alpha_env: a=TexAlpha(4) b=RasAlpha(5) c=Konst(6) d=APrev(0).
    st.alpha_env = (0u << 4) | (6u << 7) | (5u << 10) | (4u << 13) |
                   (1u << 19);
    // order: texmap 0, texcoord 0, tex enable, ras chan 0.
    st.order = (1u << 6);
    // ksel: kcsel=12 (K0 rgb), kasel=31 (K3 a); identity swizzles.
    st.ksel = 12u | (31u << 5) |
              (0u << 10) | (1u << 12) | (2u << 14) | (3u << 16) |
              (0u << 18) | (1u << 20) | (2u << 22) | (3u << 24);
    return st;
}

void check_independent_pair_compilation() {
    using Blob = Microsoft::WRL::ComPtr<ID3DBlob>;
    const auto compile = [](
        const std::string& source, const char* target, const char* label) {
        Blob code, errors;
        const HRESULT hr = D3DCompile(
            source.data(), source.size(), label, nullptr, nullptr,
            "main", target,
            D3DCOMPILE_ENABLE_STRICTNESS |
                D3DCOMPILE_PACK_MATRIX_ROW_MAJOR |
                D3DCOMPILE_OPTIMIZATION_LEVEL3,
            0, &code, &errors);
        if (FAILED(hr)) {
            throw std::runtime_error("shader pair regression compile failed");
        }
        return code;
    };
    const auto equal = [](const Blob& a, const Blob& b) {
        return a && b && a->GetBufferSize() == b->GetBufferSize() &&
               std::memcmp(a->GetBufferPointer(), b->GetBufferPointer(),
                           a->GetBufferSize()) == 0;
    };
    const galaxy::gx::ShaderGenerator gen;
    try {
        for (unsigned profile = 0; profile < 4; ++profile) {
            galaxy::gx::VertexShaderKey vk{};
            vk.num_chans = 1;
            vk.channel[0] = chan_ctrl(true, false, 0, false, 0, 0);
            galaxy::gx::PixelShaderKey pk{};
            pk.num_tev_stages = 1;
            pk.num_texgens = 1;
            pk.num_chans = 1;
            pk.alpha_comp0 = 7;
            pk.alpha_comp1 = 7;
            pk.stages[0] = basic_stage();
            if (profile != 0) {
                vk.num_texgens = 8;
                vk.flags = 7;
                vk.texmtx_idx_mask = 0xFF;
                for (unsigned i = 0; i < 8; ++i) {
                    vk.texgen[i] = texgen_word(true, false, 0, 5 + i);
                    vk.post_texgen[i] = post_word(3 * i, true);
                }
                vk.channel[0] = chan_ctrl(false, true, 0xFF, false, 2, 1);
                pk.num_tev_stages = static_cast<std::uint8_t>(1 + 5 * profile);
                for (unsigned i = 1; i < pk.num_tev_stages; ++i) {
                    pk.stages[i] = basic_stage();
                    pk.stages[i].order |= i % 8;
                }
                pk.fog_type = 4;
            }
            const auto vs = gen.generate_vs(vk);
            const auto ps = gen.generate_ps(pk);
            const auto sequential_vs = compile(vs, "vs_5_1", "gx_vs");
            const auto sequential_ps = compile(ps, "ps_5_1", "gx_ps");
            const auto pair = galaxy::gx::compile_independent_shader_pair(
                vs, ps, compile);
            if (!equal(pair.first, sequential_vs) ||
                !equal(pair.second, sequential_ps)) {
                ++g_failures;
                std::printf("FAILED: shader_pair_bytecode_profile%u\n", profile);
            }
        }
    } catch (const std::exception& error) {
        ++g_failures;
        std::printf("FAILED: shader_pair_bytecode: %s\n", error.what());
    }

    // Prove exceptional cleanup, rather than assuming source references survive
    // a pixel-side failure while the independent compiler is still executing.
    std::promise<void> vertex_started;
    auto started = vertex_started.get_future();
    std::atomic<bool> vertex_completed{false};
    bool pixel_error_caught = false;
    try {
        (void)galaxy::gx::compile_independent_shader_pair(
            std::string{"vertex-owned"}, std::string{"pixel-owned"},
            [&](const std::string& source, const char* target, const char*) {
                if (std::strcmp(target, "vs_5_1") == 0) {
                    vertex_started.set_value();
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    const int result = source == "vertex-owned" ? 1 : 0;
                    vertex_completed.store(true, std::memory_order_release);
                    return result;
                }
                started.wait();
                throw std::runtime_error("pixel failure");
            });
    } catch (const std::runtime_error& error) {
        pixel_error_caught = std::strcmp(error.what(), "pixel failure") == 0;
    }
    if (!pixel_error_caught ||
        !vertex_completed.load(std::memory_order_acquire)) {
        ++g_failures;
        std::printf("FAILED: shader_pair_pixel_failure_joins_worker\n");
    }
    bool vertex_error_caught = false;
    bool pixel_completed = false;
    try {
        (void)galaxy::gx::compile_independent_shader_pair(
            std::string{"vertex"}, std::string{"pixel"},
            [&](const std::string&, const char* target, const char*) {
                if (std::strcmp(target, "vs_5_1") == 0) {
                    throw std::runtime_error("vertex failure");
                }
                pixel_completed = true;
                return 1;
            });
    } catch (const std::runtime_error& error) {
        vertex_error_caught = std::strcmp(error.what(), "vertex failure") == 0;
    }
    if (!vertex_error_caught || !pixel_completed) {
        ++g_failures;
        std::printf("FAILED: shader_pair_vertex_failure_propagated\n");
    }
    std::printf("shader pair bytecode and ownership checks completed\n");
}

}  // namespace

int main() {
    using namespace galaxy::gx;

    {   // Aurora GX pipeline normalization: RGB8/RGB565 have no EFB alpha.
        // Destination-alpha factors must share a PSO key with ONE/ZERO;
        // RGBA6 retains the original factors and remains a distinct key.
        GxState state;
        const auto write_bp = [&state](std::uint8_t reg, std::uint32_t value) {
            state.load_bp((static_cast<std::uint32_t>(reg) << 24u) | value);
        };
        constexpr std::uint32_t kDstAlphaBlend =
            1u | (6u << 8u) | (7u << 5u);
        constexpr std::uint32_t kNormalizedBlend =
            1u | (1u << 8u) | (0u << 5u);
        write_bp(bp::kBlendMode, kDstAlphaBlend);
        const RenderStateKey rgb8 = build_render_state_key(state);
        write_bp(bp::kBlendMode, kNormalizedBlend);
        const RenderStateKey rgb8_normalized = build_render_state_key(state);
        write_bp(bp::kPeControl, 2u);  // RGB565_Z16 also lacks EFB alpha.
        write_bp(bp::kBlendMode, kDstAlphaBlend);
        const RenderStateKey rgb565 = build_render_state_key(state);
        write_bp(bp::kPeControl, 1u);  // RGBA6_Z24 has EFB alpha.
        const RenderStateKey rgba6 = build_render_state_key(state);
        if (!(rgb8 == rgb8_normalized) ||
            ((rgb565.blend_bits >> 2u) & 7u) != 1u ||
            ((rgb565.blend_bits >> 5u) & 7u) != 0u ||
            ((rgba6.blend_bits >> 2u) & 7u) != 6u ||
            ((rgba6.blend_bits >> 5u) & 7u) != 7u) {
            ++g_failures;
            std::printf("FAILED: no-alpha EFB blend key normalization\n");
        }
    }

    {
        GxState state;
        const auto write_bp = [&state](std::uint8_t reg, std::uint32_t value) {
            state.load_bp((static_cast<std::uint32_t>(reg) << 24u) | value);
        };
        write_bp(bp::kTevOrderBase, 1u | (1u << 6u));
        write_bp(bp::kGenMode, (1u << 16u) | 1u);
        write_bp(bp::kIndRef, 5u);
        if (sampled_texture_map_mask(state) !=
            static_cast<std::uint8_t>((1u << 1u) | (1u << 5u))) {
            ++g_failures;
            std::printf("FAILED: indirect texmap included in sampled bindings\n");
        }
        write_bp(bp::kGenMode, 1u << 16u);
        if (sampled_texture_map_mask(state) != static_cast<std::uint8_t>(1u << 5u)) {
            ++g_failures;
            std::printf("FAILED: no-texgen draw bound unused direct map\n");
        }
        write_bp(bp::kGenMode, (1u << 16u) | 1u);

        // Shader key generation extends the indirect-stage count when a TEV
        // command references a stage beyond the GenMode count. The binding
        // and snapshot mask must cover exactly those emitted samples too.
        write_bp(bp::kIndRef, 5u | (6u << 6u) | (7u << 12u));
        write_bp(bp::kIndCmdBase, 2u);
        if (sampled_texture_map_mask(state) !=
            static_cast<std::uint8_t>(
                (1u << 1u) | (1u << 5u) | (1u << 6u) | (1u << 7u))) {
            ++g_failures;
            std::printf("FAILED: command-extended indirect texmaps sampled\n");
        }
    }

    {
        GxState state;
        const auto write_bp = [&state](std::uint8_t reg, std::uint32_t value) {
            state.load_bp((static_cast<std::uint32_t>(reg) << 24u) | value);
        };
        constexpr auto map_bit = static_cast<std::uint8_t>(1u << 6u);
        write_bp(bp::kGenMode, 1u);
        write_bp(bp::kTevOrderBase, 6u | (1u << 6u));
        if (sampled_texture_map_mask(state) != 0u) {
            ++g_failures;
            std::printf("FAILED: dead texture stage bound leftover map\n");
        }
        // Every selector position, including alpha-only texture consumers.
        // Other operands remain nontexture register inputs.
        for (unsigned input = 0; input < 4u; ++input) {
            for (unsigned value = 0; value < 16u; ++value) {
                write_bp(bp::kTevColorEnvBase, value << (input * 4u));
                const auto expected = static_cast<std::uint8_t>(
                    value == 8u || value == 9u ? map_bit : 0u);
                if (sampled_texture_map_mask(state) != expected) {
                    ++g_failures;
                    std::printf("FAILED: TEXC/TEXA color selector binding\n");
                }
            }
        }
        write_bp(bp::kTevColorEnvBase, 0u);
        for (unsigned input = 0; input < 4u; ++input) {
            for (unsigned value = 0; value < 8u; ++value) {
                write_bp(bp::kTevAlphaEnvBase, value << (4u + input * 3u));
                const auto expected = static_cast<std::uint8_t>(value == 4u ? map_bit : 0u);
                if (sampled_texture_map_mask(state) != expected) {
                    ++g_failures;
                    std::printf("FAILED: TEXA alpha selector binding\n");
                }
            }
        }
        write_bp(bp::kTevAlphaEnvBase, 0u);
        write_bp(bp::kTevZEnv1, 4u);
        if (sampled_texture_map_mask(state) != map_bit) {
            ++g_failures;
            std::printf("FAILED: z-texture retains raw sample without TEX operands\n");
        }
        write_bp(bp::kGenMode, 0u);
        if (sampled_texture_map_mask(state) != 0u) {
            ++g_failures;
            std::printf("FAILED: no texgens cannot supply z-texture raw sample\n");
        }
        for (unsigned stage = 0; stage < kMaxTevStages; ++stage) {
            GxState staged;
            staged.load_bp((static_cast<std::uint32_t>(bp::kGenMode) << 24u) |
                           (stage << 10u) | 1u);
            const unsigned map = stage & 7u;
            staged.load_bp((static_cast<std::uint32_t>(bp::kTevOrderBase + stage / 2u) << 24u) |
                           ((map | (1u << 6u)) << ((stage & 1u) * 12u)));
            staged.load_bp((static_cast<std::uint32_t>(bp::kTevColorEnvBase + 2u * stage) << 24u) |
                           8u);
            if (sampled_texture_map_mask(staged) != static_cast<std::uint8_t>(1u << map)) {
                ++g_failures;
                std::printf("FAILED: live texture stage/map coverage\n");
            }
        }
    }

    check_compiles(
        std::string{ShaderGenerator::line_geometry_shader_source()},
        "gs_5_1",
        "gs_gx_line_expansion");
    check_compiles(
        std::string{ShaderGenerator::point_geometry_shader_source()},
        "gs_5_1",
        "gs_gx_point_expansion");

    // ---- Vertex shaders ----------------------------------------------------

    {   // Minimal: no texgens, one unlit channel.
        VertexShaderKey k{};
        std::memset(&k, 0, sizeof(k));
        k.num_chans = 1;
        k.channel[0] = chan_ctrl(true, false, 0, false, 0, 0);
        const ShaderGenerator gen;
        const std::string src = gen.generate_vs(k);
        check_contains(src, "dot(inline_pos_matrix[0]",
                       "vs_fixed_position_uses_inline_matrix");
        check_not_contains(src, "uint pnmtx_row",
                           "vs_fixed_position_skips_palette_lookup");
        run_vs(k, "vs_minimal");
    }

    // Lighting: one VS per attenuation function x diffuse function, with a
    // multi-light mask and a lit alpha channel.
    for (unsigned attn = 0; attn < 4; ++attn) {
        for (unsigned diff = 0; diff < 3; ++diff) {
            VertexShaderKey k{};
            std::memset(&k, 0, sizeof(k));
            k.num_chans = 2;
            k.flags = 0x02;  // lighting
            k.channel[0] = chan_ctrl(false, true, 0x05, false, diff, attn);
            k.channel[1] = chan_ctrl(true, false, 0, false, 0, 0);
            k.channel[2] = chan_ctrl(false, true, 0x80, true, diff, attn);
            k.channel[3] = chan_ctrl(false, false, 0, false, 0, 0);
            char label[64];
            std::snprintf(label, sizeof(label),
                          "vs_light_attn%u_diff%u", attn, diff);
            run_vs(k, label);
        }
    }

    {   // Texgens: regular ST (tex0 source), regular STQ (geom source,
        // ABC1), per-vertex texmtx on gen 0, dual-tex post matrices with
        // normalize, color0 texgen, emboss, normal source.
        VertexShaderKey k{};
        std::memset(&k, 0, sizeof(k));
        k.num_texgens = 6;
        k.num_chans = 1;
        k.flags = 0x01 | 0x04;       // per-vertex posmtx + dual-tex
        k.texmtx_idx_mask = 0x01;    // gen 0 takes the per-vertex index
        k.texgen[0] = texgen_word(false, false, 0, 5);   // ST from tex0
        k.texgen[1] = texgen_word(true, true, 0, 0);     // STQ from geom
        k.texgen[2] = texgen_word(false, false, 2, 2);   // color0
        k.texgen[3] = texgen_word(true, false, 0, 1);    // STQ from normal
        k.texgen[4] = texgen_word(false, false, 1, 5, 0); // emboss
        k.texgen[5] = texgen_word(false, false, 0, 12);  // ST from tex7
        k.post_texgen[0] = post_word(61, false);
        k.post_texgen[1] = post_word(0, true);
        k.post_texgen[3] = post_word(12, true);
        k.channel[0] = chan_ctrl(true, false, 0, false, 0, 0);
        const ShaderGenerator gen;
        const std::string src = gen.generate_vs(k);
        check_contains(src, "uint pnmtx_row",
                       "vs_indexed_position_uses_palette");
        check_contains(src, "mtx_palette[tm+0u]",
                       "vs_indexed_texmatrix_uses_palette");
        check_contains(src, "inline_post_matrices[0]",
                       "vs_postmatrix_uses_inline_constants");
        check_not_contains(src, "0x140",
                           "vs_postmatrix_skips_palette_lookup");
        check_contains(src, "o.uv[4] = o.uv[0] + float3(dot(emboss_ldir, emboss_tangent)",
                       "vs_emboss_offsets_source_with_transformed_basis");
        check_contains(src, "mul(emboss_matrix, v.binormal)",
                       "vs_emboss_preserves_basis_scale");
        check_contains(src, "emboss_dist2 > 0.0",
                       "vs_emboss_guards_zero_light_distance");
        run_vs(k, "vs_texgen_matrix");
    }

    {   // All 8 texgens, default (MatrixIndexA/B) indices, no dual-tex.
        VertexShaderKey k{};
        std::memset(&k, 0, sizeof(k));
        k.num_texgens = 8;
        k.num_chans = 1;
        for (unsigned i = 0; i < 8; ++i) {
            k.texgen[i] = texgen_word((i & 1u) != 0u, false, 0, 5u + i);
        }
        k.channel[0] = chan_ctrl(false, false, 0, false, 0, 0);
        const ShaderGenerator gen;
        const std::string src = gen.generate_vs(k);
        check_contains(src, "inline_tex_matrices[21]",
                       "vs_fixed_texmatrices_use_inline_constants");
        check_not_contains(src, "uint tm = min((matrix_index",
                           "vs_fixed_texmatrices_skip_palette_lookup");
        run_vs(k, "vs_texgen_x8");
    }

    // ---- Pixel shaders ------------------------------------------------------

    {   // Baseline lerp stage, no fog, trivial alpha test.
        PixelShaderKey k{};
        std::memset(&k, 0, sizeof(k));
        k.num_tev_stages = 1;
        k.num_texgens = 1;
        k.num_chans = 1;
        k.alpha_comp0 = 7;  // Always
        k.alpha_comp1 = 7;
        k.stages[0] = basic_stage();
        const ShaderGenerator gen;
        const std::string src = gen.generate_ps(k);
        check_contains(src,
                       "tex_fix = int2(round(tuv * tex_dims[0].zw * 128.0));",
                       "ps_regular_texture_uses_texcoord_scale");
        check_contains(src,
                       "tex_tuv = (float2(tex_fix) / 128.0) / tex_dims[0].xy;",
                       "ps_regular_texture_normalizes_by_texmap_size");
        check_compiles(src, "ps_5_1", "ps_baseline");
    }

    {   // The final TEV stage reaches the PE even when it writes REG0-2.
        // Crystal/highlight materials can end on a non-PREV destination; the
        // shader must copy that final destination before alpha/fog/output.
        PixelShaderKey k{};
        std::memset(&k, 0, sizeof(k));
        k.num_tev_stages = 1;
        k.num_texgens = 1;
        k.num_chans = 1;
        k.alpha_comp0 = 7;
        k.alpha_comp1 = 7;
        k.stages[0] = basic_stage();
        k.stages[0].color_env =
            (k.stages[0].color_env & ~(3u << 22)) | (1u << 22);
        k.stages[0].alpha_env =
            (k.stages[0].alpha_env & ~(3u << 22)) | (2u << 22);
        const ShaderGenerator gen;
        const std::string src = gen.generate_ps(k);
        check_contains(src, "tev_prev_i.rgb = tev_c_i[0].rgb;",
                       "ps_last_tev_color_reg0_becomes_output");
        check_contains(src, "tev_prev_i.a = tev_c_i[1].a;",
                       "ps_last_tev_alpha_reg1_becomes_output");
        check_compiles(src, "ps_5_1", "ps_last_tev_nonprev_dest_compiles");
    }

    {   // Indirect TEV: sample an indirect stage, apply bias/matrix/wrap,
        // and feed alpha bump through the ras channel.
        PixelShaderKey k{};
        std::memset(&k, 0, sizeof(k));
        k.num_tev_stages = 1;
        k.num_texgens = 1;
        k.num_chans = 1;
        k.num_ind_stages = 1;
        k.alpha_comp0 = 7;
        k.alpha_comp1 = 7;
        k.ind_cmd[0][0] =
            (0u) |        // indirect stage 0
            (0u << 2) |   // ITF_8
            (3u << 4) |   // bias ST
            (1u << 7) |   // bump alpha from S
            (1u << 9) |   // matrix 0
            (1u << 13) |  // wrap S 256
            (1u << 16);   // wrap T 256
        TevStagePacked st = basic_stage();
        st.order = (st.order & ~(7u << 7)) | (5u << 7);
        k.stages[0] = st;
        const ShaderGenerator gen;
        const std::string src = gen.generate_ps(k);
        check_contains(src, "ind_tex_i[0]",
                       "ps_indirect_samples_ind_stage");
        check_contains(src, "int2 ind_fix",
                       "ps_indirect_uses_fixed_point_sample_coord");
        check_contains(src, "ind_uv * tex_dims[0].zw * 128.0",
                       "ps_indirect_uses_texcoord_scale_for_ind_stage");
        check_contains(src, "int2 tevcoord_i",
                       "ps_indirect_declares_persistent_tevcoord");
        check_contains(src, "ind_mtx[0]",
                       "ps_indirect_uses_matrix_constant");
        check_contains(src, "float2 ind_tuv = (float2(tevcoord_i) / 128.0)",
                       "ps_indirect_texture_sample_uses_persistent_tevcoord");
        check_contains(src, "/ tex_dims[0].xy",
                       "ps_indirect_normalizes_sample_by_texmap_size");
        check_contains(src, "alphabump_i",
                       "ps_indirect_uses_alpha_bump");
        check_compiles(src, "ps_5_1", "ps_indirect_tev_compiles");
    }

    {   // Indirect texcoord scale is a 4-bit GX shift. Values above 8 are
        // legal and must not be clamped, or subtle effect maps become wildly
        // over-amplified.
        PixelShaderKey k{};
        std::memset(&k, 0, sizeof(k));
        k.num_tev_stages = 1;
        k.num_texgens = 1;
        k.num_chans = 1;
        k.num_ind_stages = 1;
        k.alpha_comp0 = 7;
        k.alpha_comp1 = 7;
        k.ind_scale[0] = 15u | (14u << 4);
        k.ind_cmd[0][0] =
            (0u) |        // indirect stage 0
            (0u << 2) |   // ITF_8
            (1u << 9);    // matrix 0, active
        k.stages[0] = basic_stage();
        const ShaderGenerator gen;
        const std::string src = gen.generate_ps(k);
        check_contains(src,
                       "ind_fix = int2(ind_fix.x >> 15, ind_fix.y >> 14);",
                       "ps_indirect_scale_keeps_full_4bit_shift");
        check_compiles(src, "ps_5_1", "ps_indirect_scale_15_compiles");
    }

    {   // libogc maps GX_ALPHA_BUMP, GX_ALPHA_BUMPN, and GX_COLORZERO to BP
        // raster channels 5, 6, and 7. Lock that mapping so translucent
        // indirect materials do not regress to zero or the wrong bump mode.
        PixelShaderKey k{};
        std::memset(&k, 0, sizeof(k));
        k.num_tev_stages = 3;
        k.num_texgens = 1;
        k.num_chans = 1;
        k.num_ind_stages = 1;
        k.alpha_comp0 = 7;
        k.alpha_comp1 = 7;
        k.ind_cmd[0][0] = (0u) | (1u << 7);  // bump alpha from S
        k.ind_cmd[0][1] = k.ind_cmd[0][0];
        k.ind_cmd[0][2] = k.ind_cmd[0][0];
        k.stages[0] = basic_stage();
        k.stages[0].order = (k.stages[0].order & ~(7u << 7)) | (5u << 7);
        k.stages[1] = basic_stage();
        k.stages[1].order = (k.stages[1].order & ~(7u << 7)) | (6u << 7);
        k.stages[2] = basic_stage();
        k.stages[2].order = (k.stages[2].order & ~(7u << 7)) | (7u << 7);
        const ShaderGenerator gen;
        const std::string src = gen.generate_ps(k);
        check_contains(src,
                       "int4(alphabump_i, alphabump_i, alphabump_i, alphabump_i)",
                       "ps_ras_channel_5_is_alpha_bump");
        check_contains(src, "int abn = alphabump_i | (alphabump_i >> 5);",
                       "ps_ras_channel_6_is_normalized_alpha_bump");
        check_contains(src, "ras_color_i = int4(0,0,0,0);",
                       "ps_ras_channel_7_is_zero");
        check_compiles(src, "ps_5_1", "ps_ras_channel_bump_mapping_compiles");
    }

    {   // GX still applies an active indirect coordinate op even when that
        // TEV stage has its regular texture lookup disabled. This keeps wrap
        // and add-prev state faithful instead of treating disabled texture as
        // disabled indirect.
        PixelShaderKey k{};
        std::memset(&k, 0, sizeof(k));
        k.num_tev_stages = 1;
        k.num_texgens = 1;
        k.num_chans = 1;
        k.num_ind_stages = 1;
        k.alpha_comp0 = 7;
        k.alpha_comp1 = 7;
        k.ind_cmd[0][0] =
            (0u) |        // indirect stage 0
            (0u << 2) |   // ITF_8
            (3u << 4) |   // bias ST
            (1u << 9) |   // matrix 0
            (2u << 13) |  // wrap S 128
            (2u << 16);   // wrap T 128
        k.stages[0] = basic_stage();
        k.stages[0].order &= ~(1u << 6);  // texture disabled
        const ShaderGenerator gen;
        const std::string src = gen.generate_ps(k);
        check_contains(src, "wrappedcoord.x = stage_tevcoord.x &",
                       "ps_indirect_disabled_texture_still_wraps_s");
        check_contains(src, "wrappedcoord.y = stage_tevcoord.y &",
                       "ps_indirect_disabled_texture_still_wraps_t");
        check_contains(src, "tevcoord_i = wrappedcoord + indtevtrans",
                       "ps_indirect_disabled_texture_updates_tevcoord");
        check_compiles(src, "ps_5_1",
                       "ps_indirect_disabled_texture_op_compiles");
    }

    {   // Indirect TEV coordinate state persists across stages. The first
        // stage disables the normal texture lookup but still mutates the
        // indirect TEV coordinate; the second stage adds to that previous
        // coordinate before sampling.
        PixelShaderKey k{};
        std::memset(&k, 0, sizeof(k));
        k.num_tev_stages = 2;
        k.num_texgens = 1;
        k.num_chans = 1;
        k.num_ind_stages = 1;
        k.alpha_comp0 = 7;
        k.alpha_comp1 = 7;
        k.ind_cmd[0][0] =
            (0u) |        // indirect stage 0
            (0u << 2) |   // ITF_8
            (3u << 4) |   // bias ST
            (1u << 9) |   // matrix 0
            (1u << 13) |  // wrap S 256
            (1u << 16);   // wrap T 256
        k.ind_cmd[0][1] = k.ind_cmd[0][0] | (1u << 20);  // add previous
        k.stages[0] = basic_stage();
        k.stages[0].order &= ~(1u << 6);  // texture disabled
        k.stages[1] = basic_stage();
        const ShaderGenerator gen;
        const std::string src = gen.generate_ps(k);
        check_contains(src, "tevcoord_i = wrappedcoord + indtevtrans",
                       "ps_indirect_disabled_stage_updates_tevcoord");
        check_contains(src, "tevcoord_i += wrappedcoord + indtevtrans",
                       "ps_indirect_addprev_uses_persistent_tevcoord");
        check_compiles(src, "ps_5_1", "ps_indirect_persistent_tevcoord_compiles");
    }

    {   // Hardware falls back to texcoord 0 when an indirect order points past
        // the active texgen count. Keep that explicit so effects don't sample
        // an uninitialized semantic lane.
        PixelShaderKey k{};
        std::memset(&k, 0, sizeof(k));
        k.num_tev_stages = 1;
        k.num_texgens = 1;
        k.num_chans = 1;
        k.num_ind_stages = 1;
        k.alpha_comp0 = 7;
        k.alpha_comp1 = 7;
        k.ind_ref = (7u << 3);  // indirect stage 0 uses invalid texcoord 7.
        k.ind_cmd[0][0] =
            (0u) |        // indirect stage 0
            (0u << 2) |   // ITF_8
            (1u << 9);    // matrix 0
        k.stages[0] = basic_stage();
        k.stages[0].order = (k.stages[0].order & ~(7u << 3)) | (7u << 3);
        const ShaderGenerator gen;
        const std::string src = gen.generate_ps(k);
        check_contains(src, "float3 ind_tq = pin.uv[0];",
                       "ps_indirect_invalid_indref_texcoord_uses_zero");
        check_contains(src, "float3 tq = pin.uv[0];",
                       "ps_indirect_invalid_stage_texcoord_uses_zero");
        check_compiles(src, "ps_5_1", "ps_indirect_invalid_texcoord_compiles");
    }

    {   // GX konst selector numbering: fixed fractions, K RGB, and
        // component replication must match the hardware table exactly.
        PixelShaderKey k{};
        std::memset(&k, 0, sizeof(k));
        k.num_tev_stages = 1;
        k.num_texgens = 1;
        k.alpha_comp0 = 7;
        k.alpha_comp1 = 7;
        k.stages[0] = basic_stage();
        const ShaderGenerator gen;
        const std::string src = gen.generate_ps(k);
        check_contains(src, "int3(round(saturate(konst[0].rgb) * 255.0))",
                       "ps_konst_color_k0_rgb");
        check_contains(src, "int(round(saturate(konst[3].a) * 255.0))",
                       "ps_konst_alpha_k3_a");

        k.stages[0].ksel = 0u | (7u << 5);
        const std::string fixed_src = gen.generate_ps(k);
        check_contains(fixed_src, "konst_i = int4(int3(255,255,255), 32);",
                       "ps_konst_color_fixed_one");
        check_contains(fixed_src, ", 32);",
                       "ps_konst_alpha_fixed_eighth");
    }

    {   // RGBA6 output quantizes RGB/alpha and applies hardware dither.
        PixelShaderKey k{};
        std::memset(&k, 0, sizeof(k));
        k.num_tev_stages = 1;
        k.num_texgens = 1;
        k.alpha_comp0 = 7;
        k.alpha_comp1 = 7;
        k.output_flags = 1u | 0x08u;
        k.stages[0] = basic_stage();
        const ShaderGenerator gen;
        constexpr unsigned kExpectedDither[2][2] = {{0u, 2u}, {3u, 1u}};
        for (unsigned scale = 1u;
             scale <= galaxy::gx::kMaxEfbScale; ++scale) {
            k.efb_scale_minus_one =
                static_cast<std::uint8_t>(scale - 1u);
            const std::string src = gen.generate_ps(k);
            char label[64]{};
            char expected_coord[80]{};
            std::snprintf(
                label, sizeof(label), "ps_rgba6_dither_%ux", scale);
            if (scale == 1u) {
                std::snprintf(
                    expected_coord,
                    sizeof(expected_coord),
                    "int2 dither_xy = int2(pin.pos.xy) & 1;");
            } else {
                std::snprintf(
                    expected_coord,
                    sizeof(expected_coord),
                    "int2 dither_xy = (int2(pin.pos.xy) / %u) & 1;",
                    scale);
            }
            check_contains(src, expected_coord, label);
            check_contains(
                src,
                "dither_rgb = (dither_rgb - (dither_rgb >> 6))",
                label);
            check_contains(
                src, "float3(efb_color.rgb >> 2) / 63.0", label);
            check_contains(
                src, "float(efb_color.a >> 2) / 63.0", label);

            bool pattern_matches = true;
            for (unsigned logical_y = 0u; logical_y < 2u; ++logical_y) {
                for (unsigned logical_x = 0u; logical_x < 2u; ++logical_x) {
                    for (unsigned sub_y = 0u; sub_y < scale; ++sub_y) {
                        for (unsigned sub_x = 0u; sub_x < scale; ++sub_x) {
                            const unsigned physical_x = logical_x * scale + sub_x;
                            const unsigned physical_y = logical_y * scale + sub_y;
                            const unsigned dither_x = physical_x / scale;
                            const unsigned dither_y = physical_y / scale;
                            const unsigned value =
                                ((dither_x ^ dither_y) & 1u) * 2u +
                                (dither_y & 1u);
                            pattern_matches &=
                                value == kExpectedDither[logical_y][logical_x];
                        }
                    }
                }
            }
            if (!pattern_matches) {
                ++g_failures;
                std::printf(
                    "FAILED: %s does not preserve the logical 2x2 pattern\n",
                    label);
            } else {
                std::printf(
                    "ok: %s preserves the logical 2x2 pattern\n", label);
            }
            std::snprintf(
                label, sizeof(label), "ps_rgba6_output_%ux_compiles", scale);
            check_compiles(src, "ps_5_1", label);
        }
        k.efb_scale_minus_one = 0xffu;
        const auto uniform_src = gen.generate_ps(k);
        check_contains(uniform_src,
            "int2 dither_xy = int2(pin.pos.xy * ztex_params.y) & 1;",
            "ps_rgba6_uniform_dither");
        check_compiles(uniform_src, "ps_5_1", "ps_rgba6_uniform_dither_compiles");

        // Pixel centers are half-integral, unlike edge coordinates. Exhaust
        // both axes' full physical extent at every supported scale, including
        // non-power-of-two reciprocal rounding and the last pixel.
        bool coordinates_match = true;
        for (unsigned scale = 1u; scale <= galaxy::gx::kMaxEfbScale; ++scale) {
            const float reciprocal = 1.0f / static_cast<float>(scale);
            for (unsigned n = 0u; n < 640u * scale; ++n) {
                const float center = static_cast<float>(n) + 0.5f;
                coordinates_match &= static_cast<unsigned>(center * reciprocal) == n / scale;
            }
        }
        if (!coordinates_match) {
            ++g_failures;
            std::printf("FAILED: uniform dither crosses a logical pixel boundary\n");
        }
        GxState state;
        state.load_bp((static_cast<std::uint32_t>(bp::kPeControl) << 24u) | 1u);
        state.load_bp((static_cast<std::uint32_t>(bp::kBlendMode) << 24u) | (1u << 2u));
        const auto canonical = build_pixel_shader_key(state, 1u);
        bool keys_match = canonical.efb_scale_minus_one == 0xffu;
        for (unsigned scale = 1u; scale <= galaxy::gx::kMaxEfbScale; ++scale) {
            keys_match &= canonical == build_pixel_shader_key(state, scale);
            auto legacy = canonical;
            legacy.efb_scale_minus_one = static_cast<std::uint8_t>(scale - 1u);
            keys_match &= !(canonical == legacy) && canonical.hash() != legacy.hash();
        }
        if (!keys_match) {
            ++g_failures;
            std::printf("FAILED: scale-independent key aliases a legacy shader\n");
        }
        std::printf("uniform dither: physical coordinates=%s canonical keys=%s\n",
            coordinates_match ? "PASS" : "FAIL", keys_match ? "PASS" : "FAIL");
    }

    {   // A disabled texture stage is white when texgens exist.
        PixelShaderKey k{};
        std::memset(&k, 0, sizeof(k));
        k.num_tev_stages = 1;
        k.num_texgens = 1;
        k.alpha_comp0 = 7;
        k.alpha_comp1 = 7;
        k.stages[0] = basic_stage();
        k.stages[0].order &= ~(1u << 6);
        const ShaderGenerator gen;
        const std::string src = gen.generate_ps(k);
        check_contains(
            src, "tex_color_i = int4(255,255,255,255);",
            "ps_disabled_texture_with_texgen_is_white");
        check_compiles(
            src, "ps_5_1", "ps_disabled_texture_with_texgen_compiles");
    }

    {   // With no texgens at all, the disabled texture value is black.
        PixelShaderKey k{};
        std::memset(&k, 0, sizeof(k));
        k.num_tev_stages = 1;
        k.alpha_comp0 = 7;
        k.alpha_comp1 = 7;
        k.stages[0] = basic_stage();
        k.stages[0].order &= ~(1u << 6);
        const ShaderGenerator gen;
        const std::string src = gen.generate_ps(k);
        check_contains(
            src, "tex_color_i = int4(0,0,0,0);",
            "ps_disabled_texture_without_texgen_is_black");
        check_compiles(
            src, "ps_5_1", "ps_disabled_texture_without_texgen_compiles");
    }

    // Compare modes: color widths R8/GR16/BGR24/RGB8 x GT/EQ, with the
    // matching alpha compare (incl. the A8 kind = value 3 for alpha).
    for (unsigned kind = 0; kind < 4; ++kind) {
        for (unsigned eq = 0; eq < 2; ++eq) {
            PixelShaderKey k{};
            std::memset(&k, 0, sizeof(k));
            k.num_tev_stages = 1;
            k.num_chans = 1;
            k.alpha_comp0 = 7;
            k.alpha_comp1 = 7;
            TevStagePacked st = basic_stage();
            // bias=Compare(3), op bit = eq, scale field = compare kind.
            st.color_env = (st.color_env & ~((3u << 16) | (1u << 18) |
                                             (3u << 20))) |
                           (3u << 16) | (eq << 18) | (kind << 20);
            st.alpha_env = (st.alpha_env & ~((3u << 16) | (1u << 18) |
                                             (3u << 20))) |
                           (3u << 16) | (eq << 18) | (kind << 20);
            k.stages[0] = st;
            char label[64];
            std::snprintf(label, sizeof(label),
                          "ps_compare_kind%u_%s", kind, eq ? "eq" : "gt");
            run_ps(k, label);
        }
    }

    {   // Non-identity swap tables on both ras and tex.
        PixelShaderKey k{};
        std::memset(&k, 0, sizeof(k));
        k.num_tev_stages = 2;
        k.num_chans = 2;
        k.alpha_comp0 = 7;
        k.alpha_comp1 = 7;
        TevStagePacked st = basic_stage();
        // ras swizzle = .gggb, tex swizzle = .rrra (colorize-intensity use).
        st.ksel = (st.ksel & 0x3FFu) |
                  (1u << 10) | (1u << 12) | (1u << 14) | (2u << 16) |
                  (0u << 18) | (0u << 20) | (0u << 22) | (3u << 24);
        k.stages[0] = st;
        TevStagePacked st1 = basic_stage();
        st1.order |= 1u << 7;  // ras chan 1
        k.stages[1] = st1;
        run_ps(k, "ps_swap_tables");
    }

    // Every fog type (with a real alpha test so both paths coexist).
    for (const unsigned fog : {2u, 4u, 5u, 6u, 7u}) {
        PixelShaderKey k{};
        std::memset(&k, 0, sizeof(k));
        k.num_tev_stages = 1;
        k.num_chans = 1;
        k.fog_type = static_cast<std::uint8_t>(fog);
        k.alpha_comp0 = 4;  // Greater
        k.alpha_comp1 = 7;
        k.alpha_logic = 0;
        k.stages[0] = basic_stage();
        char label[64];
        std::snprintf(label, sizeof(label), "ps_fog_type%u", fog);
        run_ps(k, label);
    }

    {   // Z-texture replace (Z24X8) + dst-alpha + 16 stages.
        PixelShaderKey k{};
        std::memset(&k, 0, sizeof(k));
        k.num_tev_stages = 16;
        k.num_chans = 1;
        k.alpha_comp0 = 7;
        k.alpha_comp1 = 7;
        k.flags = 0x01 | 0x02 | 0x04 | (2u << 3) | (2u << 5);
        for (unsigned i = 0; i < 16; ++i) {
            k.stages[i] = basic_stage();
        }
        run_ps(k, "ps_ztex_16stages");
    }

    {   // Dual-source dst-alpha: SV_Target0 carries the constant alpha,
        // SV_Target1 the TEV alpha for the SRC1 blend factors.
        PixelShaderKey k{};
        std::memset(&k, 0, sizeof(k));
        k.num_tev_stages = 1;
        k.num_texgens = 1;
        k.num_chans = 1;
        k.alpha_comp0 = 7;
        k.alpha_comp1 = 7;
        k.flags = 0x04 | 0x80;
        k.stages[0] = basic_stage();
        const ShaderGenerator gen;
        const std::string src = gen.generate_ps(k);
        check_contains(src, "float4 color1 : SV_Target1;",
                       "ps_dual_src_declares_target1");
        check_contains(src, "float tev_alpha = saturate(frag.a);",
                       "ps_dual_src_preserves_tev_alpha");
        check_contains(src, "uint dst_alpha6 = uint(round(saturate(alpha_refs.w)",
                       "ps_dual_src_writes_constant");
        check_compiles(src, "ps_5_1", "ps_dual_src_compiles");
    }

    {   // Dual-source dst-alpha combined with Z-texturing (PSOutZ + Target1
        // + SV_Depth in one signature).
        PixelShaderKey k{};
        std::memset(&k, 0, sizeof(k));
        k.num_tev_stages = 1;
        k.num_texgens = 1;
        k.num_chans = 1;
        k.alpha_comp0 = 7;
        k.alpha_comp1 = 7;
        k.flags = 0x01 | 0x02 | 0x04 | 0x80 | (2u << 3) | (2u << 5);
        k.stages[0] = basic_stage();
        const ShaderGenerator gen;
        const std::string src = gen.generate_ps(k);
        check_contains(src, "float4 color1 : SV_Target1;",
                       "ps_dual_src_ztex_declares_target1");
        check_compiles(src, "ps_5_1", "ps_dual_src_ztex_compiles");
    }

    {
        // Independent raw VAT fixture: Tex4 fraction is C[4:0], not B[31].
        static_assert(cmpr_interpolated_channel(255u, 0u) == 159u);
        static_assert(cmpr_interpolated_channel(0u, 255u) == 95u);
        static_assert(cmpr_interpolated_channel(248u, 8u) == 158u);
        for (const auto primitive : {PrimitiveClass::Triangles,
             PrimitiveClass::TriangleStrip, PrimitiveClass::TriangleFan,
             PrimitiveClass::Quads, PrimitiveClass::Quads2}) {
            if (!cull_all_suppresses_primitive(CullMode::All, primitive) ||
                cull_all_suppresses_primitive(CullMode::Front, primitive) ||
                cull_all_suppresses_primitive(CullMode::Back, primitive) ||
                cull_all_suppresses_primitive(CullMode::None, primitive)) {
                ++g_failures;
                std::printf("FAILED: polygon cull-all classification\n");
            }
        }
        for (const auto primitive : {PrimitiveClass::Lines,
             PrimitiveClass::LineStrip, PrimitiveClass::Points}) {
            if (cull_all_suppresses_primitive(CullMode::All, primitive)) {
                ++g_failures;
                std::printf("FAILED: cull-all suppressed nonpolygon primitive\n");
            }
        }
        const std::uint32_t c = 17u | (0x155u << 5u) |
            (0x0ABu << 14u) | (0x1D3u << 23u);
        for (const std::uint32_t enhance : {0u, 1u << 31u}) {
            const auto d = decode_vertex_descriptor(0u, 0x5555u, 0u,
                (1u << 27u) | (4u << 28u) | enhance, c);
            if (d.texcoord[4].count != 1u || d.texcoord[4].format != 4u ||
                d.texcoord[4].shift != 17u ||
                d.texcoord[5].count != 1u || d.texcoord[5].format != 2u ||
                d.texcoord[5].shift != 21u ||
                d.texcoord[6].count != 1u || d.texcoord[6].format != 5u ||
                d.texcoord[6].shift != 10u ||
                d.texcoord[7].count != 1u || d.texcoord[7].format != 1u ||
                d.texcoord[7].shift != 29u) {
                ++g_failures;
                std::printf("FAILED: independent VAT B/C bit fixture\n");
            }
        }
        const auto transfer = decode_wii_tlut_transfer(0x00500000u,
            1023u | (2047u << 10u));
        if (transfer.source_address != 0x0A000000u ||
            transfer.destination_offset != 0x7FE00u ||
            transfer.byte_count != 65504u ||
            decode_wii_tlut_transfer(0x00500000u, 1023u).byte_count != 0u ||
            transfer.destination_offset + transfer.byte_count > kTmemBytes) {
            ++g_failures;
            std::printf("FAILED: Wii TLUT address/count fixture\n");
        }
    }
    {
        GxState state;
        state.load_bp((static_cast<std::uint32_t>(bp::kPeControl) << 24u) | 1u);
        // RGBA6 src-alpha blending requires dual source without constant alpha.
        state.load_bp((static_cast<std::uint32_t>(bp::kBlendMode) << 24u) |
                      1u | (1u << 4u) | (4u << 8u) | (5u << 5u));
        const auto dual = build_render_state_key(state);
        state.load_bp((static_cast<std::uint32_t>(bp::kConstAlpha) << 24u) | 0x180u);
        const auto both = build_render_state_key(state);
        // Subtract uses ONE/ONE and still needs constant EFB alpha overwrite.
        state.load_bp((static_cast<std::uint32_t>(bp::kBlendMode) << 24u) |
                      1u | (1u << 4u) | (1u << 11u));
        const auto constant = build_render_state_key(state);
        if ((dual.blend_bits & (1u << 27u)) == 0u ||
            (dual.blend_bits & (1u << 28u)) != 0u ||
            (both.blend_bits & (3u << 27u)) != (3u << 27u) ||
            (constant.blend_bits & (3u << 27u)) != (1u << 28u)) {
            ++g_failures;
            std::printf("FAILED: independent alpha routing/overwrite key facts\n");
        }
        state.load_bp((static_cast<std::uint32_t>(bp::kBlendMode) << 24u) |
                      1u | (1u << 4u) | (1u << 11u) | (7u << 8u) | (6u << 5u));
        if (!(build_render_state_key(state) == constant)) {
            ++g_failures;
            std::printf("FAILED: subtract keyed ignored blend factors\n");
        }
    }
    {
        GxState state;
        state.load_bp(1u); // one regular texgen
        const std::uint32_t regular = 5u << 7u;
        state.load_xf(xf::kTexGenBase, &regular, 1u);
        const auto disabled = build_vertex_shader_key(state);
        const std::uint32_t post = 29u | (1u << 8u);
        state.load_xf(xf::kPostTexGenBase, &post, 1u);
        if (!(disabled == build_vertex_shader_key(state))) {
            ++g_failures;
            std::printf("FAILED: disabled post matrix fragmented shader key\n");
        }
        const std::uint32_t dual = 1u;
        state.load_xf(xf::kDualTexTrans, &dual, 1u);
        const auto enabled = build_vertex_shader_key(state);
        const std::uint32_t other_post = 7u | (1u << 8u);
        state.load_xf(xf::kPostTexGenBase, &other_post, 1u);
        if (!(enabled == build_vertex_shader_key(state))) {
            ++g_failures;
            std::printf("FAILED: uniform post matrix index fragmented shader key\n");
        }
        const std::uint32_t unnormalized = 7u;
        state.load_xf(xf::kPostTexGenBase, &unnormalized, 1u);
        if (enabled == build_vertex_shader_key(state)) {
            ++g_failures;
            std::printf("FAILED: post normalize dropped from shader key\n");
        }
        const auto original_swap = build_pixel_shader_key(state, 1u);
        // All reset swap tables resolve to the same swizzle, despite selectors.
        state.load_bp((static_cast<std::uint32_t>(bp::kTevAlphaEnvBase) << 24u) | 0xFu);
        if (!(original_swap == build_pixel_shader_key(state, 1u))) {
            ++g_failures;
            std::printf("FAILED: equivalent swap selectors fragmented shader key\n");
        }
    }
    {
        PixelShaderKey k{};
        k.num_tev_stages = 2;
        k.num_texgens = 1;
        k.num_ind_stages = 1;
        k.alpha_comp0 = k.alpha_comp1 = 7;
        k.stages[0] = k.stages[1] = basic_stage();
        k.stages[0].order = 0u; // disabled regular predecessor still has coords
        k.ind_cmd[0][1] = 1u << 20u;
        const ShaderGenerator gen;
        const auto src = gen.generate_ps(k);
        const auto stage1 = src.find("// ---- TEV stage 1 ----");
        if (src.find("tevcoord_i = wrappedcoord + indtevtrans") >= stage1) {
            ++g_failures;
            std::printf("FAILED: ordinary stage failed to seed add-prev\n");
        }
        check_compiles(src, "ps_5_1", "ps_regular_predecessor_addprev");
        for (unsigned width = 0; width < 3; ++width) {
            k.num_tev_stages = 1;
            k.stages[0].alpha_env = (3u << 16u) | (width << 20u);
            const auto compare_src = gen.generate_ps(k);
            check_contains(compare_src, "int3 ca_i = alpha_compare_a_0;",
                           "ps_alpha_compare_uses_stage_entry_color");
            if (compare_src.find("alpha_compare_a_0 =") >=
                compare_src.find("tev_prev_i.rgb = outv")) {
                ++g_failures;
                std::printf("FAILED: alpha compare snapshot follows color write\n");
            }
            check_compiles(compare_src, "ps_5_1", "ps_aliasing_color_alpha_compare");
        }
        for (unsigned format = 0; format < 3; ++format) {
            k.flags = static_cast<std::uint8_t>(0x02u | (2u << 3u) | (format << 5u));
            const auto early = gen.generate_ps(k);
            check_contains(early, "[earlydepthstencil]", "ps_early_depth_is_forced");
            check_not_contains(early, "SV_Depth", "ps_early_ztexture_keeps_geometric_depth");
            check_contains(early, "last_raw_tex_i", "ps_ztexture_retains_raw_sample");
            check_contains(early, "& 0xFFFFFFu", "ps_ztexture_wraps_depth");
            check_compiles(early, "ps_5_1", "ps_early_ztexture");
            k.flags |= 1u;
            const auto late = gen.generate_ps(k);
            check_contains(late, "SV_Depth", "ps_late_ztexture_exports_depth");
            check_not_contains(late, "[earlydepthstencil]", "ps_late_depth_not_forced");
            check_compiles(late, "ps_5_1", "ps_late_ztexture");
        }
    }

    check_independent_pair_compilation();
    if (g_failures == 0) {
        std::printf("all shader-gen compile checks passed\n");
    } else {
        std::printf("%d shader-gen compile checks FAILED\n", g_failures);
    }
    return g_failures;
}
