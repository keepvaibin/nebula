// shader_keys.cpp: builds canonicalized shader and pipeline keys from GxState.
//
// Canonicalization (see shader_keys.h): every field that does not affect code generation for
// the enabled stage count is zeroed, so FNV-1a hashing gives consistent cache hits. Disabled
// TEV stages, unused texgens and don't-care blend fields must not fragment the cache.

#include "galaxy/gx/shader_keys.h"

#include "galaxy/gx/gx_state.h"
#include "galaxy/gx/render_config.h"
#include "galaxy/gx/uber_constants.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace galaxy::gx {

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

// ---------------------------------------------------------------------------
// PixelShaderKey
// ---------------------------------------------------------------------------

// RGBA6 keeps 8-bit colour by default: the Flipper's 6-bit colour and its 2x2 dither only band
// and stipple the picture, and at raised internal resolution the dither shows as a dot grid.
bool efb_true_color() noexcept {
    static const bool true_color = [] {
        char value[8]{};
        std::size_t length = 0u;
        return !(getenv_s(&length, value, sizeof(value), "GALAXY_EFB_TRUE_COLOR") == 0 && length > 1u &&
                 value[0] == '0');
    }();
    return true_color;
}

PixelShaderKey build_pixel_shader_key(
    const GxState& state, unsigned efb_scale, bool true_color) {
    PixelShaderKey key{};
    // Zero-initialise the whole struct first. This covers the padding between num_tev_stages..flags
    // and the trailing stages[] entries, so the hash is deterministic for any stage count.
    std::memset(&key, 0, sizeof(key));

    const GenMode gm = state.gen_mode();
    key.num_tev_stages = gm.num_tev_stages;     // 1-16
    key.num_texgens    = gm.num_texgens;
    // Colour-channel production belongs to the VS. The PS consumes its interpolants, and no pixel
    // code depends on this count, so all counts share one canonical PS variant.
    key.num_chans      = 0u;

    // The fog type is part of the key. The A/B/C/colour params are uniforms and stay out of it.
    const FogParams fog = state.fog();
    key.fog_type = static_cast<std::uint8_t>(fog.type);

    // Alpha compare function and logic operator are key (refs are uniforms).
    const AlphaCompare ac = state.alpha_compare();
    key.alpha_comp0 = static_cast<std::uint8_t>(ac.comp0);
    key.alpha_comp1 = static_cast<std::uint8_t>(ac.comp1);
    key.alpha_logic = static_cast<std::uint8_t>(ac.logic);

    // flags: bit0 late_ztest (zcomploc = 0 means Z test AFTER texturing)
    //        bit1 z_texture   (TEV Z env op != disable)
    //        bit2 dst_alpha   (GXSetDstAlpha ENABLED on an RGBA6 EFB)
    //        bits3-4 z_texture op (BP 0xF5 bits 2-3: 1=add, 2=replace)
    //        bits5-6 z_texture format (BP 0xF5 bits 0-1: 0=Z8,1=Z16,2=Z24X8)
    // SMG clears colour and depth with a fullscreen quad whose depth comes from a Z24X8 texture via
    // GX_ZT_REPLACE (MainLoopFramework::clearEfb). Without this the quad writes geometric near-depth
    // and corrupts the depth buffer.
    //
    // The dst_alpha override applies only when GXSetDstAlpha is enabled. Otherwise the fragment keeps
    // its TEV alpha so blending works. alpha_update also gates it (Dolphin PixelShaderGen
    // useDstAlpha): with EFB alpha writes masked off, the constant never reaches the EFB, and the
    // override would still corrupt the blend factors.
    const PeControl pe = state.pe_control();
    const BlendMode bm = state.blend_mode();
    const bool late_ztest = !pe.early_z;
    const std::uint32_t ztex2 = state.bp(bp::kTevZEnv1);
    const std::uint32_t ztex_op = (ztex2 >> 2) & 0x3u;
    const std::uint32_t ztex_fmt = ztex2 & 0x3u;
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
    // Scale is a per-draw constant, so one prepared material covers every internal resolution. The
    // distinct mode keeps the identity and source of static-scale DXBC in the persistent shader cache.
    static_cast<void>(efb_scale);  // Kept for the public call signature.
    key.efb_scale_minus_one = static_cast<std::uint8_t>(
        (key.output_flags & 0x08u) != 0u ? 0xffu : 0u);

    std::uint8_t num_ind_stages =
        static_cast<std::uint8_t>(std::min<std::uint8_t>(
            gm.num_ind_stages,
            4u));
    for (unsigned s = 0; s < key.num_tev_stages; ++s) {
        const std::uint32_t cmd = state.bp(
            static_cast<std::uint8_t>(bp::kIndCmdBase + s));
        if (cmd != 0u) {
            num_ind_stages = static_cast<std::uint8_t>(
                std::max<unsigned>(num_ind_stages, (cmd & 0x3u) + 1u));
        }
    }
    key.num_ind_stages =
        static_cast<std::uint8_t>(std::min<unsigned>(num_ind_stages, 4u));
    if (key.num_ind_stages != 0u) {
        for (unsigned s = 0; s < key.num_tev_stages; ++s) {
            key.ind_cmd[s / 4u][s & 3u] = state.bp(
                static_cast<std::uint8_t>(bp::kIndCmdBase + s));
        }
        key.ind_ref = state.bp(bp::kIndRef) &
            ((std::uint32_t{1} << (6u * key.num_ind_stages)) - 1u);
        for (unsigned pair = 0; pair < 2u; ++pair) {
            const unsigned first_stage = pair * 2u;
            const unsigned active = key.num_ind_stages > first_stage
                ? std::min<unsigned>(key.num_ind_stages - first_stage, 2u) : 0u;
            const auto mask = active == 0u ? 0u
                : (std::uint32_t{1} << (8u * active)) - 1u;
            key.ind_scale[pair] = state.bp(static_cast<std::uint8_t>(
                bp::kRas1Ss0 + pair)) & mask;
        }
    }

    // Encodes each active TEV stage. Stages beyond num_tev_stages stay zero because `key` is
    // value-initialised. The explicit memset is redundant but kept: hash() is a byte-wise FNV over
    // sizeof(PixelShaderKey), and equality is byte-wise too (has_unique_object_representations_v is
    // asserted for this type). Every byte must be determinate, so keep at least one of the two zeroings.
    for (unsigned s = 0; s < key.num_tev_stages; ++s) {
        const TevStageConfig cfg = state.tev_stage(s);

        // color_env word: the raw BP bit packing is rebuilt from the decoded fields, so the key carries
        // exactly what the shader key encoding needs. Bits match TevStagePacked.color_env (BP 0xC0+2*s).
        const std::uint32_t color_env =
            (static_cast<std::uint32_t>(cfg.color_d)             & 0xFu)        |
            ((static_cast<std::uint32_t>(cfg.color_c)            & 0xFu) << 4u) |
            ((static_cast<std::uint32_t>(cfg.color_b)            & 0xFu) << 8u) |
            ((static_cast<std::uint32_t>(cfg.color_a)            & 0xFu) << 12u)|
            ((static_cast<std::uint32_t>(cfg.color_bias)         & 0x3u) << 16u)|
            (cfg.color_sub ? (1u << 18u) : 0u)                                  |
            (cfg.color_clamp ? (1u << 19u) : 0u)                                |
            ((static_cast<std::uint32_t>(cfg.color_scale)        & 0x3u) << 20u)|
            ((static_cast<std::uint32_t>(cfg.color_dest)         & 0x3u) << 22u);

        // Swap selectors are stored as resolved swizzles in ksel, so their raw low four bits do not
        // distinguish shader code.
        const std::uint32_t alpha_env =
            ((static_cast<std::uint32_t>(cfg.alpha_d)            & 0x7u) << 4u) |
            ((static_cast<std::uint32_t>(cfg.alpha_c)            & 0x7u) << 7u) |
            ((static_cast<std::uint32_t>(cfg.alpha_b)            & 0x7u) << 10u)|
            ((static_cast<std::uint32_t>(cfg.alpha_a)            & 0x7u) << 13u)|
            ((static_cast<std::uint32_t>(cfg.alpha_bias)         & 0x3u) << 16u)|
            (cfg.alpha_sub ? (1u << 18u) : 0u)                                  |
            (cfg.alpha_clamp ? (1u << 19u) : 0u)                                |
            ((static_cast<std::uint32_t>(cfg.alpha_scale)        & 0x3u) << 20u)|
            ((static_cast<std::uint32_t>(cfg.alpha_dest)         & 0x3u) << 22u);

        // order word: TevOrder low 12 bits, repacked.
        //   texmap(2:0), texcoord(5:3), tex_enable(6), ras_channel(9:7)
        const std::uint32_t order =
            (static_cast<std::uint32_t>(cfg.order.texmap)        & 0x7u)        |
            ((static_cast<std::uint32_t>(cfg.order.texcoord)     & 0x7u) << 3u) |
            (cfg.order.tex_enable ? (1u << 6u) : 0u)                            |
            ((static_cast<std::uint32_t>(cfg.order.ras_channel)  & 0x7u) << 7u);

        // ksel word: kcsel(4:0) | kasel(9:5) | resolved swap swizzles.
        // Swap tables are in the even/odd KSEL register pairs: table t = {red: reg(2t) bits 0-1,
        // green: reg(2t) bits 2-3, blue: reg(2t+1) bits 0-1, alpha: reg(2t+1) bits 2-3}.
        // ras_swap and tex_swap select the table. As in Dolphin's PixelShaderGen.cpp, the resolved
        // 4-channel swizzle is baked per stage into the key, so different tables give different keys.
        const TevKSel ras_even = state.tev_ksel(cfg.ras_swap * 2u);
        const TevKSel ras_odd  = state.tev_ksel(cfg.ras_swap * 2u + 1u);
        const TevKSel tex_even = state.tev_ksel(cfg.tex_swap * 2u);
        const TevKSel tex_odd  = state.tev_ksel(cfg.tex_swap * 2u + 1u);
        std::uint32_t ksel =
            (static_cast<std::uint32_t>(cfg.kcsel) & 0x1Fu) |
            ((static_cast<std::uint32_t>(cfg.kasel) & 0x1Fu) << 5u) |
            (static_cast<std::uint32_t>(ras_even.swap_red)   << 10u) |
            (static_cast<std::uint32_t>(ras_even.swap_green) << 12u) |
            (static_cast<std::uint32_t>(ras_odd.swap_red)    << 14u) |
            (static_cast<std::uint32_t>(ras_odd.swap_green)  << 16u) |
            (static_cast<std::uint32_t>(tex_even.swap_red)   << 18u) |
            (static_cast<std::uint32_t>(tex_even.swap_green) << 20u) |
            (static_cast<std::uint32_t>(tex_odd.swap_red)    << 22u) |
            (static_cast<std::uint32_t>(tex_odd.swap_green)  << 24u);

        // A stage's texture, rasterized colour and konst values matter only through its eight combiner
        // inputs. When a stage selects none of a source, its order bits, swap table and konst selector
        // cannot change the output, so leftover register state must not compile a duplicate shader.
        // Z textures read the last sampled texel regardless of the combiner, and indirect stages pass
        // texture coordinates forward, so the texture order is kept when either applies.
        const TevColorArg color_inputs[4] = {
            cfg.color_a, cfg.color_b, cfg.color_c, cfg.color_d};
        const TevAlphaArg alpha_inputs[4] = {
            cfg.alpha_a, cfg.alpha_b, cfg.alpha_c, cfg.alpha_d};
        bool uses_tex = false;
        bool uses_ras = false;
        bool uses_konst_color = false;
        bool uses_konst_alpha = false;
        for (const TevColorArg input : color_inputs) {
            uses_tex |= input == TevColorArg::TexColor ||
                input == TevColorArg::TexAlpha;
            uses_ras |= input == TevColorArg::RasColor ||
                input == TevColorArg::RasAlpha;
            uses_konst_color |= input == TevColorArg::Konst;
        }
        for (const TevAlphaArg input : alpha_inputs) {
            uses_tex |= input == TevAlphaArg::TexAlpha;
            uses_ras |= input == TevAlphaArg::RasAlpha;
            uses_konst_alpha |= input == TevAlphaArg::Konst;
        }
        std::uint32_t canonical_order = order;
        if (!uses_tex && !ztex_on && key.num_ind_stages == 0u) {
            canonical_order &= ~0x7Fu;          // texmap, texcoord, tex_enable
            ksel &= ~(0xFFu << 18u);            // tex swap swizzles
        }
        if (!uses_ras) {
            canonical_order &= ~(0x7u << 7u);   // ras channel
            ksel &= ~(0xFFu << 10u);            // ras swap swizzles
        }
        if (!uses_konst_color) {
            ksel &= ~0x1Fu;
        }
        if (!uses_konst_alpha) {
            ksel &= ~(0x1Fu << 5u);
        }

        key.stages[s] = TevStagePacked{color_env, alpha_env, canonical_order, ksel};
    }
    // Stages beyond num_tev_stages are zero from the value-initialisation of `key`. The hash is a
    // byte-wise FNV over the whole struct, so this zeroing is required even though the stage count
    // is the real key input.

    return key;
}

// ---------------------------------------------------------------------------
// VertexShaderKey
// ---------------------------------------------------------------------------

VertexShaderKey build_vertex_shader_key(const GxState& state) {
    VertexShaderKey key{};
    std::memset(&key, 0, sizeof(key));

    const GenMode gm = state.gen_mode();
    key.num_texgens = gm.num_texgens;
    key.num_chans   = gm.num_color_chans;

    // flags: bit0 per-vertex posmtx index present in the vertex stream,
    //        bit1 lighting enabled (any channel has lighting_enable set).
    // The pnmtx flag comes from CP VCD_LO bit 0 (the per-vertex matrix index in the vertex
    // descriptor), because GxState's public API does not expose the VCD directly.
    const std::uint32_t vcd_lo = state.cp(cp::kVcdLo);
    const bool per_vertex_posmtx = (vcd_lo & 1u) != 0u;

    bool lighting_active = false;
    for (unsigned ch = 0; ch < key.num_chans && ch < 2u; ++ch) {
        if (state.channel_ctrl(ch).lighting_enable ||
            state.channel_ctrl(2u + ch).lighting_enable) {
            lighting_active = true;
            break;
        }
    }

    // Dual-tex (post-transform matrix) enable: XF 0x1012 bit 0 (Dolphin XFMemory.h XFMEM_DUALTEX /
    // DualTexInfo.enabled). Gates the post-matrix multiply in the VS. It affects codegen, so it is
    // part of the key.
    const bool dual_tex =
        (state.xf(xf::kDualTexTrans) & 1u) != 0u;

    key.flags = static_cast<std::uint8_t>(
        (per_vertex_posmtx ? 0x01u : 0u) |
        (lighting_active   ? 0x02u : 0u) |
        (dual_tex          ? 0x04u : 0u));

    // Per-texgen per-vertex texture matrix index (VCD_LO bits 1-8). It affects VS codegen
    // (per-vertex index vs CP MatrixIndexA/B default). Masked to the active texgen count so inactive
    // bits do not fragment the cache.
    const std::uint32_t active_mask =
        (key.num_texgens >= 8u) ? 0xFFu : ((1u << key.num_texgens) - 1u);
    key.texmtx_idx_mask =
        static_cast<std::uint8_t>(((vcd_lo >> 1) & 0xFFu) & active_mask);

    // Dual transforms and matrix-index inputs are unused by color/emboss
    // texgens. Unknown type encodings use the generator's regular fallback.
    bool matrix_texgen_active = false;
    // Encode only the active texgen and post-texgen words; unused slots stay 0.
    for (unsigned i = 0; i < key.num_texgens && i < kMaxTexGens; ++i) {
        key.texgen[i] = state.xf(
            static_cast<std::uint16_t>(xf::kTexGenBase + i));
        // Fields the generator ignores for a texgen type are leftover register
        // state; keep them out of the key so they cannot duplicate a shader.
        // Color texgens read only their type; regular texgens never read the
        // emboss source/light; emboss reads only its source and light.
        switch (decode_xf_texgen(key.texgen[i]).type) {
        case TexGenType::Color0:
        case TexGenType::Color1:
            key.texgen[i] &= 0x7u << 4u;
            key.texmtx_idx_mask &= static_cast<std::uint8_t>(~(1u << i));
            break;
        case TexGenType::EmbossMap:
            key.texgen[i] &= (0x7u << 4u) | (0x3Fu << 12u);
            key.texmtx_idx_mask &= static_cast<std::uint8_t>(~(1u << i));
            break;
        case TexGenType::Regular:
        default:
            // Projection, input form, type and source row are the only fields
            // read by this path. Bits 0/3 and all high bits are reserved.
            key.texgen[i] &= (1u << 1u) | (1u << 2u) |
                (0x7u << 4u) | (0x1Fu << 7u);
            matrix_texgen_active = true;
            break;
        }
        // Matrix rows are supplied through inline uniforms from raw XF state.
        // Only normalize changes HLSL, and only for regular dual texgens.
        if (dual_tex && decode_xf_texgen(key.texgen[i]).type == TexGenType::Regular) {
            key.post_texgen[i] = state.xf(
                static_cast<std::uint16_t>(xf::kPostTexGenBase + i)) & (1u << 8u);
        }
    }

    if (!matrix_texgen_active) key.flags &= static_cast<std::uint8_t>(~0x04u);

    // Channel control registers are grouped by component (color0, color1, alpha0, alpha1), not
    // interleaved. The key keeps that layout so shader generation can pair colour N with alpha N.
    // An unlit channel reads only its material source (bit 0). The ambient source, light mask and
    // diffuse/attenuation functions are leftover state and must stay out of the key.
    const auto canonical_channel = [](std::uint32_t control) {
        return (control & 0x2u) != 0u ? (control & 0x7FFFu) : (control & 0x1u);
    };
    for (unsigned ch = 0; ch < key.num_chans && ch < 2u; ++ch) {
        key.channel[ch] = canonical_channel(state.xf(
            static_cast<std::uint16_t>(xf::kChannelCtrlBase + ch)));
        key.channel[2u + ch] = canonical_channel(state.xf(
            static_cast<std::uint16_t>(xf::kChannelCtrlBase + 2u + ch)));
    }

    return key;
}

// ---------------------------------------------------------------------------
// RenderStateKey
// ---------------------------------------------------------------------------

RenderStateKey build_render_state_key(const GxState& state) {
    RenderStateKey key{};
    std::memset(&key, 0, sizeof(key));

    const BlendMode bm = state.blend_mode();
    const PeControl pe = state.pe_control();

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
    // EFB write masks (cmode0 bits 3/4): bit25 = color_update, bit26 = alpha_update. GX z-clear and
    // z-prepass draws disable the colour update while writing depth. Without honouring these masks,
    // the clear quads would wipe the colour EFB they are meant only to z-fill.
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
    const ZMode zm = state.z_mode();
    // A disabled compare passes unconditionally, so the function field is leftover state then.
    key.zmode_bits = static_cast<std::uint16_t>(
        (zm.test_enable   ? 1u : 0u)                                            |
        (zm.test_enable
             ? ((static_cast<std::uint32_t>(zm.func) & 0x7u) << 1u) : 0u)       |
        (zm.update_enable ? (1u << 4u) : 0u));

    const GenMode gm = state.gen_mode();
    key.cull = static_cast<std::uint8_t>(gm.cull);

    // The pipeline only distinguishes RGBA6 (has destination alpha) from the
    // other formats.
    key.pixfmt = pe.pixel_format == 1u ? 1u : 0u;
    // The backend overwrites this per draw before the PSO lookup. Triangle is the safe default for
    // tests and callers that build only render-state keys.
    key.primitive_topology = 3u; // D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE

    return key;
}

}  // namespace galaxy::gx
