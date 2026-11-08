#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>

namespace AudUtil {
    // [0, max] to [0, end], clamped.
    f32 linearMax(s32 value, s32 max, f32 end);
    // [min, max] to [start, end], clamped.
    f32 linearMinMax(s32 value, s32 min, s32 max, f32 start, f32 end);
};  // namespace AudUtil
