#pragma once

// Canonicalized, hashable shader and pipeline keys.
//
// Canonicalization rule: every field that does not affect code generation for
// the *enabled* stage count is forced to zero before hashing — disabled TEV
// stages, unused texgens, and don't-care blend fields must not fragment the
// cache.  GxBackend builds keys via the build_* helpers; raw construction is
// reserved for tests.
//
// All key types are padding-free (asserted) so byte-wise FNV-1a hashing and
// defaulted equality are well-defined.
//
// No Windows/D3D12 includes — unit-testable offline.

#include "galaxy/gx/gx_bitfields.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace galaxy::gx {

class GxState;

[[nodiscard]] inline std::uint64_t fnv1a64(
    const void* data,
    std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::uint64_t hash = 0xCBF29CE484222325ull;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 0x100000001B3ull;
    }
    return hash;
}

// One TEV stage, bit-packed exactly as the uber-shader consumes it
// (uber_constants.h stage_config words share this encoding).
struct TevStagePacked {
    std::uint32_t color_env;        // BP 0xC0+2n value (24 bits used)
    std::uint32_t alpha_env;        // BP 0xC1+2n value (24 bits used)
    std::uint32_t order;            // decoded TevOrder half, repacked low 12
    std::uint32_t ksel;             // kcsel(4:0) | kasel(9:5) |
                                    // resolved ras swap swizzle r/g/b/a
                                    // (11:10,13:12,15:14,17:16) | resolved
                                    // tex swap swizzle (19:18..25:24).  The
                                    // swizzles come from the KSEL swap TABLES
                                    // selected by this stage's ras_swap /
                                    // tex_swap — baking the resolved channels
                                    // keys the shader correctly without
                                    // adding the table registers themselves
                                    // (Dolphin PixelShaderGen uid does the
                                    // same).

    bool operator==(const TevStagePacked&) const = default;
};
static_assert(sizeof(TevStagePacked) == 16);

struct PixelShaderKey {
    std::uint8_t num_tev_stages;    // 1-16
    std::uint8_t num_texgens;
    std::uint8_t num_chans;
    std::uint8_t fog_type;          // FogType (params are uniforms, type is key)
    std::uint8_t alpha_comp0;       // CompareFunc (refs are uniforms)
    std::uint8_t alpha_comp1;
    std::uint8_t alpha_logic;       // AlphaTestLogic
    std::uint8_t flags;             // bit0 late_ztest, bit1 z_texture,
                                    // bit2 dst_alpha (GXSetDstAlpha on RGBA6
                                    // EFB with alpha_update), bits3-4 ztex op,
                                    // bits5-6 ztex format, bit7 dual-source
                                    // TEV source alpha → PS exports TEV alpha
                                    // on SV_Target1 for SRC1 blend factors
    std::uint8_t output_flags;      // bits0-2 EFB format, bit3 RGBA6 dither
    std::uint8_t num_ind_stages;    // 0-4
    std::uint8_t efb_scale_minus_one; // 0..kMaxEfbScale-1 for RGBA6 dither codegen
    std::uint8_t reserved;
    std::uint32_t ind_cmd[4][4];    // BP 0x10-0x1F, grouped as uint4s
    std::uint32_t ind_ref;          // BP 0x27
    std::uint32_t ind_scale[2];     // BP 0x25-0x26
    std::uint32_t ind_reserved;
    TevStagePacked stages[16];      // zeroed past num_tev_stages

    bool operator==(const PixelShaderKey&) const = default;
    [[nodiscard]] std::uint64_t hash() const {
        return fnv1a64(this, sizeof(*this));
    }
};
static_assert(sizeof(PixelShaderKey) == 12 + 4 * 4 * 4 + 4 * 4 +
                                       16 * sizeof(TevStagePacked));
static_assert(std::has_unique_object_representations_v<PixelShaderKey>);

struct VertexShaderKey {
    std::uint8_t num_texgens;
    std::uint8_t num_chans;
    std::uint8_t flags;             // bit0 per-vertex posmtx, bit1 lighting,
                                    // bit2 dual-tex (post-matrix) enable
    std::uint8_t texmtx_idx_mask;   // VCD_LO bits 1-8: per-vertex texture
                                    // matrix index present for texgen n
                                    // (masked to active texgens — affects VS
                                    // codegen: per-vertex idx vs MatrixIndexA/B)
    std::uint32_t texgen[8];        // raw XF 0x1040+n (zeroed past num_texgens)
    std::uint32_t post_texgen[8];   // raw XF 0x1050+n (dual-tex transforms)
    std::uint32_t channel[4];       // raw XF 0x100E+n: color0, color1,
                                    // alpha0, alpha1 (zeroed past num_chans)

    bool operator==(const VertexShaderKey&) const = default;
    [[nodiscard]] std::uint64_t hash() const {
        return fnv1a64(this, sizeof(*this));
    }
};
static_assert(sizeof(VertexShaderKey) == 4 + 8 * 4 + 8 * 4 + 4 * 4);
static_assert(std::has_unique_object_representations_v<VertexShaderKey>);

// Fixed-function PSO state — affects pipeline creation only, never HLSL
// codegen.  Kept apart from the shader keys so one compiled shader pair is
// shared across blend/depth variants.
struct RenderStateKey {
    std::uint32_t blend_bits;       // bit27 dual-source alpha, bit28 constant
                                    // EFB alpha overwrite; PE_CMODE0/1 fields
                                    // when blending/logic disabled)
    std::uint16_t zmode_bits;       // canonicalized BP 0x40
    std::uint8_t cull;              // CullMode
    std::uint8_t pixfmt;            // PeControl pixel format (dst-alpha cap)
    std::uint8_t primitive_topology; // D3D12_PRIMITIVE_TOPOLOGY_TYPE value
    std::uint8_t reserved[7];

    bool operator==(const RenderStateKey&) const = default;
};
static_assert(sizeof(RenderStateKey) == 16);
static_assert(std::has_unique_object_representations_v<RenderStateKey>);

struct PsoKey {
    std::uint64_t vs_hash;
    std::uint64_t ps_hash;
    RenderStateKey render_state;

    bool operator==(const PsoKey&) const = default;
    [[nodiscard]] std::uint64_t hash() const {
        return fnv1a64(this, sizeof(*this));
    }
};
static_assert(sizeof(PsoKey) == 32);
static_assert(std::has_unique_object_representations_v<PsoKey>);

struct PsoKeyHasher {
    [[nodiscard]] std::size_t operator()(const PsoKey& key) const {
        return static_cast<std::size_t>(key.hash());
    }
};

// Key builders — the only sanctioned construction path outside tests.  Each
// applies the canonicalization rule against the current register state.
[[nodiscard]] PixelShaderKey build_pixel_shader_key(
    const GxState& state,
    unsigned efb_scale);
[[nodiscard]] VertexShaderKey build_vertex_shader_key(const GxState& state);
[[nodiscard]] RenderStateKey build_render_state_key(const GxState& state);

}  // namespace galaxy::gx
