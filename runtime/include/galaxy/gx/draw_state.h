#pragma once

// The drawing state the game's records set, as typed values: the material record's and the state ops' (requirement
// E-02: the renderer reads no register words). It is built beside the register file (GxState) while the readers move
// to it one group at a time; a check mode compares the two at every draw.

#include "galaxy/gx/gx_bitfields.h"
#include "galaxy/gx/native_material.h"
#include "galaxy/gx/native_state.h"

#include <string>

namespace galaxy::gx {

// One stage's indirect texturing (GXSetTevIndirect).
struct IndirectStage {
    std::uint8_t stage, format, bias, alpha, matrix, wrap_s, wrap_t;
    bool lod, prev;
};

struct NativeDrawState {
    // Fixed-function state: the counts and cull of the generation mode, depth, blend (as the game set it; the
    // pixel engine's effective blend is effective_blend's), the pixel format and the alpha compare.
    GenMode gen{0u, 0u, 1u, CullMode::None, 0u, false};
    ZMode z{false, CompareFunc::Never, false};
    BlendMode blend{false, false, false, false, false, BlendFactor::Zero, BlendFactor::Zero, false, 0u, false, 0u};
    PeControl pe{0u, 0u, false};
    AlphaCompare alpha{0u, 0u, CompareFunc::Never, CompareFunc::Never, AlphaTestLogic::And};

    // TEV: the stages, the four swap tables (each channel's source: red, green, blue, alpha), the fog's type, the
    // Z texture (op 0 off, 1 add, 2 replace; type 0 Z8, 1 Z16, 2 Z24X8; bias in Z24 units).
    TevStageConfig tev[16]{};
    std::uint8_t swap[4][4]{};
    FogType fog_type{FogType::Off};
    struct { std::uint8_t op, type; std::uint32_t bias; } z_texture{};
    // Indirect texturing: each TEV stage's, and the four indirect stages' map, coordinate and scales.
    IndirectStage ind[16]{};
    struct { std::uint8_t map, coord, scale_s, scale_t; } ind_order[4]{};

    // A state op (galaxy/gx/native_state.h), as the GX function it names sets the state.
    void apply(const state::Op& op);
    // A material record: the whole material, or (diff_flags) the parts J3D's change flags name.
    void apply(const material::Material& material);
};

// The indirect state in the pixel shader's packed form (its key and constants): a stage's command, the four stages'
// maps and coordinates, a pair of stages' scales.
[[nodiscard]] std::uint32_t ind_cmd_word(const IndirectStage& stage);
// A TEV stage's colour and alpha operations in the pixel shader's packed form (its constants).
[[nodiscard]] std::uint32_t tev_color_word(const TevStageConfig& stage);
[[nodiscard]] std::uint32_t tev_alpha_word(const TevStageConfig& stage);
[[nodiscard]] std::uint32_t ind_ref_word(const NativeDrawState& state);
[[nodiscard]] std::uint32_t ind_scale_word(const NativeDrawState& state, unsigned pair);

// The fields where `state` differs from `expected` (empty when equal), for the check mode.
[[nodiscard]] std::string draw_state_differences(const NativeDrawState& state, const NativeDrawState& expected);

}  // namespace galaxy::gx
