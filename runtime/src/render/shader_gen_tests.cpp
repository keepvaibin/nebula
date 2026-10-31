// ShaderGenerator tests: every codegen path emits HLSL that FXC accepts, since a syntax error
// would otherwise show as missing geometry in play. The key matrix covers texgen types and
// sources, per-channel lighting, TEV compare modes, swap tables, every fog type, z-texturing
// and alpha-test permutations. Exit code: the number of failures.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#pragma warning(push, 0)
#include <Windows.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#pragma warning(pop)

#include "galaxy/render/shader_gen.h"
#include "galaxy/render/shader_keys.h"
#include "galaxy/render/enhanced_lighting.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render/texture_sampling.h"
#include "galaxy/render/shader_pair_compile.h"

#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>
#include <thread>

#include "galaxy/render/material_shaders.h"
#include "galaxy/render/uber_shaders.h"

#include <d3d12shader.h>

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
galaxy::render::KeyChannel chan_ctrl(
    bool matsource_vtx,
    bool lighting,
    std::uint8_t light_mask,
    bool ambsource_vtx,
    unsigned diffuse,
    unsigned attn) {
    galaxy::render::KeyChannel c{};
    c.material_vertex = matsource_vtx ? 1u : 0u;
    c.lighting = lighting ? 1u : 0u;
    c.light_mask = light_mask;
    c.ambient_vertex = ambsource_vtx ? 1u : 0u;
    c.diffuse = static_cast<std::uint8_t>(diffuse & 3u);
    c.attenuation = static_cast<std::uint8_t>(attn & 3u);
    return c;
}

galaxy::render::KeyTexGen key_texgen(
    bool stq,
    bool abc1,
    unsigned type,
    unsigned source_row,
    unsigned emboss_source = 0) {
    galaxy::render::KeyTexGen g{};
    g.projection = stq ? 1u : 0u;
    g.input_abc1 = abc1 ? 1u : 0u;
    g.type = static_cast<std::uint8_t>(type & 7u);
    g.source = static_cast<std::uint8_t>(source_row & 0x1Fu);
    g.emboss_source = static_cast<std::uint8_t>(emboss_source & 7u);
    return g;
}

void run_vs(const galaxy::render::VertexShaderKey& key, const char* label) {
    const galaxy::render::ShaderGenerator gen;
    check_compiles(gen.generate_vs(key), "vs_5_1", label);
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

// One clamped lerp TEV stage writing PREV, textured from map/coord 0, ras chan 0.
galaxy::render::KeyStage basic_stage() {
    galaxy::render::KeyStage st{};
    // Colour: a = TexColor (8), b = RasColor (10), c = Konst (14), d = CPrev (0); add, clamp, x1, to PREV.
    st.color_in[0] = 8u;
    st.color_in[1] = 10u;
    st.color_in[2] = 14u;
    st.color_clamp = 1u;
    // Alpha: a = TexAlpha (4), b = RasAlpha (5), c = Konst (6), d = APrev (0).
    st.alpha_in[0] = 4u;
    st.alpha_in[1] = 5u;
    st.alpha_in[2] = 6u;
    st.alpha_clamp = 1u;
    st.tex_enable = 1u;
    // Konst K0 rgb (12), K3 a (31); identity swizzles.
    st.konst_color = 12u;
    st.konst_alpha = 31u;
    for (std::uint8_t c = 0; c < 4u; ++c) {
        st.ras_swizzle[c] = st.tex_swizzle[c] = c;
    }
    return st;
}

// A stage from a game material's captured TEV words (colour and alpha operation, order: GX's field positions).
galaxy::render::KeyStage stage_from_words(std::uint32_t color, std::uint32_t alpha, std::uint32_t order) {
    galaxy::render::KeyStage st = basic_stage();
    for (unsigned i = 0; i < 4u; ++i) {
        st.color_in[3u - i] = static_cast<std::uint8_t>((color >> (4u * i)) & 0xFu);
        st.alpha_in[3u - i] = static_cast<std::uint8_t>((alpha >> (4u + 3u * i)) & 0x7u);
    }
    st.color_bias = static_cast<std::uint8_t>((color >> 16u) & 3u);
    st.color_sub = static_cast<std::uint8_t>((color >> 18u) & 1u);
    st.color_clamp = static_cast<std::uint8_t>((color >> 19u) & 1u);
    st.color_scale = static_cast<std::uint8_t>((color >> 20u) & 3u);
    st.color_dest = static_cast<std::uint8_t>((color >> 22u) & 3u);
    st.alpha_bias = static_cast<std::uint8_t>((alpha >> 16u) & 3u);
    st.alpha_sub = static_cast<std::uint8_t>((alpha >> 18u) & 1u);
    st.alpha_clamp = static_cast<std::uint8_t>((alpha >> 19u) & 1u);
    st.alpha_scale = static_cast<std::uint8_t>((alpha >> 20u) & 3u);
    st.alpha_dest = static_cast<std::uint8_t>((alpha >> 22u) & 3u);
    st.tex_map = static_cast<std::uint8_t>(order & 7u);
    st.tex_coord = static_cast<std::uint8_t>((order >> 3u) & 7u);
    st.tex_enable = static_cast<std::uint8_t>((order >> 6u) & 1u);
    st.channel = static_cast<std::uint8_t>((order >> 7u) & 7u);
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
    const galaxy::render::ShaderGenerator gen;
    try {
        for (unsigned profile = 0; profile < 4; ++profile) {
            galaxy::render::VertexShaderKey vk{};
            vk.num_chans = 1;
            vk.channel[0] = chan_ctrl(true, false, 0, false, 0, 0);
            galaxy::render::PixelShaderKey pk{};
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
                    vk.texgen[i] = key_texgen(true, false, 0, 5 + i);
                    vk.texgen[i].normalize = 1u;
                }
                vk.channel[0] = chan_ctrl(false, true, 0xFF, false, 2, 1);
                pk.num_tev_stages = static_cast<std::uint8_t>(1 + 5 * profile);
                for (unsigned i = 1; i < pk.num_tev_stages; ++i) {
                    pk.stages[i] = basic_stage();
                    pk.stages[i].tex_map = static_cast<std::uint8_t>(i % 8);
                }
                pk.fog_type = 4;
            }
            const auto vs = gen.generate_vs(vk);
            const auto ps = galaxy::render::material_ps_hlsl(pk);
            const auto sequential_vs = compile(vs, "vs_5_1", "gx_vs");
            const auto sequential_ps = compile(ps, "ps_5_1", "gx_ps");
            const auto pair = galaxy::render::compile_independent_shader_pair(
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

    // Exceptional cleanup must hold when a pixel-side failure happens while the compiler is running.
    std::promise<void> vertex_started;
    auto started = vertex_started.get_future();
    std::atomic<bool> vertex_completed{false};
    bool pixel_error_caught = false;
    try {
        (void)galaxy::render::compile_independent_shader_pair(
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
        (void)galaxy::render::compile_independent_shader_pair(
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
    using namespace galaxy::render;

    {   // RGB8/RGB565 have no EFB alpha, so destination-alpha factors read ONE/ZERO (as in Aurora's GX pipeline
        // normalization), which writes the source unchanged: no blend at all (effective_blend), the key of an
        // unblended draw; RGBA6 keeps its factors and its blend.
        NativeDrawState state;
        const auto set_blend = [&state](BlendFactor source, BlendFactor destination) {
            state.blend.blend_enable = true;
            state.blend.src_factor = source;
            state.blend.dst_factor = destination;
        };
        set_blend(BlendFactor::DstAlpha, BlendFactor::InvDstAlpha);
        const RenderStateKey rgb8 = build_render_state_key(state);
        set_blend(BlendFactor::One, BlendFactor::Zero);
        const RenderStateKey rgb8_normalized = build_render_state_key(state);
        state.pe.pixel_format = 2u;  // RGB565_Z16 also lacks EFB alpha.
        set_blend(BlendFactor::DstAlpha, BlendFactor::InvDstAlpha);
        const RenderStateKey rgb565 = build_render_state_key(state);
        state.pe.pixel_format = 1u;  // RGBA6_Z24 has EFB alpha.
        const RenderStateKey rgba6 = build_render_state_key(state);
        if (!(rgb8 == rgb8_normalized) || (rgb8.blend_bits & 1u) != 0u ||
            (rgb565.blend_bits & 0x1FFFu) != 0u ||
            (rgba6.blend_bits & 1u) == 0u ||
            ((rgba6.blend_bits >> 2u) & 7u) != 6u ||
            ((rgba6.blend_bits >> 5u) & 7u) != 7u) {
            ++g_failures;
            std::printf("FAILED: no-alpha EFB blend key normalization\n");
        }
    }

    {
        NativeDrawState state;
        state.tev[0].order = TevOrder{1u, 0u, true, 0u};
        state.gen.num_texgens = 1u;
        state.gen.num_ind_stages = 1u;
        state.ind_order[0].map = 5u;
        if (sampled_texture_map_mask(state) !=
            static_cast<std::uint8_t>((1u << 1u) | (1u << 5u))) {
            ++g_failures;
            std::printf("FAILED: indirect texmap included in sampled bindings\n");
        }
        state.gen.num_texgens = 0u;
        if (sampled_texture_map_mask(state) != static_cast<std::uint8_t>(1u << 5u)) {
            ++g_failures;
            std::printf("FAILED: no-texgen draw bound unused direct map\n");
        }
        state.gen.num_texgens = 1u;

        // Shader key generation extends the indirect-stage count when a TEV
        // command references a stage beyond the GenMode count. The binding
        // and snapshot mask must cover exactly those emitted samples too.
        state.ind_order[1].map = 6u;
        state.ind_order[2].map = 7u;
        state.ind[0].stage = 2u;
        if (sampled_texture_map_mask(state) !=
            static_cast<std::uint8_t>(
                (1u << 1u) | (1u << 5u) | (1u << 6u) | (1u << 7u))) {
            ++g_failures;
            std::printf("FAILED: command-extended indirect texmaps sampled\n");
        }
    }

    {
        NativeDrawState state;
        constexpr auto map_bit = static_cast<std::uint8_t>(1u << 6u);
        state.gen.num_texgens = 1u;
        state.tev[0].order = TevOrder{6u, 0u, true, 0u};
        TevColorArg* const colors[4] = {&state.tev[0].color_d, &state.tev[0].color_c, &state.tev[0].color_b,
                                        &state.tev[0].color_a};
        TevAlphaArg* const alphas[4] = {&state.tev[0].alpha_d, &state.tev[0].alpha_c, &state.tev[0].alpha_b,
                                        &state.tev[0].alpha_a};
        if (sampled_texture_map_mask(state) != 0u) {
            ++g_failures;
            std::printf("FAILED: dead texture stage bound leftover map\n");
        }
        // Every selector position, including alpha-only texture consumers.
        // Other operands remain nontexture register inputs.
        for (unsigned input = 0; input < 4u; ++input) {
            for (unsigned value = 0; value < 16u; ++value) {
                for (TevColorArg* c : colors) {
                    *c = TevColorArg{};
                }
                *colors[input] = static_cast<TevColorArg>(value);
                const auto expected = static_cast<std::uint8_t>(
                    value == 8u || value == 9u ? map_bit : 0u);
                if (sampled_texture_map_mask(state) != expected) {
                    ++g_failures;
                    std::printf("FAILED: TEXC/TEXA color selector binding\n");
                }
            }
        }
        for (TevColorArg* c : colors) {
            *c = TevColorArg{};
        }
        for (unsigned input = 0; input < 4u; ++input) {
            for (unsigned value = 0; value < 8u; ++value) {
                for (TevAlphaArg* a : alphas) {
                    *a = TevAlphaArg{};
                }
                *alphas[input] = static_cast<TevAlphaArg>(value);
                const auto expected = static_cast<std::uint8_t>(value == 4u ? map_bit : 0u);
                if (sampled_texture_map_mask(state) != expected) {
                    ++g_failures;
                    std::printf("FAILED: TEXA alpha selector binding\n");
                }
            }
        }
        for (TevAlphaArg* a : alphas) {
            *a = TevAlphaArg{};
        }
        state.z_texture.op = 1u;
        if (sampled_texture_map_mask(state) != map_bit) {
            ++g_failures;
            std::printf("FAILED: z-texture retains raw sample without TEX operands\n");
        }
        state.gen.num_texgens = 0u;
        if (sampled_texture_map_mask(state) != 0u) {
            ++g_failures;
            std::printf("FAILED: no texgens cannot supply z-texture raw sample\n");
        }
        for (unsigned stage = 0; stage < kMaxTevStages; ++stage) {
            NativeDrawState staged;
            staged.gen.num_texgens = 1u;
            staged.gen.num_tev_stages = static_cast<std::uint8_t>(stage + 1u);
            const unsigned map = stage & 7u;
            staged.tev[stage].order = TevOrder{static_cast<std::uint8_t>(map), 0u, true, 0u};
            staged.tev[stage].color_d = TevColorArg::TexColor;
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

    // Curved surfaces: every vertex output variant's hull and domain shaders, and curved vertex shaders (one matrix
    // or the per-vertex palette) for each variant.
    for (unsigned variant = 0; variant < 4u; ++variant) {
        char label[64];
        std::snprintf(label, sizeof(label), "hs_curved_variant_%u", variant);
        check_compiles(ShaderGenerator::curved_stage_source(variant, false), "hs_5_1", label);
        std::snprintf(label, sizeof(label), "ds_curved_variant_%u", variant);
        check_compiles(ShaderGenerator::curved_stage_source(variant, true), "ds_5_1", label);
        for (const bool palette : {false, true}) {
            VertexShaderKey k{};
            std::memset(&k, 0, sizeof(k));
            k.num_chans = 1;
            k.flags = static_cast<std::uint8_t>(kVsKeyCurved | 0x02u | (palette ? 0x01u : 0u) |
                                                ((variant & 1u) != 0u ? kVsKeyPerPixelLighting : 0u) |
                                                ((variant & 2u) != 0u ? kVsKeyMotion : 0u));
            k.channel[0] = chan_ctrl(false, true, 0x1, false, 2, 1);
            k.channel[2] = chan_ctrl(false, true, 0x1, false, 2, 1);
            std::snprintf(label, sizeof(label), "vs_curved_variant_%u_%s", variant, palette ? "palette" : "inline");
            run_vs(k, label);
        }
    }

    // Vertex shaders.
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
            k.flags |= kVsKeyFiniteLightAttn;
            std::snprintf(label, sizeof(label),
                          "vs_light_attn%u_diff%u_finite", attn, diff);
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
        k.texgen[0] = key_texgen(false, false, 0, 5);   // ST from tex0
        k.texgen[1] = key_texgen(true, true, 0, 0);     // STQ from geom
        k.texgen[2] = key_texgen(false, false, 2, 2);   // color0
        k.texgen[3] = key_texgen(true, false, 0, 1);    // STQ from normal
        k.texgen[4] = key_texgen(false, false, 1, 5, 0); // emboss
        k.texgen[5] = key_texgen(false, false, 0, 12);  // ST from tex7
        k.texgen[0].normalize = 0u;
        k.texgen[1].normalize = 1u;
        k.texgen[3].normalize = 1u;
        k.channel[0] = chan_ctrl(true, false, 0, false, 0, 0);
        const ShaderGenerator gen;
        const std::string src = gen.generate_vs(k);
        check_contains(src, "matrix_row(pn_row+0u)",
                       "vs_indexed_position_uses_palette");
        check_contains(src, "matrix_row(tm+0u)",
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
            k.texgen[i] = key_texgen((i & 1u) != 0u, false, 0, 5u + i);
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

    // Physical ray tracing's de-baking only in its frames' vertex shaders.
    {
        const ShaderGenerator gen;
        VertexShaderKey k{};
        std::memset(&k, 0, sizeof(k));
        k.num_chans = 1;
        k.channel[0] = chan_ctrl(true, false, 0, false, 0, 0);
        check_not_contains(gen.generate_vs(k), "nebula_debake", "vs_without_debake");
        k.features = kVsFeatureDebake;
        check_contains(gen.generate_vs(k), "nebula_debake_gain(v.nrm)", "vs_debake");
        run_vs(k, "vs_debake");
    }

    // Motion-vector variants.

    // Enhanced lighting.

    // Pixel shaders.














    // Compare modes: color widths R8/GR16/BGR24/RGB8 x GT/EQ, with the
    // matching alpha compare (incl. the A8 kind = value 3 for alpha).


    // Every fog type (with a real alpha test so both paths coexist).




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
        NativeDrawState state;
        state.pe.pixel_format = 1u;
        // RGBA6 src-alpha blending requires dual source without constant alpha.
        state.blend.blend_enable = true;
        state.blend.alpha_update = true;
        state.blend.src_factor = BlendFactor::SrcAlpha;
        state.blend.dst_factor = BlendFactor::InvSrcAlpha;
        const auto dual = build_render_state_key(state);
        state.blend.const_alpha_enable = true;
        state.blend.const_alpha = 0x80u;
        const auto both = build_render_state_key(state);
        // Subtract uses ONE/ONE and still needs constant EFB alpha overwrite.
        state.blend.subtract = true;
        state.blend.src_factor = BlendFactor::Zero;
        state.blend.dst_factor = BlendFactor::Zero;
        const auto constant = build_render_state_key(state);
        if ((dual.blend_bits & (1u << 27u)) == 0u ||
            (dual.blend_bits & (1u << 28u)) != 0u ||
            (both.blend_bits & (3u << 27u)) != (3u << 27u) ||
            (constant.blend_bits & (3u << 27u)) != (1u << 28u)) {
            ++g_failures;
            std::printf("FAILED: independent alpha routing/overwrite key facts\n");
        }
        state.blend.src_factor = BlendFactor::InvDstAlpha;
        state.blend.dst_factor = BlendFactor::DstAlpha;
        if (!(build_render_state_key(state) == constant)) {
            ++g_failures;
            std::printf("FAILED: subtract keyed ignored blend factors\n");
        }
    }
    {
        NativeDrawState state;
        state.gen.num_texgens = 1u;  // one regular texgen
        state.texgen[0] = decode_xf_texgen(5u << 7u);
        // Post-transform texture matrices are always on (natively there is no revision bit).
        state.post_texgen[0] = decode_xf_post_texgen(29u | (1u << 8u));
        const auto enabled = build_vertex_shader_key(state, 0u);
        state.post_texgen[0] = decode_xf_post_texgen(7u | (1u << 8u));
        if (!(enabled == build_vertex_shader_key(state, 0u))) {
            ++g_failures;
            std::printf("FAILED: uniform post matrix index fragmented shader key\n");
        }
        state.post_texgen[0] = decode_xf_post_texgen(7u);
        if (enabled == build_vertex_shader_key(state, 0u)) {
            ++g_failures;
            std::printf("FAILED: post normalize dropped from shader key\n");
        }
        NativeDrawState draw;
        draw.gen.num_texgens = 1u;
        const auto original_swap = build_pixel_shader_key(draw, 1u);
        // All reset swap tables resolve to the same swizzle, despite selectors.
        draw.tev[0].ras_swap = 3u;
        draw.tev[0].tex_swap = 3u;
        if (!(original_swap == build_pixel_shader_key(draw, 1u))) {
            ++g_failures;
            std::printf("FAILED: equivalent swap selectors fragmented shader key\n");
        }
    }

    check_independent_pair_compilation();

    {   // The material pixel shader: each variant's build-time DXBC is its source compiled as the runtime would
        // (the CMake define table and material_ps_defines agree), and keys pick the variant of their interface.
        for (unsigned variant = 0; variant < kMaterialPsVariants; ++variant) {
            const std::string source = material_ps_defines(variant) + std::string(material_ps_source());
            Microsoft::WRL::ComPtr<ID3DBlob> code, errors;
            const HRESULT hr = D3DCompile(source.data(), source.size(), "material_ps", nullptr, nullptr, "main",
                                          "ps_5_1",
                                          D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_PACK_MATRIX_ROW_MAJOR |
                                              D3DCOMPILE_OPTIMIZATION_LEVEL1,
                                          0, &code, &errors);
            const std::span<const std::uint8_t> built = material_ps_dxbc(variant);
            if (FAILED(hr) || built.size() != code->GetBufferSize() ||
                std::memcmp(built.data(), code->GetBufferPointer(), built.size()) != 0) {
                ++g_failures;
                std::printf("FAILED: material_ps_variant_%u (build-time DXBC %zu bytes, compiled %zu)\n", variant,
                            built.size(), SUCCEEDED(hr) ? static_cast<std::size_t>(code->GetBufferSize()) : 0u);
            }
        }
        PixelShaderKey key{};
        key.num_tev_stages = 1u;
        const auto expect_variant = [&](const char* label, unsigned expected) {
            if (material_ps_variant(key) != expected) {
                ++g_failures;
                std::printf("FAILED: %s variant %u, expected %u\n", label, material_ps_variant(key), expected);
            }
        };
        expect_variant("material_plain_early", 1u);
        key.flags = 0x01u;
        expect_variant("material_plain_late", 0u);
        key.flags = 0x03u;
        expect_variant("material_depth_export", 4u);
        key.flags = 0x83u;
        expect_variant("material_depth_dual", 5u);
        key.flags = 0x82u;
        expect_variant("material_ztex_early_dual", 3u);
        key.flags = 0x00u;
        key.reserved = kPsKeyMotion | kPsKeyReactive;
        expect_variant("material_motion_wins", 7u);
        key.reserved = kPsKeyReactive | kPsKeyLayer | kPsKeyEnhancedLighting;
        expect_variant("material_layer_enhanced", 2u * kMaterialPsOutputs + 13u);
        key.reserved = kPsKeyPerPixelLighting | kPsKeyEnhancedLighting;
        key.flags = 0x01u;
        expect_variant("material_ppl_enhanced_late", 3u * kMaterialPsOutputs);
        for (unsigned variant = 0; variant < kUberPsVariants; ++variant) {
            const MaterialPsVariant form = material_ps_variant_form(uber_ps_material_variant(variant));
            if (form.dual != uber_ps_variant_dual_source(variant) || form.depth != uber_ps_variant_export_depth(variant) ||
                form.early != uber_ps_variant_early_depth(variant) || form.layer != uber_ps_variant_layer(variant) ||
                form.ppl || form.enhanced || form.motion) {
                ++g_failures;
                std::printf("FAILED: uber_ps_material_variant_%u\n", variant);
            }
        }
        std::printf("material pixel shader variants checked\n");
    }


    if (g_failures == 0) {
        std::printf("all shader-gen compile checks passed\n");
    } else {
        std::printf("%d shader-gen compile checks FAILED\n", g_failures);
    }
    return g_failures;
}
