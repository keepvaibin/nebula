#pragma once

// Uber pipeline support: the HLSL sources (runtime/src/gx/uber_vs.hlsl and
// uber_ps.hlsl, embedded at configure time) and the per-draw packing of the
// shader keys into the constant-buffer fields those shaders interpret.
//
// The uber shaders run the same arithmetic as ShaderGenerator's specialized
// output with every key decision taken at run time, so a draw whose
// specialized pipeline is still compiling renders with the uber pipeline of
// its render state instead of stalling the render thread.

#include "galaxy/gx/shader_keys.h"
#include "galaxy/gx/uber_constants.h"

#include <cstdint>
#include <cstring>
#include <string_view>

namespace galaxy::gx {

[[nodiscard]] std::string_view uber_vs_source() noexcept;
[[nodiscard]] std::string_view uber_ps_source() noexcept;

// Output-signature variants of the uber pixel shader. A key needs exactly one:
// bit 0 dual-source TEV alpha (SV_Target1), bit 1 depth export (Z texture with
// late Z), bit 2 early depth test. Depth export never runs with early depth.
// Variants 6-8: the effects layer (kPsKeyLayer: reactive mask, layer colour and
// transmittance, never dual-source), late Z, depth export, early Z.
inline constexpr unsigned kUberPsVariants = 9u;
[[nodiscard]] constexpr unsigned uber_ps_variant(const PixelShaderKey& key) noexcept {
    const bool dual_source = (key.flags & 0x80u) != 0u;
    const bool z_texture = (key.flags & 0x02u) != 0u;
    const bool early_depth = (key.flags & 0x01u) == 0u;
    const bool export_depth = z_texture && !early_depth;
    if ((key.reserved & kPsKeyLayer) != 0u) {
        return 6u + (early_depth ? 2u : export_depth ? 1u : 0u);
    }
    if (early_depth) {
        return 4u + (dual_source ? 1u : 0u);
    }
    return (dual_source ? 1u : 0u) + (export_depth ? 2u : 0u);
}
[[nodiscard]] constexpr bool uber_ps_variant_layer(unsigned variant) noexcept {
    return variant >= 6u;
}
[[nodiscard]] constexpr bool uber_ps_variant_dual_source(unsigned variant) noexcept {
    return variant < 6u && (variant & 1u) != 0u;
}
[[nodiscard]] constexpr bool uber_ps_variant_export_depth(unsigned variant) noexcept {
    return variant == 2u || variant == 3u || variant == 7u;
}
[[nodiscard]] constexpr bool uber_ps_variant_early_depth(unsigned variant) noexcept {
    return (variant >= 4u && variant < 6u) || variant == 8u;
}

// The fields of GxPsConstants that specialized shaders bake from the key and
// never read: stage_config, ind_cmd, ind_misc and uber_ps_header.
inline void apply_uber_ps_key(const PixelShaderKey& key, GxPsConstants& out) noexcept {
    static_assert(sizeof(key.stages) == sizeof(out.stage_config));
    static_assert(sizeof(key.ind_cmd) == sizeof(out.ind_cmd));
    std::memcpy(out.stage_config, key.stages, sizeof(out.stage_config));
    std::memcpy(out.ind_cmd, key.ind_cmd, sizeof(out.ind_cmd));
    out.ind_misc[0] = key.ind_ref;
    out.ind_misc[1] = key.ind_scale[0];
    out.ind_misc[2] = key.ind_scale[1];
    out.ind_misc[3] = key.num_ind_stages;
    out.uber_ps_header[0] =
        static_cast<std::uint32_t>(key.num_tev_stages) |
        (static_cast<std::uint32_t>(key.num_texgens) << 8u) |
        (static_cast<std::uint32_t>(key.num_chans) << 16u) |
        (static_cast<std::uint32_t>(key.fog_type) << 24u);
    out.uber_ps_header[1] =
        static_cast<std::uint32_t>(key.alpha_comp0) |
        (static_cast<std::uint32_t>(key.alpha_comp1) << 8u) |
        (static_cast<std::uint32_t>(key.alpha_logic) << 16u) |
        (static_cast<std::uint32_t>(key.flags) << 24u);
    out.uber_ps_header[2] =
        static_cast<std::uint32_t>(key.output_flags) |
        (static_cast<std::uint32_t>(key.efb_scale_minus_one) << 16u);
    out.uber_ps_header[3] = 0u;
}

// GxVsConstants' uber tail is written straight into upload memory, so this
// takes the destination of each field rather than a whole struct.
inline void apply_uber_vs_key(const VertexShaderKey& key, GxVsConstants& out) noexcept {
    out.uber_vs_header[0] = key.num_texgens;
    out.uber_vs_header[1] = key.num_chans;
    out.uber_vs_header[2] = key.flags;
    out.uber_vs_header[3] = key.texmtx_idx_mask;
    static_assert(sizeof(key.texgen) == sizeof(out.uber_texgen));
    static_assert(sizeof(key.post_texgen) == sizeof(out.uber_post_texgen));
    static_assert(sizeof(key.channel) == sizeof(out.uber_channel));
    std::memcpy(out.uber_texgen, key.texgen, sizeof(out.uber_texgen));
    std::memcpy(out.uber_post_texgen, key.post_texgen, sizeof(out.uber_post_texgen));
    std::memcpy(out.uber_channel, key.channel, sizeof(out.uber_channel));
}

}  // namespace galaxy::gx
