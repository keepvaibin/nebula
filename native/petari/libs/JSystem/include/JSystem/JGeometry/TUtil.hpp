#pragma once

#include "JSystem/JMath/JMATrigonometric.hpp"
#include <cmath>
#include <nebula_math.h>

namespace JGeometry {
    template < typename T >
    class TUtil {
    public:
        static f32 invert(f32 value) {
            return 1.0f / value;
        }

        static int epsilonEquals(T a1, T a2, T a3) {
            int ret = 0;

            a1 -= a2;

            if (-a3 <= a1 && a1 <= a3) {
                ret = 1;
            }

            return ret;
        }

        static T sqrt(T val) {
            return NebulaSqrt(val);
        }

        static f32 sqrtInline(f32 val) {
            return NebulaSqrt(val);
        }

        static f32 asin(f32 val) {
            if (val >= 1.0f) {
                return 1.5707964f;
            }

            if (val <= -1.0f) {
                return -1.5707964f;
            }

            return JMAAsinRadian(val);
        }

        static f32 acos(f32 val) {
            return JMAAcosRadian(val);
        }

        static f32 PI() {
            return 3.1415927f;
        }

        static T clamp(T val, T min, T max) {
            if (val < min) {
                return min;
            }

            if (val > max) {
                return max;
            }

            return val;
        }

        static f32 inv_sqrt(f32 val) {
            if (val <= 0.0f) {
                return val;
            }
            return 1.0f / sqrtf(val);
        }

        static inline f32 epsilon() {
            return 32.0f * FLT_EPSILON;
        }
    };
};  // namespace JGeometry
