#pragma once

// The material pixel shader (runtime/src/render/material_ps.hlsl): one source compiled when Nebula is built, in
// variants that fix only the shader's interface (lighting inputs, render targets, early depth). A draw's material
// program is data: pack_material_program writes it into PixelConstants, and the shader reads it per draw.
//
// D3D12 takes the variants' DXBC compiled at build time (material_ps_dxbc). The Vulkan backend compiles the same source
// (material_ps_hlsl) with its device compiler until its SPIR-V is built ahead too (E-19).

#include "galaxy/render/shader_keys.h"
#include "galaxy/render/uber_constants.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace galaxy::render {

// A variant's interface. `out` is one of the fifteen output forms below.
struct MaterialPsVariant {
    bool ppl = false;       // per-pixel GX lighting (kPsKeyPerPixelLighting)
    bool enhanced = false;  // enhanced lighting (kPsKeyEnhancedLighting)
    bool motion = false;    // motion target (kPsKeyMotion)
    bool reactive = false;  // reactive mask (kPsKeyReactive without motion)
    bool layer = false;     // effects layer targets (kPsKeyLayer, reactive draws)
    bool depth = false;     // depth export (a Z texture with late depth)
    bool dual = false;      // dual-source alpha (key flags bit 7)
    bool early = false;     // early depth test
};

// Output forms: plain, dual-source, depth export, motion, reactive and layer, each early or late where depth is not
// exported.
inline constexpr unsigned kMaterialPsOutputs = 15u;
inline constexpr unsigned kMaterialPsVariants = 4u * kMaterialPsOutputs;

[[nodiscard]] constexpr MaterialPsVariant material_ps_variant_form(unsigned index) noexcept {
    MaterialPsVariant v{};
    const unsigned light = index / kMaterialPsOutputs;
    v.ppl = (light & 1u) != 0u;
    v.enhanced = (light & 2u) != 0u;
    switch (index % kMaterialPsOutputs) {
    case 0u: break;
    case 1u: v.early = true; break;
    case 2u: v.dual = true; break;
    case 3u: v.dual = true; v.early = true; break;
    case 4u: v.depth = true; break;
    case 5u: v.depth = true; v.dual = true; break;
    case 6u: v.motion = true; break;
    case 7u: v.motion = true; v.early = true; break;
    case 8u: v.motion = true; v.depth = true; break;
    case 9u: v.reactive = true; break;
    case 10u: v.reactive = true; v.early = true; break;
    case 11u: v.reactive = true; v.depth = true; break;
    case 12u: v.reactive = true; v.layer = true; break;
    case 13u: v.reactive = true; v.layer = true; v.early = true; break;
    default: v.reactive = true; v.layer = true; v.depth = true; break;
    }
    return v;
}

// The variant a key draws with (the precedence of the outputs: motion, reactive, depth export, dual source).
[[nodiscard]] constexpr unsigned material_ps_variant(const PixelShaderKey& key) noexcept {
    const bool motion = (key.reserved & kPsKeyMotion) != 0u;
    const bool reactive = !motion && (key.reserved & kPsKeyReactive) != 0u;
    const bool layer = reactive && (key.reserved & kPsKeyLayer) != 0u;
    const bool early = (key.flags & 0x01u) == 0u;
    const bool depth = (key.flags & 0x02u) != 0u && !early;
    const bool dual = (key.flags & 0x80u) != 0u;
    unsigned out = 0u;
    if (motion) {
        out = depth ? 8u : early ? 7u : 6u;
    } else if (layer) {
        out = depth ? 14u : early ? 13u : 12u;
    } else if (reactive) {
        out = depth ? 11u : early ? 10u : 9u;
    } else if (depth) {
        out = dual ? 5u : 4u;
    } else if (dual) {
        out = early ? 3u : 2u;
    } else {
        out = early ? 1u : 0u;
    }
    const unsigned light = ((key.reserved & kPsKeyPerPixelLighting) != 0u ? 1u : 0u) |
                           ((key.reserved & kPsKeyEnhancedLighting) != 0u ? 2u : 0u);
    return light * kMaterialPsOutputs + out;
}

// The variant's defines as HLSL text ("#define MAT_PPL 1\n" ...), for a compile of material_ps_source().
[[nodiscard]] std::string material_ps_defines(unsigned variant);
// The shader's source (embedded at configure time).
[[nodiscard]] std::string_view material_ps_source() noexcept;
// The source of a key's variant: its defines, then the shader.
[[nodiscard]] std::string material_ps_hlsl(const PixelShaderKey& key);
// The variant's DXBC (ps_5_1, compiled at build time); empty in builds without the D3D12 renderer.
[[nodiscard]] std::span<const std::uint8_t> material_ps_dxbc(unsigned variant) noexcept;

// Writes a key's program into the words material_ps.hlsl reads: stage_config (each stage's colour, alpha, order and
// select words), ind_cmd (each stage's indirect command, bit 24 the command is active, bit 25 its coordinate is
// needed: by its own command or a later stage's add-previous), ind_misc (x orders, y-z scales, w the stage count and
// the enhanced material word bits 0-22 from bit 8) and uber_ps_header x-z (z bits 8-15: the key's option bits). The
// light word (uber_ps_header w) is RenderCore's.
inline void pack_material_program(const PixelShaderKey& key, PixelConstants& out) noexcept {
    std::uint32_t later_add_prev = 0u;  // stages after s that add the previous coordinate, as s's bit
    for (unsigned s = kMaxTevStages; s-- > 0u;) {
        const KeyStage& stage = key.stages[s];
        const bool command = key.num_ind_stages != 0u && s < key.num_tev_stages && indirect_active(stage);
        std::uint32_t word = ind_command_bits(stage);
        if (command) {
            word |= 1u << 24u;
        }
        if (command || later_add_prev != 0u) {
            word |= s < key.num_tev_stages ? 1u << 25u : 0u;
        }
        if (command && stage.ind_prev != 0u) {
            later_add_prev = 1u;
        }
        out.stage_config[s][0] = tev_color_bits(stage);
        out.stage_config[s][1] = tev_alpha_bits(stage);
        out.stage_config[s][2] = tev_order_bits(stage);
        out.stage_config[s][3] = tev_select_bits(stage);
        out.ind_cmd[s / 4u][s & 3u] = word;
    }
    out.ind_misc[0] = ind_order_bits(key);
    out.ind_misc[1] = ind_scale_bits(key, 0u);
    out.ind_misc[2] = ind_scale_bits(key, 1u);
    out.ind_misc[3] = static_cast<std::uint32_t>(key.num_ind_stages) | (key.ind_reserved & 0x7FFFFFu) << 8u;
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
        (static_cast<std::uint32_t>(key.reserved) << 8u) |
        (static_cast<std::uint32_t>(key.efb_scale_minus_one) << 16u);
    out.uber_ps_header[3] = 0u;
}

}  // namespace galaxy::render
