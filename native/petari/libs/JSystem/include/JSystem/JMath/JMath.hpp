#pragma once

#include <revolution/mtx.h>
#include <revolution/types.h>

void JMAMTXApplyScale(const Mtx, Mtx, f32, f32, f32);
void JMAVECLerp(const Vec*, const Vec*, Vec*, f32);
void JMAVECScaleAdd(const Vec*, const Vec*, Vec*, f32);
void JMAQuatLerp(const Quaternion*, const Quaternion*, f32, Quaternion*);
void JMAEulerToQuat(s16, s16, s16, Quaternion*);

inline f32 JMAFastSqrt(__REGISTER const f32 input) {
#ifdef TARGET_PC
    // The console multiplies the input by frsqrte's estimate of its reciprocal square root; natively the root.
    return input > 0.0f ? __builtin_sqrtf(input) : input;
#elif defined(__MWERKS__)
    if (input > 0.0f) {
        __REGISTER f32 out;
        asm {
            frsqrte out, input
        }
        return out * input;
    } else {
        return input;
    }
#endif
}

inline f32 JMAHermiteInterpolation(__REGISTER f32 p1, __REGISTER f32 p2, __REGISTER f32 p3, __REGISTER f32 p4, __REGISTER f32 p5, __REGISTER f32 p6,
                                   __REGISTER f32 p7) {
#ifdef TARGET_PC
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
#elif defined(__MWERKS__)
    __REGISTER f32 ff25;
    __REGISTER f32 ff31;
    __REGISTER f32 ff30;
    __REGISTER f32 ff29;
    __REGISTER f32 ff28;
    __REGISTER f32 ff27;
    __REGISTER f32 ff26;
    // clang-format off
    asm {
        fsubs   ff31, p1, p2
        fsubs   ff30, p5, p2
        fdivs   ff29, ff31, ff30
        fmuls   ff28,ff29,ff29
        fadds   ff25,ff29,ff29
        fsubs   ff27,ff28,ff29
        fsubs   ff30, p3, p6
        fmsubs  ff26,ff25,ff27,ff28
        fmadds  ff25,p4,ff27,p4
        fmadds  ff26,ff26,ff30,p3
        fmadds  ff25,p7,ff27,ff25
        fmsubs  ff25,ff29,p4,ff25
        fnmsubs ff25,ff31,ff25,ff26

    }
    // clang-format on
    return ff25;
#endif
}

namespace JMath {
    inline f32 fastReciprocal(__REGISTER f32 value) {
#ifdef __MWERKS__
        asm {
            fres value, value
        }
        return value;
#else
        return 1.0f / value;
#endif
    }

    template < typename T >
    inline T fastSqrt(T value) {
        return JMAFastSqrt(value);
    }

    inline void gekko_ps_copy3(__REGISTER void* dest, __REGISTER const void* src) {
#ifdef TARGET_PC
        memcpy(dest, src, 3 * sizeof(f32));
#elif defined(__MWERKS__)
        __REGISTER f32 x, y;
        asm {
            psq_l x, 0(src), 0, 0
            lfs y, 8(src)
            psq_st x, 0(dest), 0, 0
            stfs y, 8(dest)
        }
#endif
    }

    inline void gekko_ps_copy6(__REGISTER void* dest, __REGISTER const void* src) {
#ifdef TARGET_PC
        memcpy(dest, src, 6 * sizeof(f32));
#elif defined(__MWERKS__)
        __REGISTER f32 x, y, z;
        asm {
            psq_l x, 0(src), 0, 0
            psq_l y, 8(src), 0, 0
            psq_l z, 16(src), 0, 0
            psq_st x, 0(dest), 0, 0
            psq_st y, 8(dest), 0, 0
            psq_st z, 16(dest), 0, 0
        }
#endif
    }

    inline void gekko_ps_copy12(__REGISTER void* pDest, __REGISTER const void* pSrc) {
#ifdef TARGET_PC
        memcpy(pDest, pSrc, 12 * sizeof(f32));
#elif defined(__MWERKS__)
        register f32 f_0, f_1, f_2, f_3, f_4, f_5;

        __asm {
                psq_l     f_0, 0(pSrc), 0, 0
                psq_l     f_1, 8(pSrc), 0, 0
                psq_l     f_2, 0x10(pSrc), 0, 0
                psq_l     f_3, 0x18(pSrc), 0, 0
                psq_l     f_4, 0x20(pSrc), 0, 0
                psq_l     f_5, 0x28(pSrc), 0, 0
                psq_st    f_0, 0(pDest), 0, 0
                psq_st    f_1, 8(pDest), 0, 0
                psq_st    f_2, 0x10(pDest), 0, 0
                psq_st    f_3, 0x18(pDest), 0, 0
                psq_st    f_4, 0x20(pDest), 0, 0
                psq_st    f_5, 0x28(pDest), 0, 0
        }
        ;
#endif
    }

    inline void gekko_ps_copy16(__REGISTER void* pDest, __REGISTER const void* pSrc) {
#ifdef TARGET_PC
        memcpy(pDest, pSrc, 16 * sizeof(f32));
#elif defined(__MWERKS__)
        register f32 f_0, f_1, f_2, f_3, f_4, f_5, f_6, f_7;

        __asm {
                psq_l     f_0, 0(pSrc), 0, 0
                psq_l     f_1, 8(pSrc), 0, 0
                psq_l     f_2, 0x10(pSrc), 0, 0
                psq_l     f_3, 0x18(pSrc), 0, 0
                psq_l     f_4, 0x20(pSrc), 0, 0
                psq_l     f_5, 0x28(pSrc), 0, 0
                psq_l     f_6, 0x30(pSrc), 0, 0
                psq_l     f_7, 0x38(pSrc), 0, 0
                psq_st    f_0, 0(pDest), 0, 0
                psq_st    f_1, 8(pDest), 0, 0
                psq_st    f_2, 0x10(pDest), 0, 0
                psq_st    f_3, 0x18(pDest), 0, 0
                psq_st    f_4, 0x20(pDest), 0, 0
                psq_st    f_5, 0x28(pDest), 0, 0
                psq_st    f_6, 0x30(pDest), 0, 0
                psq_st    f_7, 0x38(pDest), 0, 0
        }
        ;
#endif
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
