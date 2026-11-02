#pragma once

#include <revolution/mtx.h>
#include <revolution/types.h>

void JMAMTXApplyScale(const Mtx, Mtx, f32, f32, f32);
void JMAVECLerp(const Vec*, const Vec*, Vec*, f32);
void JMAVECScaleAdd(const Vec*, const Vec*, Vec*, f32);
void JMAQuatLerp(const Quaternion*, const Quaternion*, f32, Quaternion*);
void JMAEulerToQuat(s16, s16, s16, Quaternion*);

inline f32 JMAFastSqrt(__REGISTER const f32 input) {
    // The console multiplies the input by frsqrte's estimate of its reciprocal square root; natively the root.
    return input > 0.0f ? __builtin_sqrtf(input) : input;
}

inline f32 JMAHermiteInterpolation(__REGISTER f32 p1, __REGISTER f32 p2, __REGISTER f32 p3, __REGISTER f32 p4, __REGISTER f32 p5, __REGISTER f32 p6,
                                   __REGISTER f32 p7) {
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

namespace JMath {
    inline f32 fastReciprocal(__REGISTER f32 value) {
        return 1.0f / value;
    }

    template < typename T >
    inline T fastSqrt(T value) {
        return JMAFastSqrt(value);
    }

    inline void gekko_ps_copy3(__REGISTER void* dest, __REGISTER const void* src) {
        memcpy(dest, src, 3 * sizeof(f32));
    }

    inline void gekko_ps_copy6(__REGISTER void* dest, __REGISTER const void* src) {
        memcpy(dest, src, 6 * sizeof(f32));
    }

    inline void gekko_ps_copy12(__REGISTER void* pDest, __REGISTER const void* pSrc) {
        memcpy(pDest, pSrc, 12 * sizeof(f32));
    }

    inline void gekko_ps_copy16(__REGISTER void* pDest, __REGISTER const void* pSrc) {
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
