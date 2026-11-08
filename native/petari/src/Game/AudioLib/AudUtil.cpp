#include "Game/AudioLib/AudUtil.hpp"

namespace AudUtil {
    f32 linearMax(s32 value, s32 max, f32 end) {
        if (value <= 0.0f) {
            return 0.0f;
        }
        if (value >= max) {
            return end;
        }
        return end * value / max;
    }

    f32 linearMinMax(s32 value, s32 min, s32 max, f32 start, f32 end) {
        if (value <= min) {
            return start;
        }
        if (value >= max) {
            return end;
        }
        return start + (end - start) * (value - min) / (max - min);
    }
};  // namespace AudUtil
