// Builds canonical shader and pipeline keys from the typed drawing state (see shader_keys.h).

#include "galaxy/render/shader_keys.h"

#include "galaxy/render/draw_state.h"
#include "galaxy/render/uber_constants.h"

#include <algorithm>
#include <cstring>

namespace galaxy::render {

namespace {

// GX blending uses the TEV-computed source alpha. The EFB alpha that is eventually stored may
// be quantized to the target format or replaced by GXSetDstAlpha. Source-alpha blend factors
// therefore need a separate export whenever they cannot safely read color0.a.
[[nodiscard]] bool blend_reads_src_alpha(const BlendMode& bm) {
    return bm.blend_enable && !bm.subtract &&
        (static_cast<std::uint32_t>(bm.src_factor) == 4u ||
         static_cast<std::uint32_t>(bm.src_factor) == 5u ||
         static_cast<std::uint32_t>(bm.dst_factor) == 4u ||
         static_cast<std::uint32_t>(bm.dst_factor) == 5u);
}

[[nodiscard]] bool needs_dual_src_alpha(
    const PeControl& pe,
    const BlendMode& bm) {
    if (!blend_reads_src_alpha(bm)) {
        return false;
    }
    const bool dst_alpha =
        (pe.pixel_format == 1u) && bm.const_alpha_enable && bm.alpha_update;
    return dst_alpha || pe.pixel_format == 1u;
}

}  // namespace

PixelShaderKey build_pixel_shader_key(
    const NativeDrawState& state, unsigned efb_scale, bool true_color) {
    // Value-initialized: hash() and operator== read the used stages and the header, and unused fields must be zero.
    PixelShaderKey key{};

    const GenMode gm = state.gen;
    key.num_tev_stages = gm.num_tev_stages;     // 1-16
    key.num_texgens    = gm.num_texgens;
    // Colour-channel production belongs to the VS. The PS consumes its interpolants, and no pixel
    // code depends on this count, so all counts share one canonical PS variant.
    key.num_chans      = 0u;

    // The fog type is part of the key. The A/B/C/colour params are uniforms and stay out of it.
    key.fog_type = static_cast<std::uint8_t>(state.fog_type);

    // Alpha compare function and logic operator are key (refs are uniforms).
    const AlphaCompare ac = state.alpha;
    key.alpha_comp0 = static_cast<std::uint8_t>(ac.comp0);
    key.alpha_comp1 = static_cast<std::uint8_t>(ac.comp1);
    key.alpha_logic = static_cast<std::uint8_t>(ac.logic);

    // flags: bit0 late_ztest (zcomploc = 0: Z test after texturing)
    //        bit1 z_texture   (TEV Z env op != disable)
    //        bit2 dst_alpha   (GXSetDstAlpha enabled on an RGBA6 EFB)
    //        bits3-4 z_texture op (BP 0xF5 bits 2-3: 1=add, 2=replace)
    //        bits5-6 z_texture format (BP 0xF5 bits 0-1: 0=Z8,1=Z16,2=Z24X8)
    //        bit7 dual-source TEV alpha (needs_dual_src_alpha)
    // SMG clears colour and depth with a fullscreen quad whose depth comes from a Z24X8 texture via
    // GX_ZT_REPLACE (MainLoopFramework::clearEfb).
    //
    // dst_alpha also requires alpha_update, as in Dolphin's PixelShaderGen (useDstAlpha): with EFB
    // alpha writes masked, the constant never reaches the EFB and must not replace the TEV alpha
    // the blend factors read.
    const PeControl pe = state.pe;
    const BlendMode bm = effective_blend(state.blend, pe.pixel_format == 1u);
    const bool late_ztest = !pe.early_z;
    const std::uint32_t ztex_op = state.z_texture.op;
    const std::uint32_t ztex_fmt = state.z_texture.type;
    const bool ztex_on = ztex_op != 0u;
    const bool dst_alpha =
        (pe.pixel_format == 1u) && bm.const_alpha_enable && bm.alpha_update;
    key.flags = static_cast<std::uint8_t>(
        (late_ztest ? 0x01u : 0u) |
        (ztex_on ? 0x02u : 0u) |
        (dst_alpha ? 0x04u : 0u) |
        (ztex_on ? (ztex_op << 3) : 0u) |
        (ztex_on ? (ztex_fmt << 5) : 0u) |
        (needs_dual_src_alpha(pe, bm) ? 0x80u : 0u));
    key.output_flags = static_cast<std::uint8_t>(
        (pe.pixel_format & 0x7u) |
        (pe.pixel_format == 1u && true_color ? 0x10u
         : (pe.pixel_format == 1u && bm.dither) ? 0x08u : 0u));
    // The EFB scale is a per-draw constant, so one shader serves every internal resolution and
    // `efb_scale` is unused. Dither keys use 0xff, apart from the fixed-scale values 0-15.
    static_cast<void>(efb_scale);
    key.efb_scale_minus_one = static_cast<std::uint8_t>(
        (key.output_flags & 0x08u) != 0u ? 0xffu : 0u);

    const auto indirect = [](const IndirectStage& ind) {
        return (ind.stage | ind.format | ind.bias | ind.alpha | ind.matrix | ind.wrap_s | ind.wrap_t |
                static_cast<unsigned>(ind.lod) | static_cast<unsigned>(ind.prev)) != 0u;
    };
    unsigned num_ind_stages = std::min<unsigned>(gm.num_ind_stages, 4u);
    for (unsigned s = 0; s < key.num_tev_stages; ++s) {
        if (indirect(state.ind[s])) {
            num_ind_stages = std::max<unsigned>(num_ind_stages, (state.ind[s].stage & 0x3u) + 1u);
        }
    }
    key.num_ind_stages = static_cast<std::uint8_t>(std::min<unsigned>(num_ind_stages, 4u));
    for (unsigned i = 0; i < key.num_ind_stages; ++i) {
        const auto& order = state.ind_order[i];
        key.ind_order[i] = KeyIndirectOrder{static_cast<std::uint8_t>(order.map & 0x7u),
            static_cast<std::uint8_t>(order.coord & 0x7u), static_cast<std::uint8_t>(order.scale_s & 0xFu),
            static_cast<std::uint8_t>(order.scale_t & 0xFu)};
    }

    for (unsigned s = 0; s < key.num_tev_stages; ++s) {
        const TevStageConfig& cfg = state.tev[s];
        KeyStage& stage = key.stages[s];
        const auto u8 = [](auto value, unsigned mask) {
            return static_cast<std::uint8_t>(static_cast<unsigned>(value) & mask);
        };
        stage.color_in[0] = u8(cfg.color_a, 0xFu);
        stage.color_in[1] = u8(cfg.color_b, 0xFu);
        stage.color_in[2] = u8(cfg.color_c, 0xFu);
        stage.color_in[3] = u8(cfg.color_d, 0xFu);
        stage.color_bias = u8(cfg.color_bias, 0x3u);
        stage.color_sub = u8(cfg.color_sub, 0x1u);
        stage.color_clamp = u8(cfg.color_clamp, 0x1u);
        stage.color_scale = u8(cfg.color_scale, 0x3u);
        stage.color_dest = u8(cfg.color_dest, 0x3u);
        stage.alpha_in[0] = u8(cfg.alpha_a, 0x7u);
        stage.alpha_in[1] = u8(cfg.alpha_b, 0x7u);
        stage.alpha_in[2] = u8(cfg.alpha_c, 0x7u);
        stage.alpha_in[3] = u8(cfg.alpha_d, 0x7u);
        stage.alpha_bias = u8(cfg.alpha_bias, 0x3u);
        stage.alpha_sub = u8(cfg.alpha_sub, 0x1u);
        stage.alpha_clamp = u8(cfg.alpha_clamp, 0x1u);
        stage.alpha_scale = u8(cfg.alpha_scale, 0x3u);
        stage.alpha_dest = u8(cfg.alpha_dest, 0x3u);
        stage.tex_map = u8(cfg.order.texmap, 0x7u);
        stage.tex_coord = u8(cfg.order.texcoord, 0x7u);
        stage.tex_enable = u8(cfg.order.tex_enable, 0x1u);
        stage.channel = u8(cfg.order.ras_channel, 0x7u);
        stage.konst_color = u8(cfg.kcsel, 0x1Fu);
        stage.konst_alpha = u8(cfg.kasel, 0x1Fu);
        // ras_swap and tex_swap select swap tables; as in Dolphin's PixelShaderGen, the key holds the resolved
        // swizzles, not the tables.
        for (unsigned c = 0; c < 4u; ++c) {
            stage.ras_swizzle[c] = u8(state.swap[cfg.ras_swap & 3u][c], 0x3u);
            stage.tex_swizzle[c] = u8(state.swap[cfg.tex_swap & 3u][c], 0x3u);
        }
        if (key.num_ind_stages != 0u) {
            const IndirectStage& ind = state.ind[s];
            stage.ind_stage = u8(ind.stage, 0x3u);
            stage.ind_format = u8(ind.format, 0x3u);
            stage.ind_bias = u8(ind.bias, 0x7u);
            stage.ind_alpha = u8(ind.alpha, 0x3u);
            stage.ind_matrix = u8(ind.matrix, 0xFu);
            stage.ind_wrap_s = u8(ind.wrap_s, 0x7u);
            stage.ind_wrap_t = u8(ind.wrap_t, 0x7u);
            stage.ind_lod = u8(ind.lod, 0x1u);
            stage.ind_prev = u8(ind.prev, 0x1u);
        }

        // A stage's texture, rasterized colour and konst values matter only through its eight
        // combiner inputs. When no input selects a source, its order, swizzles and konst
        // selector cannot change the output and are cleared. The texture order is kept when a Z
        // texture (which reads the last sampled texel) or an indirect stage is active.
        bool uses_tex = false;
        bool uses_ras = false;
        bool uses_konst_color = false;
        bool uses_konst_alpha = false;
        for (const TevColorArg input : {cfg.color_a, cfg.color_b, cfg.color_c, cfg.color_d}) {
            uses_tex |= input == TevColorArg::TexColor || input == TevColorArg::TexAlpha;
            uses_ras |= input == TevColorArg::RasColor || input == TevColorArg::RasAlpha;
            uses_konst_color |= input == TevColorArg::Konst;
        }
        for (const TevAlphaArg input : {cfg.alpha_a, cfg.alpha_b, cfg.alpha_c, cfg.alpha_d}) {
            uses_tex |= input == TevAlphaArg::TexAlpha;
            uses_ras |= input == TevAlphaArg::RasAlpha;
            uses_konst_alpha |= input == TevAlphaArg::Konst;
        }
        if (!uses_tex && !ztex_on && key.num_ind_stages == 0u) {
            stage.tex_map = stage.tex_coord = stage.tex_enable = 0u;
            std::memset(stage.tex_swizzle, 0, sizeof(stage.tex_swizzle));
        }
        if (!uses_ras) {
            stage.channel = 0u;
            std::memset(stage.ras_swizzle, 0, sizeof(stage.ras_swizzle));
        }
        if (!uses_konst_color) {
            stage.konst_color = 0u;
        }
        if (!uses_konst_alpha) {
            stage.konst_alpha = 0u;
        }
    }

    return key;
}

VertexShaderKey build_vertex_shader_key(const NativeDrawState& state, std::uint32_t matrix_bits) {
    VertexShaderKey key{};
    std::memset(&key, 0, sizeof(key));

    const GenMode gm = state.gen;
    key.num_texgens = gm.num_texgens;
    key.num_chans   = gm.num_color_chans;

    // flags: bit0 per-vertex position matrix index (CP VCD_LO bit 0),
    //        bit1 lighting enabled on any channel, bit2 dual-tex enable,
    //        bit5 kVsKeyFiniteLightAttn (set below).
    const std::uint32_t vcd_lo = matrix_bits;
    const bool per_vertex_posmtx = (vcd_lo & 1u) != 0u;

    bool lighting_active = false;
    for (unsigned ch = 0; ch < key.num_chans && ch < 2u; ++ch) {
        if (state.channel[ch].lighting_enable ||
            state.channel[2u + ch].lighting_enable) {
            lighting_active = true;
            break;
        }
    }

    // The post-transform matrices always apply: the console switched them on once at its start (GXInit's revision
    // bits) and the game never switches them off.
    const bool dual_tex = true;

    key.flags = static_cast<std::uint8_t>(
        (per_vertex_posmtx ? 0x01u : 0u) |
        (lighting_active   ? 0x02u : 0u) |
        (dual_tex          ? 0x04u : 0u));

    // Per-vertex texture matrix index per texgen (VCD_LO bits 1-8), instead of CP MatrixIndexA/B;
    // masked to the active texgens.
    const std::uint32_t active_mask =
        (key.num_texgens >= 8u) ? 0xFFu : ((1u << key.num_texgens) - 1u);
    key.texmtx_idx_mask =
        static_cast<std::uint8_t>(((vcd_lo >> 1) & 0xFFu) & active_mask);

    // Dual transforms and matrix-index inputs are unused by color/emboss
    // texgens. Unknown type encodings use the generator's regular fallback.
    bool matrix_texgen_active = false;
    // Only the active generators; fields a generator's type does not read are leftover state and stay zero, so they
    // cannot duplicate a shader. Colour generators read only their type, emboss maps their source and light, regular
    // generators their projection, input form and source row (and the post-transform's normalize).
    for (unsigned i = 0; i < key.num_texgens && i < kMaxTexGens; ++i) {
        const XfTexGen& g = state.texgen[i];
        KeyTexGen& out = key.texgen[i];
        out.type = static_cast<std::uint8_t>(static_cast<unsigned>(g.type) & 0x7u);
        switch (g.type) {
        case TexGenType::Color0:
        case TexGenType::Color1:
            key.texmtx_idx_mask &= static_cast<std::uint8_t>(~(1u << i));
            break;
        case TexGenType::EmbossMap:
            out.emboss_source = static_cast<std::uint8_t>(g.emboss_source & 0x7u);
            out.emboss_light = static_cast<std::uint8_t>(g.emboss_light & 0x7u);
            key.texmtx_idx_mask &= static_cast<std::uint8_t>(~(1u << i));
            break;
        case TexGenType::Regular:
        default:
            out.projection = g.generate_stq ? 1u : 0u;
            out.input_abc1 = g.input_form_abc1 ? 1u : 0u;
            out.source = static_cast<std::uint8_t>(g.source_row & 0x1Fu);
            matrix_texgen_active = true;
            break;
        }
        if (dual_tex && g.type == TexGenType::Regular) {
            out.normalize = state.post_texgen[i].normalize ? 1u : 0u;
        }
    }

    if (!matrix_texgen_active) key.flags &= static_cast<std::uint8_t>(~0x04u);

    // Channels are grouped by component (colour 0, colour 1, alpha 0, alpha 1), so shader generation pairs colour N
    // with alpha N. An unlit channel reads only its material source: its ambient source, lights and functions are
    // leftover state and stay out of the key.
    const auto canonical_channel = [](const XfChannelCtrl& c) {
        KeyChannel out{};
        out.material_vertex = c.material_from_vertex ? 1u : 0u;
        if (c.lighting_enable) {
            out.lighting = 1u;
            out.light_mask = c.light_mask;
            out.ambient_vertex = c.ambient_from_vertex ? 1u : 0u;
            out.diffuse = static_cast<std::uint8_t>(c.diffuse_func & 0x3u);
            out.attenuation = static_cast<std::uint8_t>(c.atten_func & 0x3u);
        }
        return out;
    };
    for (unsigned ch = 0; ch < key.num_chans && ch < 2u; ++ch) {
        key.channel[ch] = canonical_channel(state.channel[ch]);
        key.channel[2u + ch] = canonical_channel(state.channel[2u + ch]);
    }
    // A lit channel with attenuation (specular or spotlight: the function's low bit) and lights.
    for (const KeyChannel& channel : key.channel) {
        if (channel.lighting != 0u && (channel.attenuation & 1u) != 0u && channel.light_mask != 0u) {
            key.flags |= kVsKeyFiniteLightAttn;
        }
    }

    return key;
}

RenderStateKey build_render_state_key(const NativeDrawState& state) {
    RenderStateKey key{};
    std::memset(&key, 0, sizeof(key));

    const PeControl pe = state.pe;
    const BlendMode bm = effective_blend(state.blend, pe.pixel_format == 1u);

    // An EFB without alpha reads destination alpha as one. These factors are normalized before
    // hashing, as Aurora does in populate_pipeline_config. pipeline_cache.cpp applies the same
    // mapping to the D3D12 PSO.
    const auto blend_factor = [has_alpha = pe.pixel_format == 1u](
                                  BlendFactor factor) {
        if (!has_alpha) {
            if (factor == BlendFactor::DstAlpha) {
                return BlendFactor::One;
            }
            if (factor == BlendFactor::InvDstAlpha) {
                return BlendFactor::Zero;
            }
        }
        return factor;
    };

    // Canonicalize blend_bits: with blending and logic ops disabled, zero all blend fields so they
    // do not fragment the cache.
    if (bm.blend_enable) {
        // Encode: dst_factor(7:5) | src_factor(4:2) | subtract(1) | blend_enable(0)
        key.blend_bits =
            (1u)                                                                |
            ((static_cast<std::uint32_t>(bm.subtract ? BlendFactor::One : blend_factor(bm.src_factor)) & 0x7u) << 2u) |
            ((static_cast<std::uint32_t>(bm.subtract ? BlendFactor::One : blend_factor(bm.dst_factor)) & 0x7u) << 5u) |
            (bm.subtract ? (1u << 1u) : 0u);
    } else if (bm.logic_op_enable) {
        // Encode logic op: bit8 = logic_op_enable, bits 12-9 = logic_mode
        key.blend_bits =
            (1u << 8u) |
            ((static_cast<std::uint32_t>(bm.logic_mode) & 0xFu) << 9u);
    }
    // EFB write masks (cmode0 bits 3/4): bit25 = color_update, bit26 = alpha_update. Z-clear and
    // z-prepass draws write depth with the colour update off.
    if (bm.color_update) {
        key.blend_bits |= (1u << 25u);
    }
    if (bm.alpha_update && pe.pixel_format == 1u) {
        key.blend_bits |= (1u << 26u);
    }
    // bit27 = dual-source TEV alpha (must agree with PixelShaderKey flags bit7). The PSO uses
    // SRC1_ALPHA/INV_SRC1_ALPHA in place of SrcAlpha/InvSrcAlpha, so the blender reads TEV alpha
    // while color0.a stays the physical EFB alpha.
    if (needs_dual_src_alpha(pe, bm)) {
        key.blend_bits |= (1u << 27u);
    }
    // Constant EFB alpha replacement is independent of source-alpha routing.
    // The pipeline reads this bit only inside its blend-enabled branch; the
    // pixel shader applies constant destination alpha itself.
    if (bm.blend_enable && pe.pixel_format == 1u && bm.const_alpha_enable &&
        bm.alpha_update) {
        key.blend_bits |= (1u << 28u);
    }

    // zmode_bits: test_enable(0) | func(3:1) | update_enable(4)
    const ZMode zm = state.z;
    // A disabled compare passes unconditionally, so the function field is leftover state then.
    key.zmode_bits = static_cast<std::uint16_t>(
        (zm.test_enable   ? 1u : 0u)                                            |
        (zm.test_enable
             ? ((static_cast<std::uint32_t>(zm.func) & 0x7u) << 1u) : 0u)       |
        (zm.update_enable ? (1u << 4u) : 0u));

    key.cull = static_cast<std::uint8_t>(state.gen.cull);

    // The pipeline only distinguishes RGBA6 (has destination alpha) from the
    // other formats.
    key.pixfmt = pe.pixel_format == 1u ? 1u : 0u;
    // The backend overwrites this per draw before the PSO lookup. Triangle is the safe default for
    // tests and callers that build only render-state keys.
    key.primitive_topology = 3u; // D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE

    return key;
}

}  // namespace galaxy::render
