#pragma once

#include <nebula_math.h>

void JMAEulerToQuat(s16, s16, s16, Quaternion*);

inline f32 JMAHermiteInterpolation(f32 p1, f32 p2, f32 p3, f32 p4, f32 p5, f32 p6,
                                   f32 p7) {
    const f32 ff31 = p1 - p2;
    const f32 t = ff31 / (p5 - p2);
    const f32 squared = t * t;
    const f32 twice = t + t;
    const f32 ff27 = squared - t;
    const f32 ff30 = p3 - p6;
    f32 ff26 = twice * ff27 - squared;
    f32 ff25 = p4 * ff27 + p4;
    ff26 = ff26 * ff30 + p3;
    ff25 = p7 * ff27 + ff25;
    ff25 = t * p4 - ff25;
    return ff26 - ff31 * ff25;
}

// The J3D files' last names for what nebula_math has.
inline void JMAMTXApplyScale(const Mtx src, Mtx dst, f32 x, f32 y, f32 z) {
    NebulaMtxApplyScale(src, dst, x, y, z);
}

namespace JMath {
    inline f32 fastReciprocal(f32 value) {
        return 1.0f / value;
    }

    template < typename T >
    inline T fastSqrt(T value) {
        return NebulaSqrt(value);
    }

    inline void copyFloats3(void* dest, const void* src) {
        memcpy(dest, src, 3 * sizeof(f32));
    }

    inline void copyFloats6(void* dest, const void* src) {
        memcpy(dest, src, 6 * sizeof(f32));
    }

    inline void copyFloats12(void* pDest, const void* pSrc) {
        memcpy(pDest, pSrc, 12 * sizeof(f32));
    }

    inline void copyFloats16(void* pDest, const void* pSrc) {
        memcpy(pDest, pSrc, 16 * sizeof(f32));
    }
};  // namespace JMath

template < typename T >
inline T JMAMax(T param_0, T param_1) {
    T ret;
    if (param_0 > param_1) {
        ret = param_0;
    } else {
        ret = param_1;
    }
    return ret;
}
