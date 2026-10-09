// shader_keys.cpp — Build canonicalized shader and pipeline keys from GxState.
//
// Canonicalization rule (from shader_keys.h): every field that does not affect
// code generation for the *enabled* stage count is forced to zero so that FNV-1a
// hashing produces consistent cache hits.  Disabled TEV stages, unused texgens,
// and don't-care blend fields must not fragment the cache.

#include "galaxy/gx/shader_keys.h"

#include "galaxy/gx/gx_state.h"
#include "galaxy/gx/render_config.h"
#include "galaxy/gx/uber_constants.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace galaxy::gx {

namespace {

// GX blending consumes the TEV-computed source alpha. The EFB alpha value that
// eventually gets stored may be quantized to the physical target format or
// replaced by GXSetDstAlpha, so source-alpha blend factors need a separate
// export whenever they cannot safely read color0.a.
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

PixelShaderKey build_pixel_shader_key(
    const GxState& state,
    unsigned efb_scale) {
    PixelShaderKey key{};
    // Zero-initialise the entire struct first — this covers the padding between
    // num_tev_stages..flags and the trailing stages[] entries so the hash is
    // deterministic regardless of the stage count.
    std::memset(&key, 0, sizeof(key));

    const GenMode gm = state.gen_mode();
    key.num_tev_stages = gm.num_tev_stages;     // 1-16
    key.num_texgens    = gm.num_texgens;
    // Color-channel production belongs to the VS; PS consumes its interpolants.
    // No pixel code depends on this count, so retain one canonical PS variant.
    key.num_chans      = 0u;

    // Fog type is baked into the key; the actual A/B/C/colour params are
    // uniforms and must not enter the key.
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
    // SMG clears color+depth with a fullscreen quad whose depth comes from a
    // Z24X8 texture via GX_ZT_REPLACE (MainLoopFramework::clearEfb) — without
    // this the quad writes geometric near-depth and poisons the depth buffer.
    //
    // dst_alpha (bit2) was previously keyed on pixel_format==RGBA6 ALONE,
    // which forced frag.a = const_dst_alpha (0 when GXSetDstAlpha is off) for
    // EVERY scene draw — SMG renders the scene in RGBA6, so every blended
    // material collapsed to alpha 0 and SrcAlpha blending made it INVISIBLE
    // (the whole-scene "only the blue clear survives" blocker).  The override
    // is correct only when GXSetDstAlpha is actually ENABLED; otherwise the
    // fragment must carry its TEV-computed alpha so the blend works.
    // alpha_update gates it too (Dolphin PixelShaderGen useDstAlpha): with
    // EFB alpha writes masked off the constant can never land in the EFB,
    // and overriding frag.a would still corrupt the blend factors.
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
        ((pe.pixel_format == 1u && bm.dither) ? 0x08u : 0u));
    // Scale is a per-draw constant, so preparing one material covers every
    // internal resolution. A distinct mode preserves the identity/source of
    // existing static-scale DXBC in the persistent shader cache.
    static_cast<void>(efb_scale); // Retain the public call signature.
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

    // Encode each active TEV stage. Stages beyond num_tev_stages stay zero
    // because `key` is value-initialised (see the construction above), not
    // because of the explicit memset: the memset writes the same zeros and is
    // redundant. Kept deliberately, because `PixelShaderKey::hash()` is a
    // byte-wise FNV over `sizeof(PixelShaderKey)` and equality is byte-wise too
    // (`has_unique_object_representations_v` is asserted for this type), so every
    // byte must be determinate; anyone who removes one of the two zeroings must
    // keep the other.
    for (unsigned s = 0; s < key.num_tev_stages; ++s) {
        const TevStageConfig cfg = state.tev_stage(s);

        // color_env word: reconstruct the raw BP bit-packing from the decoded
        // fields so the key carries exactly what the shader key encoding needs.
        // Bits match TevStagePacked.color_env (BP 0xC0+2*s layout).
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

        // Swap selectors are represented by resolved swizzles in ksel, so
        // their raw low four bits do not distinguish shader code.
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

        // order word: re-pack TevOrder low 12 bits
        //   texmap(2:0), texcoord(5:3), tex_enable(6), ras_channel(9:7)
        const std::uint32_t order =
            (static_cast<std::uint32_t>(cfg.order.texmap)        & 0x7u)        |
            ((static_cast<std::uint32_t>(cfg.order.texcoord)     & 0x7u) << 3u) |
            (cfg.order.tex_enable ? (1u << 6u) : 0u)                            |
            ((static_cast<std::uint32_t>(cfg.order.ras_channel)  & 0x7u) << 7u);

        // ksel word: kcsel(4:0) | kasel(9:5) | resolved swap swizzles.
        // The swap TABLES live in the even/odd KSEL register pairs: table t =
        // {red: reg(2t) bits 0-1, green: reg(2t) bits 2-3,
        //  blue: reg(2t+1) bits 0-1, alpha: reg(2t+1) bits 2-3}
        // (Dolphin PixelShaderGen.cpp bakes the resolved 4-channel swizzle
        // per stage into the shader uid the same way).  cfg.ras_swap /
        // cfg.tex_swap select the table; we bake the resolved channels so
        // different tables produce different shader keys.
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

        // A stage's texture, rasterized-color and konst values are observed
        // only through its eight combiner inputs (the generator reads
        // tex_color_i / ras_color_i / konst_i nowhere else). When a stage
        // selects none of a source, its order bits, swap table and konst
        // selector cannot change the output; leftover register state from an
        // earlier material must not compile a duplicate shader. Z-textures read
        // the last sampled texel regardless of the combiner, and indirect
        // stages carry texture coordinates forward, so the texture order is
        // kept whenever either is active.
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
    // Stages beyond num_tev_stages are zero, from the value-initialisation of
    // `key` rather than from the redundant memset beside it. The hash is a
    // byte-wise FNV over the whole struct, so this zeroing is load-bearing even
    // though the stage count is the real key input.

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
    // The VCD state is not directly available via GxState public API for the
    // pnmtx flag, so we check whether any CP VCD_LO bit 0 indicates per-vertex
    // matrix index in the vertex descriptor.
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

    // Dual-tex (post-transform matrix) enable: XF 0x1012 bit 0 (Dolphin
    // XFMemory.h XFMEM_DUALTEX / DualTexInfo.enabled).  Gates the post-matrix
    // multiply in the VS — codegen-affecting, so it is part of the key.
    const bool dual_tex =
        (state.xf(xf::kDualTexTrans) & 1u) != 0u;

    key.flags = static_cast<std::uint8_t>(
        (per_vertex_posmtx ? 0x01u : 0u) |
        (lighting_active   ? 0x02u : 0u) |
        (dual_tex          ? 0x04u : 0u));

    // Per-texgen per-vertex texture matrix index (VCD_LO bits 1-8) — affects
    // VS codegen (per-vertex index vs CP MatrixIndexA/B default).  Masked to
    // the active texgen count so inactive bits don't fragment the cache.
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

    // Channel control registers are grouped by component, not interleaved:
    // color0, color1, alpha0, alpha1. Preserve that layout in the key so
    // shader generation can pair color N with alpha N.
    // An unlit channel part reads only its material source (bit 0); the
    // ambient source, light mask and diffuse/attenuation functions are
    // leftover state and must not fragment the key.
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

    // An EFB without alpha reads destination alpha as one. Normalize these
    // factors before hashing, as Aurora does in populate_pipeline_config;
    // pipeline_cache.cpp already applies the same mapping to the D3D12 PSO.
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

    // Canonicalize blend_bits: when blending is disabled and logic ops are
    // disabled, zero all blend fields so they don't fragment the cache.
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
    // EFB write masks (cmode0 bits 3/4): bit25 = color_update,
    // bit26 = alpha_update.  GX z-clear / z-prepass draws disable the color
    // update while writing depth — without honoring these masks the clear
    // quads wipe the color EFB they are only meant to z-fill.
    if (bm.color_update) {
        key.blend_bits |= (1u << 25u);
    }
    if (bm.alpha_update && pe.pixel_format == 1u) {
        key.blend_bits |= (1u << 26u);
    }
    // bit27 = dual-source TEV alpha (must agree with PixelShaderKey flags
    // bit7): the PSO swaps SrcAlpha/InvSrcAlpha for SRC1_ALPHA/
    // INV_SRC1_ALPHA so the blender consumes TEV alpha while color0.a remains
    // the physical EFB alpha.
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
    // A disabled compare passes unconditionally, so the function field is
    // leftover state then (observed: z=0x6 alongside z=0x0 for one material).
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
    // Backend overwrites this per draw before PSO lookup. Triangle is the
    // safe default for tests and callers that only build render-state keys.
    key.primitive_topology = 3u; // D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE

    return key;
}

}  // namespace galaxy::gx
