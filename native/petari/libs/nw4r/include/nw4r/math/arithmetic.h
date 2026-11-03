#pragma once


namespace nw4r {
    namespace math {
        // fsel: ifPos where cond >= 0 (+0 and -0 included), else ifNeg (NaN included).
        inline f32 FSelect(f32 cond, f32 ifPos, f32 ifNeg) {
            return cond >= 0.0f ? ifPos : ifNeg;
        }
    };  // namespace math
};  // namespace nw4r
