#pragma once

#include <nebula_types.h>

namespace MR {
    /// @brief The frames of Mario's eyelid in a model's textures: four in a row from "mario_eyeLid.0".
    constexpr u32 cEyeLidFrameNum = 4;

    /// @brief The first eyelid frame's texture index, given where "mario_eyeLid.0" is in a model of `textureCount`
    /// textures (-1: it has none), or -1 when the model does not hold the four: its eyelid keeps the model's own image.
    /// The models are data a mod may replace. N-3035: the console took texture 0 for a model without the frame (and the
    /// previous model's index for the bee's and the hopper's), and read the four frames past the model's textures.
    inline s32 eyeLidFramesAt(s32 found, u32 textureCount) {
        return found >= 0 && static_cast< u32 >(found) < textureCount && textureCount - static_cast< u32 >(found) >= cEyeLidFrameNum
                   ? found
                   : -1;
    }
}  // namespace MR
