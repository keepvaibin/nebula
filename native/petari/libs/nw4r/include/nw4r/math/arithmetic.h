#pragma once


namespace nw4r {
    namespace math {
        inline f32 U16ToF32(u16 x) {
            return static_cast< f32 >(x);
        }

        inline u16 F32ToU16(f32 x) {
            return nebula_f32_to_u16(x);
        }

        inline f32 FAbs(f32 x) {
            return __builtin_fabsf(x);
        }

        // fsel: ifPos where cond >= 0 (+0 and -0 included), else ifNeg (NaN included).
        inline f32 FSelect(f32 cond, f32 ifPos, f32 ifNeg) {
            return cond >= 0.0f ? ifPos : ifNeg;
        }
    };  // namespace math
};  // namespace nw4r
