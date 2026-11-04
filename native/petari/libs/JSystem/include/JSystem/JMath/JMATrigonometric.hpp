#pragma once

#include <math_types.hpp>
#include <nebula_math.h>

namespace JMath {
    template < typename T >
    struct TAngleConstant_;

    template <>
    struct TAngleConstant_< f32 > {
        static f32 RADIAN_DEG090() {
            return 1.5707964f;
        }

        static f32 RADIAN_DEG180() {
            return 3.1415927f;
        }

        static f32 RADIAN_DEG360() {
            return 6.2831855f;
        }

        static f32 RADIAN_TO_DEGREE_FACTOR() {
            return 180.0f / RADIAN_DEG180();
        }
    };
};  // namespace JMath

// An angle as a short: a whole turn in 65536.
inline void JMASSinCos(s16 v, f32* pSin, f32* pCos) {
    NebulaSinCos(v * 0.000095873799f, pSin, pCos);
}

inline f32 JMASCos(s16 v) {
    return NebulaCos(v * 0.000095873799f);
}

inline f32 JMASSin(s16 v) {
    return NebulaSin(v * 0.000095873799f);
}

inline f32 JMACosShort(s16 v) {
    return JMASCos(v);
}

inline f32 JMASinShort(s16 v) {
    return JMASSin(v);
}

inline f32 JMAAcosRadian(f32 v) {
    return NebulaAcos(v);
}

inline f32 JMAAsinRadian(f32 v) {
    return NebulaAsin(v);
}

inline f32 JMACosDegree(f32 angle) {
    return NebulaCos(angle * 0.017453292f);
}

inline f32 JMACosRadian(f32 angle) {
    return NebulaCos(angle);
}

inline f32 JMACosLap(f32 angle) {
    return NebulaCos(angle * 6.2831855f);
}

inline f32 JMASinDegree(f32 angle) {
    return NebulaSin(angle * 0.017453292f);
}

inline f32 JMASinRadian(f32 angle) {
    return NebulaSin(angle);
}

inline f32 JMASinLap(f32 angle) {
    return NebulaSin(angle * 6.2831855f);
}

inline f32 JMAATan2(f32 x, f32 y) {
    return NebulaAtan2(x, y);
}

namespace JMath {
    inline f32 acosDegree(f32 x) {
        return NebulaAcos(x) * TAngleConstant_< f32 >::RADIAN_TO_DEGREE_FACTOR();
    }
};  // namespace JMath
