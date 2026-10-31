#pragma once

// The uber vertex shader (runtime/src/render/uber_vs.hlsl, embedded at configure time) and the per-draw packing of the
// shader keys into the constant-buffer fields the shaders read: the material pixel shader's program
// (pack_material_program, material_shaders.h) and the uber vertex shader's key.
//
// The uber vertex shader runs the same arithmetic as ShaderGenerator's specialized output with every key decision
// taken at run time, so a draw whose specialized vertex shader is still compiling renders with the uber pipeline of its
// render state instead of stalling the render thread.

#include "galaxy/render/material_shaders.h"
#include "galaxy/render/shader_keys.h"
#include "galaxy/render/uber_constants.h"

#include <cstdint>
#include <cstring>
#include <string_view>

namespace galaxy::render {

[[nodiscard]] std::string_view uber_vs_source() noexcept;

// The material program's fields of PixelConstants (pack_material_program).
inline void apply_uber_ps_key(const PixelShaderKey& key, PixelConstants& out) noexcept {
    pack_material_program(key, out);
}

// apply_uber_ps_key's fields for the last key packed: draws in a row mostly share their key, and packing reads every
// stage.
struct UberPsKeyMemo {
    PixelShaderKey key{};
    std::uint32_t stage_config[kMaxTevStages][4]{};
    std::uint32_t ind_cmd[4][4]{};
    std::uint32_t ind_misc[4]{};
    std::uint32_t header[4]{};
    bool valid = false;

    // Whether the constants last packed hold `next`'s program (RenderCore re-packs its pixel constants otherwise).
    [[nodiscard]] bool holds(const PixelShaderKey& next) const noexcept {
        return valid && next == key;
    }

    void apply(const PixelShaderKey& next, PixelConstants& out) noexcept {
        if (!valid || !(next == key)) {
            apply_uber_ps_key(next, out);
            std::memcpy(stage_config, out.stage_config, sizeof(stage_config));
            std::memcpy(ind_cmd, out.ind_cmd, sizeof(ind_cmd));
            std::memcpy(ind_misc, out.ind_misc, sizeof(ind_misc));
            std::memcpy(header, out.uber_ps_header, sizeof(header));
            key = next;
            valid = true;
            return;
        }
        std::memcpy(out.stage_config, stage_config, sizeof(stage_config));
        std::memcpy(out.ind_cmd, ind_cmd, sizeof(ind_cmd));
        std::memcpy(out.ind_misc, ind_misc, sizeof(ind_misc));
        std::memcpy(out.uber_ps_header, header, sizeof(header));
    }
};

// The uber pipelines' pixel variants: a stand-in pipeline pairs the uber vertex shader (no lighting or motion outputs)
// with one of these material pixel variants. Bit 0 dual-source alpha, bit 1 depth export, bit 2 early depth (variants
// 0-5); 6-8 the effects layer, late, depth export, early.
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
// The material pixel variant (material_shaders.h) of an uber variant: no lighting inputs.
[[nodiscard]] constexpr unsigned uber_ps_material_variant(unsigned variant) noexcept {
    constexpr unsigned kOutputs[kUberPsVariants] = {0u, 2u, 4u, 5u, 1u, 3u, 12u, 14u, 13u};
    return variant < kUberPsVariants ? kOutputs[variant] : 0u;
}

// VertexConstants' uber tail is written straight into upload memory, so this
// takes the destination of each field rather than a whole struct.
inline void apply_uber_vs_key(const VertexShaderKey& key, VertexConstants& out) noexcept {
    out.uber_vs_header[0] = key.num_texgens;
    out.uber_vs_header[1] = key.num_chans;
    out.uber_vs_header[2] = key.flags;
    out.uber_vs_header[3] = key.texmtx_idx_mask;
    for (unsigned i = 0; i < 8u; ++i) {
        out.uber_texgen[i] = texgen_bits(key.texgen[i]);
        out.uber_post_texgen[i] = post_texgen_bits(key.texgen[i]);
    }
    for (unsigned c = 0; c < 4u; ++c) {
        out.uber_channel[c] = channel_bits(key.channel[c]);
    }
}

}  // namespace galaxy::render
