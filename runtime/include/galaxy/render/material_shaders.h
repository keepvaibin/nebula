#pragma once

// The material shaders: one pixel source (runtime/src/render/material_ps.hlsl) and one vertex source (material_vs.hlsl),
// compiled when Nebula is built in variants that fix only their interfaces (lighting inputs and outputs, motion, curved
// surfaces, render targets, early depth), and the fixed stages (lines and points, curved surfaces' hull and domain
// stages, the curve sort). A draw's material and vertex programs are data: pack_material_program and
// pack_vertex_program write them into the constants, and the shaders read them per draw.
//
// D3D12 takes the variants' DXBC compiled at build time (material_ps_dxbc, material_vs_dxbc, ...). The Vulkan backend
// compiles the same sources (material_ps_hlsl, material_vs_hlsl) with its device compiler until its SPIR-V is built
// ahead too (E-19).

#include "galaxy/render/shader_keys.h"
#include "galaxy/render/uber_constants.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
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


// The material vertex shader (runtime/src/render/material_vs.hlsl): sixteen variants by the outputs a draw needs
// (per-pixel or enhanced lighting's, motion, curved surfaces) and interpolated redraw's vertex blending; the vertex
// program (coordinate generators, channels, matrix slots) is data in VertexConstants (pack_vertex_program).
inline constexpr unsigned kMaterialVsVariants = 16u;
struct MaterialVsVariant {
    bool light_out = false;  // kVsKeyPerPixelLighting or kVsKeyEnhancedLighting
    bool motion = false;     // kVsKeyMotion
    bool curved = false;     // kVsKeyCurved
    bool blend = false;      // kVsFeatureVertexBlend
};
[[nodiscard]] constexpr MaterialVsVariant material_vs_variant_form(unsigned index) noexcept {
    return MaterialVsVariant{(index & 1u) != 0u, (index & 2u) != 0u, (index & 4u) != 0u, (index & 8u) != 0u};
}
[[nodiscard]] constexpr unsigned material_vs_variant(const VertexShaderKey& key) noexcept {
    return ((key.flags & (kVsKeyPerPixelLighting | kVsKeyEnhancedLighting)) != 0u ? 1u : 0u) |
           ((key.flags & kVsKeyMotion) != 0u ? 2u : 0u) | ((key.flags & kVsKeyCurved) != 0u ? 4u : 0u) |
           ((key.features & kVsFeatureVertexBlend) != 0u ? 8u : 0u);
}
// The uber stand-in's vertex variant: no lighting, motion or curve outputs, vertex blending (its draws may be redrawn).
inline constexpr unsigned kMaterialVsStandIn = 8u;

[[nodiscard]] std::string material_vs_defines(unsigned variant);
[[nodiscard]] std::string_view material_vs_source() noexcept;
[[nodiscard]] std::string material_vs_hlsl(const VertexShaderKey& key);
[[nodiscard]] std::span<const std::uint8_t> material_vs_dxbc(unsigned variant) noexcept;

// The fixed stages: lines and points as quads (geometry), curved surfaces' hull and domain stages by the vertex
// output variant (curved_variant: kCurvedPerPixel, kCurvedMotion), and the curve sort (compute: classify, offsets,
// scatter; RendererD3D12::sort_curved).
[[nodiscard]] std::string line_geometry_shader_source();
[[nodiscard]] std::string point_geometry_shader_source();
[[nodiscard]] std::string curved_stage_source(unsigned variant, bool domain);
[[nodiscard]] std::string curve_sort_source();
[[nodiscard]] std::span<const std::uint8_t> line_geometry_dxbc() noexcept;
[[nodiscard]] std::span<const std::uint8_t> point_geometry_dxbc() noexcept;
[[nodiscard]] std::span<const std::uint8_t> curved_stage_dxbc(unsigned variant, bool domain) noexcept;

// A key in one line, for logs.
[[nodiscard]] std::string describe_key(const PixelShaderKey& key);
[[nodiscard]] std::string describe_key(const VertexShaderKey& key);

// Writes a key's vertex program into the words material_vs.hlsl reads: uber_vs_header (coordinates, channels, key
// flags, matrix slots | features << 8), uber_texgen and uber_post_texgen (each coordinate's words), uber_channel.
inline void pack_vertex_program(const VertexShaderKey& key, VertexConstants& out) noexcept {
    out.uber_vs_header[0] = key.num_texgens;
    out.uber_vs_header[1] = key.num_chans;
    out.uber_vs_header[2] = key.flags;
    out.uber_vs_header[3] = static_cast<std::uint32_t>(key.texmtx_idx_mask) | key.features << 8u;
    for (unsigned i = 0; i < 8u; ++i) {
        out.uber_texgen[i] = texgen_bits(key.texgen[i]);
        out.uber_post_texgen[i] = post_texgen_bits(key.texgen[i]);
    }
    for (unsigned c = 0; c < 4u; ++c) {
        out.uber_channel[c] = channel_bits(key.channel[c]);
    }
}
// The key whose program the vertex constants last uploaded hold (RenderCore uploads them again when it changes).
struct VertexProgramMemo {
    VertexShaderKey key{};
    bool valid = false;
    [[nodiscard]] bool holds(const VertexShaderKey& next) const noexcept {
        return valid && next == key;
    }
    void note(const VertexShaderKey& next) noexcept {
        key = next;
        valid = true;
    }
};

// The material program's fields of PixelConstants (pack_material_program).
inline void apply_uber_ps_key(const PixelShaderKey& key, PixelConstants& out) noexcept {
    pack_material_program(key, out);
}
// The vertex program's fields of VertexConstants (pack_vertex_program).
inline void apply_uber_vs_key(const VertexShaderKey& key, VertexConstants& out) noexcept {
    pack_vertex_program(key, out);
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

}  // namespace galaxy::render
