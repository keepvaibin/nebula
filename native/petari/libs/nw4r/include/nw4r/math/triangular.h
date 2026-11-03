#pragma once

#include "nw4r/math/arithmetic.h"
#include "nw4r/math/constant.h"
#include <NebulaGpu.h>
#include <nebula_math.h>

#define NW4R_MATH_DEG_TO_RAD(deg) ((deg) * 0.01745329252f)

namespace nw4r {
    namespace math {
        inline f32 SinDeg(f32 deg) {
            return __builtin_sinf(NW4R_MATH_DEG_TO_RAD(deg));
        }

        inline f32 CosDeg(f32 deg) {
            return __builtin_cosf(NW4R_MATH_DEG_TO_RAD(deg));
        }
    };  // namespace math
};  // namespace nw4r
