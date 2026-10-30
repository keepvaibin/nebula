#pragma once

// The drawing state the game's records set, as typed values: the material record's and the state ops' (requirement
// E-02: the renderer reads no register words). It is built beside the register file (GxState) while the readers move
// to it one group at a time; a check mode compares the two at every draw.

#include "galaxy/gx/gx_bitfields.h"
#include "galaxy/gx/native_material.h"
#include "galaxy/gx/native_state.h"

#include <string>

namespace galaxy::gx {

struct NativeDrawState {
    // Fixed-function state: the counts and cull of the generation mode, depth, blend (as the game set it; the
    // pixel engine's effective blend is effective_blend's), the pixel format and the alpha compare.
    GenMode gen{0u, 0u, 1u, CullMode::None, 0u, false};
    ZMode z{false, CompareFunc::Never, false};
    BlendMode blend{false, false, false, false, false, BlendFactor::Zero, BlendFactor::Zero, false, 0u, false, 0u};
    PeControl pe{0u, 0u, false};
    AlphaCompare alpha{0u, 0u, CompareFunc::Never, CompareFunc::Never, AlphaTestLogic::And};

    // A state op (galaxy/gx/native_state.h), as the GX function it names sets the state.
    void apply(const state::Op& op);
    // A material record: the whole material, or (diff_flags) the parts J3D's change flags name.
    void apply(const material::Material& material);
};

// The fields where `state` differs from `expected` (empty when equal), for the check mode.
[[nodiscard]] std::string draw_state_differences(const NativeDrawState& state, const NativeDrawState& expected);

}  // namespace galaxy::gx
