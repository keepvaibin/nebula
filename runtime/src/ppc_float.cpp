#include "galaxy/ppc_float.h"
#include "galaxy/ppc_paired_float.h"

#include "galaxy/native_api.h"

#if defined(_M_X64) || defined(__SSE2__)
#include <emmintrin.h>
#endif

extern "C" {
#include "platform.h"
#include "softfloat.h"
}

namespace galaxy {
namespace {

using namespace float_native_detail;

struct SoftFloatResult32 {
    std::uint32_t bits{};
    std::uint8_t exceptions{};
};

struct SoftFloatResult64 {
    std::uint64_t bits{};
    std::uint8_t exceptions{};
};

std::uint_fast8_t softfloat_rounding_mode(std::uint32_t fpscr) {
    switch (fpscr & 3u) {
        case 0:
            return softfloat_round_near_even;
        case 1:
            return softfloat_round_minMag;
        case 2:
            return softfloat_round_max;
        case 3:
            return softfloat_round_min;
        default:
            return softfloat_round_near_even;
    }
}

class SoftFloatState {
public:
    explicit SoftFloatState(std::uint_fast8_t rounding)
        : saved_rounding_(softfloat_roundingMode),
          saved_tininess_(softfloat_detectTininess),
          saved_exceptions_(softfloat_exceptionFlags) {
        softfloat_roundingMode = rounding;
        softfloat_detectTininess = softfloat_tininess_afterRounding;
        softfloat_exceptionFlags = 0;
    }

    SoftFloatState(const SoftFloatState&) = delete;
    SoftFloatState& operator=(const SoftFloatState&) = delete;

    ~SoftFloatState() {
        softfloat_roundingMode = saved_rounding_;
        softfloat_detectTininess = saved_tininess_;
        softfloat_exceptionFlags = saved_exceptions_;
    }

    std::uint8_t exceptions() const {
        return static_cast<std::uint8_t>(softfloat_exceptionFlags);
    }

private:
    std::uint_fast8_t saved_rounding_;
    std::uint_fast8_t saved_tininess_;
    std::uint_fast8_t saved_exceptions_;
};

float32_t run_f32_operation(
    PpcFloatBinaryOperation operation,
    float32_t left,
    float32_t right) {
    switch (operation) {
    case PpcFloatBinaryOperation::Add:
        return f32_add(left, right);
    case PpcFloatBinaryOperation::Subtract:
        return f32_sub(left, right);
    case PpcFloatBinaryOperation::Multiply:
        return f32_mul(left, right);
    case PpcFloatBinaryOperation::Divide:
        return f32_div(left, right);
    }
    std::abort();
}

float64_t run_f64_operation(
    PpcFloatBinaryOperation operation,
    float64_t left,
    float64_t right) {
    switch (operation) {
    case PpcFloatBinaryOperation::Add:
        return f64_add(left, right);
    case PpcFloatBinaryOperation::Subtract:
        return f64_sub(left, right);
    case PpcFloatBinaryOperation::Multiply:
        return f64_mul(left, right);
    case PpcFloatBinaryOperation::Divide:
        return f64_div(left, right);
    }
    std::abort();
}

SoftFloatResult32 calculate_f32(
    PpcFloatBinaryOperation operation,
    std::uint32_t left,
    std::uint32_t right,
    std::uint_fast8_t rounding) {
    SoftFloatState state(rounding);
    const float32_t value =
        run_f32_operation(operation, float32_t{left}, float32_t{right});
    return SoftFloatResult32{value.v, state.exceptions()};
}

SoftFloatResult64 calculate_f64(
    PpcFloatBinaryOperation operation,
    std::uint64_t left,
    std::uint64_t right,
    std::uint_fast8_t rounding) {
    SoftFloatState state(rounding);
    const float64_t value =
        run_f64_operation(operation, float64_t{left}, float64_t{right});
    return SoftFloatResult64{value.v, state.exceptions()};
}

SoftFloatResult32 calculate_f32_sqrt(
    std::uint32_t bits,
    std::uint_fast8_t rounding) {
    SoftFloatState state(rounding);
    const float32_t value = f32_sqrt(float32_t{bits});
    return SoftFloatResult32{value.v, state.exceptions()};
}

SoftFloatResult64 calculate_f64_sqrt(
    std::uint64_t bits,
    std::uint_fast8_t rounding) {
    SoftFloatState state(rounding);
    const float64_t value = f64_sqrt(float64_t{bits});
    return SoftFloatResult64{value.v, state.exceptions()};
}

bool ternary_subtracts(PpcFloatTernaryOperation operation) {
    return operation == PpcFloatTernaryOperation::MultiplySubtract ||
           operation == PpcFloatTernaryOperation::NegativeMultiplySubtract;
}

bool ternary_negates(PpcFloatTernaryOperation operation) {
    return operation == PpcFloatTernaryOperation::NegativeMultiplyAdd ||
           operation == PpcFloatTernaryOperation::NegativeMultiplySubtract;
}

SoftFloatResult32 calculate_f32_ternary(
    PpcFloatTernaryOperation operation,
    std::uint32_t multiplicand,
    std::uint32_t multiplier,
    std::uint32_t addend,
    std::uint_fast8_t rounding) {
    SoftFloatState state(rounding);
    if (ternary_subtracts(operation)) {
        addend ^= 0x80000000u;
    }
    const float32_t value = f32_mulAdd(
        float32_t{multiplicand}, float32_t{multiplier}, float32_t{addend});
    return SoftFloatResult32{value.v, state.exceptions()};
}

SoftFloatResult64 calculate_f64_ternary(
    PpcFloatTernaryOperation operation,
    std::uint64_t multiplicand,
    std::uint64_t multiplier,
    std::uint64_t addend,
    std::uint_fast8_t rounding) {
    SoftFloatState state(rounding);
    if (ternary_subtracts(operation)) {
        addend ^= 0x8000000000000000ull;
    }
    const float64_t value = f64_mulAdd(
        float64_t{multiplicand}, float64_t{multiplier}, float64_t{addend});
    return SoftFloatResult64{value.v, state.exceptions()};
}

SoftFloatResult32 calculate_f64_to_f32(
    std::uint64_t bits,
    std::uint_fast8_t rounding) {
    SoftFloatState state(rounding);
    const float32_t value = f64_to_f32(float64_t{bits});
    return SoftFloatResult32{value.v, state.exceptions()};
}

struct SoftFloatIntegerResult {
    std::int32_t value{};
    std::uint8_t exceptions{};
};

SoftFloatIntegerResult calculate_f64_to_i32_round_zero(std::uint64_t bits) {
    SoftFloatState state(softfloat_round_minMag);
    const std::int_fast32_t value =
        f64_to_i32_r_minMag(float64_t{bits}, true);
    return SoftFloatIntegerResult{
        static_cast<std::int32_t>(value),
        state.exceptions()};
}

bool f32_is_nan(std::uint32_t bits) {
    return (bits & 0x7F800000u) == 0x7F800000u &&
           (bits & 0x007FFFFFu) != 0;
}

bool f32_is_signaling_nan(std::uint32_t bits) {
    return f32_is_nan(bits) && (bits & 0x00400000u) == 0;
}

bool f32_is_infinity(std::uint32_t bits) {
    return (bits & 0x7FFFFFFFu) == 0x7F800000u;
}

bool f32_is_zero(std::uint32_t bits) {
    return (bits & 0x7FFFFFFFu) == 0;
}


bool f64_is_finite(std::uint64_t bits) {
    return (bits & 0x7FF0000000000000ull) != 0x7FF0000000000000ull;
}


bool f64_is_zero_or_normal(std::uint64_t bits) {
    const std::uint64_t exponent = bits & 0x7FF0000000000000ull;
    return exponent != 0 || (bits & 0x000FFFFFFFFFFFFFull) == 0;
}


bool f64_is_normal_or_zero_result(std::uint64_t bits) {
    const std::uint64_t exponent = bits & 0x7FF0000000000000ull;
    return (exponent != 0 && exponent != 0x7FF0000000000000ull) ||
           (bits & 0x7FFFFFFFFFFFFFFFull) == 0;
}



std::int32_t f32_unbiased_exponent(std::uint32_t bits) {
    return static_cast<std::int32_t>((bits >> 23) & 0xFFu) - 127;
}

bool f32_fma_exact_in_double(
    std::uint32_t multiplicand,
    std::uint32_t multiplier,
    std::uint32_t addend) {
    if ((multiplicand & 0x7FFFFFFFu) == 0 ||
        (multiplier & 0x7FFFFFFFu) == 0 ||
        (addend & 0x7FFFFFFFu) == 0) {
        return true;
    }

    const std::int32_t product_exponent =
        f32_unbiased_exponent(multiplicand) + f32_unbiased_exponent(multiplier);
    const std::int32_t addend_exponent = f32_unbiased_exponent(addend);
    const std::int32_t distance = product_exponent - addend_exponent;
    // A binary32 product has at most 48 significant bits and can carry to
    // product_exponent + 1. Reserve one more bit for the addition's carry:
    // an aligned operand span of at most 52 bits makes the sum exactly
    // representable in binary64. Distances 28 and 29 do not prove this.
    return distance >= 0 ? distance <= 27 : distance >= -5;
}

bool f64_is_infinity_bits(std::uint64_t bits) {
    return (bits & 0x7FFFFFFFFFFFFFFFull) == 0x7FF0000000000000ull;
}

bool f64_is_zero_bits(std::uint64_t bits) {
    return (bits & 0x7FFFFFFFFFFFFFFFull) == 0;
}

bool f64_is_power_of_two(std::uint64_t bits) {
    return ((bits & 0x7FF0000000000000ull) != 0) &&
           ((bits & 0x7FF0000000000000ull) != 0x7FF0000000000000ull) &&
           ((bits & 0x000FFFFFFFFFFFFFull) == 0);
}

bool try_narrow_widened_f32(std::uint64_t bits, std::uint32_t& output) {
    // Exact normal/signed-zero values need no round trip through the general
    // widener. The existing finite-exponent/low-bit proof rejects every other
    // encoding; retain the original subnormal/infinity/payload checks below.
    if (try_narrow_normal_or_zero_widened_f32(bits, output)) {
        return true;
    }
    const std::uint32_t exponent =
        static_cast<std::uint32_t>((bits >> 52) & 0x7FFu);
    const std::uint64_t magnitude = bits & 0x7FFFFFFFFFFFFFFFull;
    if (exponent > 896u || magnitude == 0) {
        output = static_cast<std::uint32_t>((bits >> 32) & 0xC0000000ull) |
                 static_cast<std::uint32_t>((bits >> 29) & 0x3FFFFFFFull);
    } else if (exponent >= 874u) {
        const std::uint32_t shift = 926u - exponent;
        const std::uint64_t significand =
            0x0010000000000000ull | (bits & 0x000FFFFFFFFFFFFFull);
        output = static_cast<std::uint32_t>(bits >> 32) & 0x80000000u;
        output |= static_cast<std::uint32_t>(significand >> shift) & 0x007FFFFFu;
    } else {
        return false;
    }
    return widen_f32_bits(output) == bits;
}

std::uint32_t flush_f32_input(std::uint32_t bits, std::uint32_t fpscr) {
    if ((fpscr & kNi) != 0 && (bits & 0x7F800000u) == 0 &&
        (bits & 0x007FFFFFu) != 0) {
        return bits & 0x80000000u;
    }
    return bits;
}

std::uint64_t flush_f64_input(std::uint64_t bits, std::uint32_t fpscr) {
    if ((fpscr & kNi) != 0 && (bits & 0x7FF0000000000000ull) == 0 &&
        (bits & 0x000FFFFFFFFFFFFFull) != 0) {
        return bits & 0x8000000000000000ull;
    }
    return bits;
}

std::uint32_t invalid_detail_f32(
    PpcFloatBinaryOperation operation,
    std::uint32_t left,
    std::uint32_t right) {
    if (f32_is_signaling_nan(left) || f32_is_signaling_nan(right)) {
        return kVxSnan;
    }
    if (f32_is_nan(left) || f32_is_nan(right)) {
        return 0;
    }
    const bool left_infinity = f32_is_infinity(left);
    const bool right_infinity = f32_is_infinity(right);
    const bool left_zero = f32_is_zero(left);
    const bool right_zero = f32_is_zero(right);
    switch (operation) {
    case PpcFloatBinaryOperation::Add:
        return left_infinity && right_infinity && ((left ^ right) >> 31) != 0
                   ? kVxIsi
                   : 0;
    case PpcFloatBinaryOperation::Subtract:
        return left_infinity && right_infinity && ((left ^ right) >> 31) == 0
                   ? kVxIsi
                   : 0;
    case PpcFloatBinaryOperation::Multiply:
        return (left_infinity && right_zero) || (right_infinity && left_zero)
                   ? kVxImz
                   : 0;
    case PpcFloatBinaryOperation::Divide:
        if (left_zero && right_zero) {
            return kVxZdz;
        }
        return left_infinity && right_infinity ? kVxIdi : 0;
    }
    return 0;
}

std::uint32_t invalid_detail_f64(
    PpcFloatBinaryOperation operation,
    std::uint64_t left,
    std::uint64_t right) {
    if (f64_is_signaling_nan(left) || f64_is_signaling_nan(right)) {
        return kVxSnan;
    }
    if (f64_is_nan(left) || f64_is_nan(right)) {
        return 0;
    }
    const bool left_infinity = f64_is_infinity_bits(left);
    const bool right_infinity = f64_is_infinity_bits(right);
    const bool left_zero = f64_is_zero_bits(left);
    const bool right_zero = f64_is_zero_bits(right);
    switch (operation) {
    case PpcFloatBinaryOperation::Add:
        return left_infinity && right_infinity && ((left ^ right) >> 63) != 0
                   ? kVxIsi
                   : 0;
    case PpcFloatBinaryOperation::Subtract:
        return left_infinity && right_infinity && ((left ^ right) >> 63) == 0
                   ? kVxIsi
                   : 0;
    case PpcFloatBinaryOperation::Multiply:
        return (left_infinity && right_zero) || (right_infinity && left_zero)
                   ? kVxImz
                   : 0;
    case PpcFloatBinaryOperation::Divide:
        if (left_zero && right_zero) {
            return kVxZdz;
        }
        return left_infinity && right_infinity ? kVxIdi : 0;
    }
    return 0;
}

std::uint32_t invalid_detail_f32_ternary(
    PpcFloatTernaryOperation operation,
    std::uint32_t multiplicand,
    std::uint32_t multiplier,
    std::uint32_t addend) {
    std::uint32_t result = 0;
    if (f32_is_signaling_nan(multiplicand) ||
        f32_is_signaling_nan(multiplier) ||
        f32_is_signaling_nan(addend)) {
        result |= kVxSnan;
    }
    const bool product_invalid =
        (f32_is_infinity(multiplicand) && f32_is_zero(multiplier)) ||
        (f32_is_zero(multiplicand) && f32_is_infinity(multiplier));
    if (product_invalid) {
        result |= kVxImz;
    }
    if (!product_invalid &&
        (f32_is_infinity(multiplicand) || f32_is_infinity(multiplier)) &&
        f32_is_infinity(addend)) {
        const std::uint32_t product_sign =
            (multiplicand ^ multiplier) & 0x80000000u;
        const std::uint32_t addend_sign =
            (addend ^ (ternary_subtracts(operation) ? 0x80000000u : 0u)) &
            0x80000000u;
        if (product_sign != addend_sign) {
            result |= kVxIsi;
        }
    }
    return result;
}

std::uint32_t invalid_detail_f64_ternary(
    PpcFloatTernaryOperation operation,
    std::uint64_t multiplicand,
    std::uint64_t multiplier,
    std::uint64_t addend) {
    std::uint32_t result = 0;
    if (f64_is_signaling_nan(multiplicand) ||
        f64_is_signaling_nan(multiplier) ||
        f64_is_signaling_nan(addend)) {
        result |= kVxSnan;
    }
    const bool product_invalid =
        (f64_is_infinity_bits(multiplicand) && f64_is_zero_bits(multiplier)) ||
        (f64_is_zero_bits(multiplicand) && f64_is_infinity_bits(multiplier));
    if (product_invalid) {
        result |= kVxImz;
    }
    if (!product_invalid &&
        (f64_is_infinity_bits(multiplicand) ||
         f64_is_infinity_bits(multiplier)) &&
        f64_is_infinity_bits(addend)) {
        const std::uint64_t product_sign =
            (multiplicand ^ multiplier) & 0x8000000000000000ull;
        const std::uint64_t addend_sign =
            (addend ^
             (ternary_subtracts(operation) ? 0x8000000000000000ull : 0ull)) &
            0x8000000000000000ull;
        if (product_sign != addend_sign) {
            result |= kVxIsi;
        }
    }
    return result;
}

std::uint32_t map_softfloat_exceptions(std::uint8_t exceptions) {
    std::uint32_t result = 0;
    if ((exceptions & softfloat_flag_overflow) != 0) {
        result |= kOx;
    }
    if ((exceptions & softfloat_flag_underflow) != 0) {
        result |= kUx;
    }
    if ((exceptions & softfloat_flag_infinite) != 0) {
        result |= kZx;
    }
    if ((exceptions & softfloat_flag_inexact) != 0) {
        result |= kXx;
    }
    return result;
}

std::uint64_t widen_f32(std::uint32_t bits) {
    return widen_f32_bits(bits);
}


std::uint32_t classify_f64(std::uint64_t bits) {
    const bool negative = (bits >> 63) != 0;
    const std::uint32_t exponent =
        static_cast<std::uint32_t>((bits >> 52) & 0x7FFu);
    const std::uint64_t fraction = bits & 0x000FFFFFFFFFFFFFull;
    if (exponent == 0x7FFu) {
        return fraction != 0 ? 0x00011000u
                             : (negative ? 0x00009000u : 0x00005000u);
    }
    if (exponent == 0) {
        if (fraction == 0) {
            return negative ? 0x00012000u : 0x00002000u;
        }
        return negative ? 0x00018000u : 0x00014000u;
    }
    return negative ? 0x00008000u : 0x00004000u;
}

std::uint32_t truncate_f32_estimate(std::uint32_t bits) {
    const std::uint32_t exponent = bits & 0x7F800000u;
    if (exponent == 0 || exponent == 0x7F800000u) {
        return bits;
    }
    return bits & ~0x00000FFFu;
}

std::uint64_t truncate_f64_estimate(std::uint64_t bits) {
    const std::uint64_t exponent = bits & 0x7FF0000000000000ull;
    if (exponent == 0 || exponent == 0x7FF0000000000000ull) {
        return bits;
    }
    // Keep the existing twelve-significant-bit approximation policy in the
    // result's own format, retaining its binary64 exponent range.
    return bits & ~((1ull << 41u) - 1ull);
}




// Keep the guarded native path inside each public arithmetic caller:
// outlining adds an avoidable result-object round trip to every operation,
// including the common multiply and nearby-add cases.

GALAXY_ALWAYS_INLINE bool native_f32_paired_binary_fast(
    PpcFloatBinaryOperation operation,
    std::uint32_t left_ps0,
    std::uint32_t left_ps1,
    std::uint32_t right_ps0,
    std::uint32_t right_ps1,
    std::uint32_t fpscr,
    PpcFloatResult& lane0,
    PpcFloatResult& lane1) {
#if defined(_M_X64) || defined(__SSE2__)
    // This shares the scalar helper's nearest-even, finite normal/zero
    // contract. NI inputs have already been flushed. Every binary32 product
    // has at most 48 significant bits; nearby add/subtract uses the existing
    // exponent-distance proof. The binary64 lanes therefore retain an exact
    // mathematical result for each lane's FI/FR calculation.
    if ((fpscr & 3u) != 0u || operation == PpcFloatBinaryOperation::Divide ||
        !native_f32_binary_inputs(left_ps0, right_ps0) ||
        !native_f32_binary_inputs(left_ps1, right_ps1)) {
        return false;
    }
    if ((operation == PpcFloatBinaryOperation::Add ||
         operation == PpcFloatBinaryOperation::Subtract) &&
        (!f32_add_sub_exact_in_double(left_ps0, right_ps0) ||
         !f32_add_sub_exact_in_double(left_ps1, right_ps1))) {
        // A distant add/subtract still uses the scalar helper's exact
        // dominant-operand status derivation, through the fallback below.
        return false;
    }
    const __m128 left = _mm_castsi128_ps(_mm_set_epi32(
        0, 0, std::bit_cast<std::int32_t>(left_ps1),
        std::bit_cast<std::int32_t>(left_ps0)));
    const __m128 right = _mm_castsi128_ps(_mm_set_epi32(
        0, 0, std::bit_cast<std::int32_t>(right_ps1),
        std::bit_cast<std::int32_t>(right_ps0)));
    const __m128d left_exact = _mm_cvtps_pd(left);
    const __m128d right_exact = _mm_cvtps_pd(right);
    __m128 rounded{};
    __m128d exact{};
    // Packed binary32 operations follow WiiCompiled's Ppc{Add,Sub,Mul}PairInline
    // in runtime/include/isa/ppc_isa_float.h, revision
    // 9b7b9913e4ab60b9c57fa4be56b8da308e76d06e (GPL-2.0-or-later;
    // Copyright 2018 Dolphin Emulator Project). Galaxy's eligibility proofs,
    // exact binary64 status lanes, register layout, and commit remain local.
    switch (operation) {
    case PpcFloatBinaryOperation::Add:
        rounded = _mm_add_ps(left, right);
        exact = _mm_add_pd(left_exact, right_exact);
        break;
    case PpcFloatBinaryOperation::Subtract:
        rounded = _mm_sub_ps(left, right);
        exact = _mm_sub_pd(left_exact, right_exact);
        break;
    case PpcFloatBinaryOperation::Multiply:
        rounded = _mm_mul_ps(left, right);
        exact = _mm_mul_pd(left_exact, right_exact);
        break;
    case PpcFloatBinaryOperation::Divide:
        return false;
    }
    const __m128i rounded_bits = _mm_castps_si128(rounded);
    const std::uint32_t bits0 = std::bit_cast<std::uint32_t>(
        _mm_cvtsi128_si32(rounded_bits));
    const std::uint32_t bits1 = std::bit_cast<std::uint32_t>(
        _mm_cvtsi128_si32(_mm_srli_si128(rounded_bits, 4)));
    if (!native_f32_result_is_eligible(bits0) ||
        !native_f32_result_is_eligible(bits1)) {
        return false;
    }
    double exact_lanes[2]{};
    _mm_storeu_pd(exact_lanes, exact);
    lane0 = native_f32_normal_or_zero_result(bits0, exact_lanes[0]);
    lane1 = native_f32_normal_or_zero_result(bits1, exact_lanes[1]);
    return true;
#else
    return native_f32_binary_fast(operation, left_ps0, right_ps0, fpscr, lane0) &&
           native_f32_binary_fast(operation, left_ps1, right_ps1, fpscr, lane1);
#endif
}

PpcFloatResult native_f64_exact_result(std::uint64_t bits) {
    return PpcFloatResult{bits, 0, classify_f64(bits), false, false};
}

bool native_f64_binary_fast(
    PpcFloatBinaryOperation operation,
    std::uint64_t left,
    std::uint64_t right,
    std::uint32_t fpscr,
    PpcFloatResult& result) {
    if ((fpscr & 0x7u) != 0 || operation == PpcFloatBinaryOperation::Divide ||
        !f64_is_finite(left) || !f64_is_finite(right) ||
        !f64_is_zero_or_normal(left) || !f64_is_zero_or_normal(right)) {
        return false;
    }

    const double left_value = std::bit_cast<double>(left);
    const double right_value = std::bit_cast<double>(right);
    double rounded = 0.0;
    bool exact = false;
    switch (operation) {
    case PpcFloatBinaryOperation::Add:
        exact = f64_is_zero_bits(left) || f64_is_zero_bits(right);
        rounded = left_value + right_value;
        break;
    case PpcFloatBinaryOperation::Subtract:
        exact = f64_is_zero_bits(left) || f64_is_zero_bits(right) ||
                left == right;
        rounded = left_value - right_value;
        break;
    case PpcFloatBinaryOperation::Multiply:
        exact = f64_is_zero_bits(left) || f64_is_zero_bits(right) ||
                f64_is_power_of_two(left) || f64_is_power_of_two(right);
        rounded = left_value * right_value;
        break;
    case PpcFloatBinaryOperation::Divide:
        return false;
    }
    const std::uint64_t rounded_bits = std::bit_cast<std::uint64_t>(rounded);
    if (!exact || !f64_is_normal_or_zero_result(rounded_bits)) {
        return false;
    }
    // Scaling by a power of two is exact only while the result remains
    // representable. A nonzero product rounded to zero or up to minimum
    // normal must retain the software path's underflow, inexact, and
    // rounding status.
    if (operation == PpcFloatBinaryOperation::Multiply &&
        (rounded_bits & 0x7FFFFFFFFFFFFFFFull) <= 0x0010000000000000ull &&
        !f64_is_zero_bits(left) && !f64_is_zero_bits(right)) {
        return false;
    }
    result = native_f64_exact_result(rounded_bits);
    return true;
}

GALAXY_ALWAYS_INLINE bool native_f32_ternary_fast(
    PpcFloatTernaryOperation operation,
    std::uint32_t multiplicand,
    std::uint32_t multiplier,
    std::uint32_t addend,
    std::uint32_t fpscr,
    PpcFloatResult& result) {
    if ((fpscr & 0x3u) != 0 || !f32_is_finite(multiplicand) ||
        !f32_is_finite(multiplier) || !f32_is_finite(addend) ||
        !f32_is_zero_or_normal(multiplicand) ||
        !f32_is_zero_or_normal(multiplier) ||
        !f32_is_zero_or_normal(addend) ||
        !f32_fma_exact_in_double(multiplicand, multiplier, addend)) {
        return false;
    }

    const float multiplicand_value = std::bit_cast<float>(multiplicand);
    const float multiplier_value = std::bit_cast<float>(multiplier);
    const float addend_value = std::bit_cast<float>(addend);
    const float adjusted_addend =
        ternary_subtracts(operation) ? -addend_value : addend_value;
    const double exact =
        static_cast<double>(multiplicand_value) *
            static_cast<double>(multiplier_value) +
        static_cast<double>(adjusted_addend);
    // The guard proves that `exact` is the mathematical fused result, with
    // no intermediate rounding. Round it once to binary32. This avoids a
    // second fused operation (and its CRT call on the SSE2 baseline).
    const float rounded = static_cast<float>(exact);
    std::uint32_t rounded_bits = std::bit_cast<std::uint32_t>(rounded);
    // Retain the software tininess decision when rounding reaches minimum
    // normal, just as for the binary arithmetic fast path.
    if (!f32_is_normal_or_zero_result(rounded_bits) ||
        (rounded_bits & 0x7FFFFFFFu) == 0x00800000u) {
        return false;
    }

    result = native_f32_normal_or_zero_result(rounded_bits, exact);
    if (ternary_negates(operation)) {
        // Eligibility already proves a normal or signed-zero result. Negation
        // changes only its sign: the widened encoding keeps its magnitude,
        // while FPRF swaps +/-normal or +/-zero. FI/FR describe the original
        // rounding and remain unchanged, just as in the software path.
        result.bits ^= 0x8000000000000000ull;
        result.fprf ^= (result.fprf & 0x00002000u) != 0u
            ? 0x00010000u : 0x0000C000u;
    }
    return true;
}

GALAXY_ALWAYS_INLINE bool native_f32_paired_ternary_fast(
    PpcFloatTernaryOperation operation,
    std::uint32_t multiplicand_ps0,
    std::uint32_t multiplicand_ps1,
    std::uint32_t multiplier_ps0,
    std::uint32_t multiplier_ps1,
    std::uint32_t addend_ps0,
    std::uint32_t addend_ps1,
    std::uint32_t fpscr,
    PpcFloatResult& lane0,
    PpcFloatResult& lane1) {
#if defined(_M_X64) || defined(__SSE2__)
    if ((fpscr & 3u) != 0u ||
        !native_f32_binary_inputs(multiplicand_ps0, multiplier_ps0) ||
        !native_f32_binary_inputs(multiplicand_ps1, multiplier_ps1) ||
        !native_f32_binary_inputs(addend_ps0, addend_ps1) ||
        !f32_fma_exact_in_double(multiplicand_ps0, multiplier_ps0, addend_ps0) ||
        !f32_fma_exact_in_double(multiplicand_ps1, multiplier_ps1, addend_ps1)) {
        return false;
    }
    const auto pair = [](std::uint32_t lower, std::uint32_t upper) {
        return _mm_cvtps_pd(_mm_castsi128_ps(_mm_set_epi32(
            0, 0, std::bit_cast<std::int32_t>(upper),
            std::bit_cast<std::int32_t>(lower))));
    };
    const __m128d product = _mm_mul_pd(pair(multiplicand_ps0, multiplicand_ps1),
        pair(multiplier_ps0, multiplier_ps1));
    const __m128d addend = pair(addend_ps0, addend_ps1);
    // Each product and aligned sum is proved exact in binary64, including
    // signed zero. These separate operations introduce no rounding error;
    // only the binary32 conversion rounds the mathematical fused result.
    const __m128d exact = ternary_subtracts(operation)
        ? _mm_sub_pd(product, addend) : _mm_add_pd(product, addend);
    const __m128i rounded = _mm_castps_si128(_mm_cvtpd_ps(exact));
    const auto bits0 = std::bit_cast<std::uint32_t>(_mm_cvtsi128_si32(rounded));
    const auto bits1 = std::bit_cast<std::uint32_t>(
        _mm_cvtsi128_si32(_mm_srli_si128(rounded, 4)));
    if (!native_f32_result_is_eligible(bits0) ||
        !native_f32_result_is_eligible(bits1)) {
        return false;
    }
    double exact_lanes[2]{};
    _mm_storeu_pd(exact_lanes, exact);
    lane0 = native_f32_normal_or_zero_result(bits0, exact_lanes[0]);
    lane1 = native_f32_normal_or_zero_result(bits1, exact_lanes[1]);
    if (ternary_negates(operation)) {
        for (auto* lane : {&lane0, &lane1}) {
            lane->bits ^= 0x8000000000000000ull;
            lane->fprf ^= (lane->fprf & 0x00002000u) != 0u
                ? 0x00010000u : 0x0000C000u;
        }
    }
    return true;
#else
    return native_f32_ternary_fast(operation, multiplicand_ps0, multiplier_ps0,
        addend_ps0, fpscr, lane0) &&
        native_f32_ternary_fast(operation, multiplicand_ps1, multiplier_ps1,
            addend_ps1, fpscr, lane1);
#endif
}

bool native_f64_ternary_fast(
    PpcFloatTernaryOperation operation,
    std::uint64_t multiplicand,
    std::uint64_t multiplier,
    std::uint64_t addend,
    std::uint32_t fpscr,
    PpcFloatResult& result) {
    if ((fpscr & 0x7u) != 0 || !f64_is_finite(multiplicand) ||
        !f64_is_finite(multiplier) || !f64_is_finite(addend) ||
        !f64_is_zero_or_normal(multiplicand) ||
        !f64_is_zero_or_normal(multiplier) ||
        !f64_is_zero_or_normal(addend)) {
        return false;
    }

    const bool exact_product =
        f64_is_zero_bits(multiplicand) || f64_is_zero_bits(multiplier) ||
        f64_is_power_of_two(multiplicand) || f64_is_power_of_two(multiplier);
    const bool exact_sum = exact_product && f64_is_zero_bits(addend);
    const bool exact_passthrough =
        (f64_is_zero_bits(multiplicand) || f64_is_zero_bits(multiplier)) &&
        (operation == PpcFloatTernaryOperation::MultiplyAdd ||
         operation == PpcFloatTernaryOperation::MultiplySubtract);
    if (!exact_sum && !exact_passthrough) {
        return false;
    }

    const double multiplicand_value = std::bit_cast<double>(multiplicand);
    const double multiplier_value = std::bit_cast<double>(multiplier);
    const double addend_value = std::bit_cast<double>(addend);
    const double adjusted_addend =
        ternary_subtracts(operation) ? -addend_value : addend_value;
    const double rounded =
        std::fma(multiplicand_value, multiplier_value, adjusted_addend);
    std::uint64_t rounded_bits = std::bit_cast<std::uint64_t>(rounded);
    if (!f64_is_normal_or_zero_result(rounded_bits)) {
        return false;
    }
    // The exact-product proof also excludes underflow rounded up to minimum
    // normal; a normal rounded exponent alone does not prove exact scaling.
    if ((rounded_bits & 0x7FFFFFFFFFFFFFFFull) <= 0x0010000000000000ull &&
        !f64_is_zero_bits(multiplicand) && !f64_is_zero_bits(multiplier)) {
        return false;
    }

    if (ternary_negates(operation)) {
        rounded_bits ^= 0x8000000000000000ull;
    }
    result = native_f64_exact_result(rounded_bits);
    return true;
}



}  // namespace

PpcFloat32Result ppc_narrow_f64_to_f32(std::uint64_t bits, std::uint32_t fpscr) {
    // Loads and single-precision arithmetic keep binary32 values widened in
    // the PPC register file. Storing such a value needs no rounding, under
    // any FPSCR rounding mode. Prove the conversion by an exact bit round
    // trip; this also handles signed zero, subnormals, and infinity without
    // changing the host floating-point environment. NaNs still need the
    // general path's payload, quieting, and exception behavior.
    std::uint32_t exact_bits = 0u;
    if ((bits & 0x1FFFFFFFull) == 0u && !f64_is_nan(bits) &&
        try_narrow_widened_f32(bits, exact_bits)) {
        return PpcFloat32Result{exact_bits, 0u};
    }
    const auto result =
        calculate_f64_to_f32(bits, softfloat_rounding_mode(fpscr));
    return PpcFloat32Result{result.bits, result.exceptions};
}

PpcFloatResult ppc_round_f64_to_f32(
    std::uint64_t bits,
    std::uint32_t fpscr) {
    bits = flush_f64_input(bits, fpscr);
    const std::uint32_t invalid =
        f64_is_signaling_nan(bits) ? kVxSnan : 0;
    const auto primary =
        calculate_f64_to_f32(bits, softfloat_rounding_mode(fpscr));
    std::uint32_t result_bits = primary.bits;
    if ((fpscr & kNi) != 0 && (result_bits & 0x7F800000u) == 0 &&
        (result_bits & 0x007FFFFFu) != 0) {
        result_bits &= 0x80000000u;
    }
    const bool inexact =
        (primary.exceptions & softfloat_flag_inexact) != 0;
    bool rounded_up = false;
    if (inexact) {
        const auto truncated =
            calculate_f64_to_f32(bits, softfloat_round_minMag);
        rounded_up = primary.bits != truncated.bits;
    }
    return PpcFloatResult{
        widen_f32(result_bits),
        invalid | map_softfloat_exceptions(primary.exceptions),
        classify_f32(result_bits),
        inexact,
        rounded_up};
}

PpcFloatResult ppc_f64_to_i32_round_zero(
    std::uint64_t bits,
    std::uint32_t fpscr) {
    bits = flush_f64_input(bits, fpscr);
    const auto converted = calculate_f64_to_i32_round_zero(bits);
    const bool invalid =
        (converted.exceptions & softfloat_flag_invalid) != 0;
    std::uint32_t integer_bits = static_cast<std::uint32_t>(converted.value);
    std::uint32_t exception_bits = 0;
    if (invalid) {
        exception_bits |= kVxCvi;
        if (f64_is_signaling_nan(bits)) {
            exception_bits |= kVxSnan;
        }
        if (f64_is_nan(bits) || (bits >> 63) != 0) {
            integer_bits = 0x80000000u;
        } else {
            integer_bits = 0x7FFFFFFFu;
        }
    } else {
        exception_bits |= map_softfloat_exceptions(converted.exceptions);
    }
    const bool inexact =
        (converted.exceptions & softfloat_flag_inexact) != 0;
    return PpcFloatResult{
        0xFFF8000000000000ull | integer_bits,
        exception_bits,
        0,
        inexact,
        false};
}

PpcFloatResult ppc_f64_binary(
    PpcFloatBinaryOperation operation,
    std::uint64_t left,
    std::uint64_t right,
    std::uint32_t fpscr) {
    left = flush_f64_input(left, fpscr);
    right = flush_f64_input(right, fpscr);
    PpcFloatResult fast_result{};
    if (native_f64_binary_fast(operation, left, right, fpscr, fast_result)) {
        return fast_result;
    }
    const std::uint32_t invalid = invalid_detail_f64(operation, left, right);
    if (f64_is_nan(left) || f64_is_nan(right)) {
        const std::uint64_t source = f64_is_nan(left) ? left : right;
        const std::uint64_t result = source | 0x0008000000000000ull;
        return PpcFloatResult{result, invalid, classify_f64(result), false, false};
    }
    if (invalid != 0) {
        constexpr std::uint64_t kCanonicalNan = 0x7FF8000000000000ull;
        return PpcFloatResult{
            kCanonicalNan, invalid, classify_f64(kCanonicalNan), false, false};
    }

    const auto primary =
        calculate_f64(operation, left, right, softfloat_rounding_mode(fpscr));
    std::uint64_t bits = primary.bits;
    if ((fpscr & kNi) != 0 && (bits & 0x7FF0000000000000ull) == 0 &&
        (bits & 0x000FFFFFFFFFFFFFull) != 0) {
        bits &= 0x8000000000000000ull;
    }
    const bool inexact = (primary.exceptions & softfloat_flag_inexact) != 0;
    bool rounded_up = false;
    if (inexact) {
        const auto truncated =
            calculate_f64(operation, left, right, softfloat_round_minMag);
        rounded_up = primary.bits != truncated.bits;
    }
    return PpcFloatResult{
        bits,
        map_softfloat_exceptions(primary.exceptions),
        classify_f64(bits),
        inexact,
        rounded_up};
}

PpcFloatResult ppc_f32_binary(
    PpcFloatBinaryOperation operation,
    std::uint32_t left,
    std::uint32_t right,
    std::uint32_t fpscr) {
    left = flush_f32_input(left, fpscr);
    right = flush_f32_input(right, fpscr);
    // The fast guard admits only finite operands and non-division arithmetic,
    // which cannot raise an invalid-operation detail. Exceptional operands
    // retain the classification below without paying for it on this path.
    PpcFloatResult fast_result{};
    if (native_f32_binary_fast(operation, left, right, fpscr, fast_result)) {
        return fast_result;
    }
    const std::uint32_t invalid = invalid_detail_f32(operation, left, right);
    if (f32_is_nan(left) || f32_is_nan(right)) {
        const std::uint32_t source = f32_is_nan(left) ? left : right;
        const std::uint32_t result = source | 0x00400000u;
        return PpcFloatResult{
            widen_f32(result), invalid, classify_f32(result), false, false};
    }
    if (invalid != 0) {
        constexpr std::uint32_t kCanonicalNan = 0x7FC00000u;
        return PpcFloatResult{
            widen_f32(kCanonicalNan),
            invalid,
            classify_f32(kCanonicalNan),
            false,
            false};
    }

    const auto primary =
        calculate_f32(operation, left, right, softfloat_rounding_mode(fpscr));
    std::uint32_t bits = primary.bits;
    if ((fpscr & kNi) != 0 && (bits & 0x7F800000u) == 0 &&
        (bits & 0x007FFFFFu) != 0) {
        bits &= 0x80000000u;
    }
    const bool inexact = (primary.exceptions & softfloat_flag_inexact) != 0;
    bool rounded_up = false;
    if (inexact) {
        const auto truncated =
            calculate_f32(operation, left, right, softfloat_round_minMag);
        rounded_up = primary.bits != truncated.bits;
    }
    return PpcFloatResult{
        widen_f32(bits),
        map_softfloat_exceptions(primary.exceptions),
        classify_f32(bits),
        inexact,
        rounded_up};
}

PpcFloatResult ppc_f64_binary_to_f32(
    PpcFloatBinaryOperation operation,
    std::uint64_t left,
    std::uint64_t right,
    std::uint32_t fpscr) {
    std::uint32_t left_f32 = 0;
    std::uint32_t right_f32 = 0;
    if (operation != PpcFloatBinaryOperation::Divide &&
        try_narrow_widened_f32(left, left_f32) &&
        try_narrow_widened_f32(right, right_f32)) {
        return ppc_f32_binary(operation, left_f32, right_f32, fpscr);
    }

    const PpcFloatResult wide =
        ppc_f64_binary(operation, left, right, fpscr);
    PpcFloatResult narrowed = ppc_round_f64_to_f32(wide.bits, fpscr);
    narrowed.exception_bits |= wide.exception_bits;
    narrowed.inexact = narrowed.inexact || wide.inexact;
    narrowed.rounded_up = narrowed.rounded_up || wide.rounded_up;
    return narrowed;
}

PpcFloatResult ppc_f64_ternary(
    PpcFloatTernaryOperation operation,
    std::uint64_t multiplicand,
    std::uint64_t multiplier,
    std::uint64_t addend,
    std::uint32_t fpscr) {
    multiplicand = flush_f64_input(multiplicand, fpscr);
    multiplier = flush_f64_input(multiplier, fpscr);
    addend = flush_f64_input(addend, fpscr);
    PpcFloatResult fast_result{};
    if (native_f64_ternary_fast(
            operation, multiplicand, multiplier, addend, fpscr, fast_result)) {
        return fast_result;
    }
    const std::uint32_t invalid = invalid_detail_f64_ternary(
        operation, multiplicand, multiplier, addend);
    if (f64_is_nan(multiplicand) || f64_is_nan(multiplier) ||
        f64_is_nan(addend)) {
        const std::uint64_t source =
            f64_is_nan(multiplicand)
                ? multiplicand
                : (f64_is_nan(multiplier) ? multiplier : addend);
        const std::uint64_t result = source | 0x0008000000000000ull;
        return PpcFloatResult{result, invalid, classify_f64(result), false, false};
    }
    if (invalid != 0) {
        constexpr std::uint64_t kCanonicalNan = 0x7FF8000000000000ull;
        return PpcFloatResult{
            kCanonicalNan, invalid, classify_f64(kCanonicalNan), false, false};
    }

    const auto primary = calculate_f64_ternary(
        operation,
        multiplicand,
        multiplier,
        addend,
        softfloat_rounding_mode(fpscr));
    std::uint64_t bits = primary.bits;
    if ((fpscr & kNi) != 0 && (bits & 0x7FF0000000000000ull) == 0 &&
        (bits & 0x000FFFFFFFFFFFFFull) != 0) {
        bits &= 0x8000000000000000ull;
    }
    const bool inexact =
        (primary.exceptions & softfloat_flag_inexact) != 0;
    bool rounded_up = false;
    if (inexact) {
        const auto truncated = calculate_f64_ternary(
            operation,
            multiplicand,
            multiplier,
            addend,
            softfloat_round_minMag);
        rounded_up = primary.bits != truncated.bits;
    }
    if (ternary_negates(operation)) {
        bits ^= 0x8000000000000000ull;
    }
    return PpcFloatResult{
        bits,
        map_softfloat_exceptions(primary.exceptions),
        classify_f64(bits),
        inexact,
        rounded_up};
}

PpcFloatResult ppc_f32_ternary(
    PpcFloatTernaryOperation operation,
    std::uint32_t multiplicand,
    std::uint32_t multiplier,
    std::uint32_t addend,
    std::uint32_t fpscr) {
    multiplicand = flush_f32_input(multiplicand, fpscr);
    multiplier = flush_f32_input(multiplier, fpscr);
    addend = flush_f32_input(addend, fpscr);
    PpcFloatResult fast_result{};
    if (native_f32_ternary_fast(
            operation, multiplicand, multiplier, addend, fpscr, fast_result)) {
        return fast_result;
    }
    const std::uint32_t invalid = invalid_detail_f32_ternary(
        operation, multiplicand, multiplier, addend);
    if (f32_is_nan(multiplicand) || f32_is_nan(multiplier) ||
        f32_is_nan(addend)) {
        const std::uint32_t source =
            f32_is_nan(multiplicand)
                ? multiplicand
                : (f32_is_nan(multiplier) ? multiplier : addend);
        const std::uint32_t result = source | 0x00400000u;
        return PpcFloatResult{
            widen_f32(result), invalid, classify_f32(result), false, false};
    }
    if (invalid != 0) {
        constexpr std::uint32_t kCanonicalNan = 0x7FC00000u;
        return PpcFloatResult{
            widen_f32(kCanonicalNan),
            invalid,
            classify_f32(kCanonicalNan),
            false,
            false};
    }

    const auto primary = calculate_f32_ternary(
        operation,
        multiplicand,
        multiplier,
        addend,
        softfloat_rounding_mode(fpscr));
    std::uint32_t bits = primary.bits;
    if ((fpscr & kNi) != 0 && (bits & 0x7F800000u) == 0 &&
        (bits & 0x007FFFFFu) != 0) {
        bits &= 0x80000000u;
    }
    const bool inexact =
        (primary.exceptions & softfloat_flag_inexact) != 0;
    bool rounded_up = false;
    if (inexact) {
        const auto truncated = calculate_f32_ternary(
            operation,
            multiplicand,
            multiplier,
            addend,
            softfloat_round_minMag);
        rounded_up = primary.bits != truncated.bits;
    }
    if (ternary_negates(operation)) {
        bits ^= 0x80000000u;
    }
    return PpcFloatResult{
        widen_f32(bits),
        map_softfloat_exceptions(primary.exceptions),
        classify_f32(bits),
        inexact,
        rounded_up};
}

PpcFloatResult ppc_f32_passthrough(std::uint32_t bits) {
    return PpcFloatResult{widen_f32(bits), 0, classify_f32(bits), false, false};
}

PpcFloatResult ppc_f32_reciprocal_estimate(
    std::uint32_t bits,
    std::uint32_t fpscr) {
    bits = flush_f32_input(bits, fpscr);
    if (f32_is_nan(bits)) {
        const std::uint32_t result = bits | 0x00400000u;
        const std::uint32_t exception =
            f32_is_signaling_nan(bits) ? kVxSnan : 0;
        return PpcFloatResult{
            widen_f32(result),
            exception,
            classify_f32(result),
            false,
            false};
    }
    if (f32_is_zero(bits)) {
        const std::uint32_t result =
            (bits & 0x80000000u) | 0x7F800000u;
        return PpcFloatResult{
            widen_f32(result), kZx, classify_f32(result), false, false};
    }
    if (f32_is_infinity(bits)) {
        const std::uint32_t result = bits & 0x80000000u;
        return PpcFloatResult{
            widen_f32(result), 0, classify_f32(result), false, false};
    }

    const auto estimate = calculate_f32(
        PpcFloatBinaryOperation::Divide,
        0x3F800000u,
        bits,
        softfloat_round_minMag);
    std::uint32_t result = truncate_f32_estimate(estimate.bits);
    if ((fpscr & kNi) != 0 && (result & 0x7F800000u) == 0 &&
        (result & 0x007FFFFFu) != 0) {
        result &= 0x80000000u;
    }
    return PpcFloatResult{
        widen_f32(result),
        map_softfloat_exceptions(estimate.exceptions) & ~kXx,
        classify_f32(result),
        false,
        false};
}

PpcFloatResult ppc_f64_reciprocal_estimate(
    std::uint64_t bits,
    std::uint32_t fpscr) {
    bits = flush_f64_input(bits, fpscr);
    if (f64_is_nan(bits)) {
        const auto narrowed = calculate_f64_to_f32(
            bits | 0x0008000000000000ull, softfloat_round_minMag);
        return PpcFloatResult{
            widen_f32(narrowed.bits),
            f64_is_signaling_nan(bits) ? kVxSnan : 0u,
            classify_f32(narrowed.bits), false, false};
    }
    if (f64_is_zero_bits(bits)) {
        const std::uint32_t result =
            (static_cast<std::uint32_t>(bits >> 32u) & 0x80000000u) |
            0x7F800000u;
        return PpcFloatResult{
            widen_f32(result), kZx, classify_f32(result), false, false};
    }
    if (f64_is_infinity_bits(bits)) {
        const std::uint32_t result =
            static_cast<std::uint32_t>(bits >> 32u) & 0x80000000u;
        return PpcFloatResult{
            widen_f32(result), 0u, classify_f32(result), false, false};
    }

    // Estimate from the full source; narrowing the operand would erase legal
    // binary64 values before taking their reciprocal. The software estimate
    // policy is retained here; this is not a Broadway table implementation.
    const auto reciprocal = calculate_f64(
        PpcFloatBinaryOperation::Divide, 0x3FF0000000000000ull, bits,
        softfloat_round_minMag);
    const auto narrowed =
        calculate_f64_to_f32(reciprocal.bits, softfloat_round_minMag);
    std::uint32_t result = truncate_f32_estimate(narrowed.bits);
    if ((fpscr & kNi) != 0 && (result & 0x7F800000u) == 0 &&
        (result & 0x007FFFFFu) != 0) {
        result &= 0x80000000u;
    }
    const auto exceptions = static_cast<std::uint8_t>(
        reciprocal.exceptions | narrowed.exceptions);
    return PpcFloatResult{
        widen_f32(result), map_softfloat_exceptions(exceptions) & ~kXx,
        classify_f32(result), false, false};
}

PpcFloatResult ppc_f32_reciprocal_sqrt_estimate(
    std::uint32_t bits,
    std::uint32_t fpscr) {
    bits = flush_f32_input(bits, fpscr);
    if (f32_is_nan(bits)) {
        const std::uint32_t result = bits | 0x00400000u;
        const std::uint32_t exception =
            f32_is_signaling_nan(bits) ? kVxSnan : 0;
        return PpcFloatResult{
            widen_f32(result),
            exception,
            classify_f32(result),
            false,
            false};
    }
    if (f32_is_zero(bits)) {
        const std::uint32_t result =
            (bits & 0x80000000u) | 0x7F800000u;
        return PpcFloatResult{
            widen_f32(result), kZx, classify_f32(result), false, false};
    }
    if ((bits & 0x80000000u) != 0) {
        constexpr std::uint32_t kCanonicalNan = 0x7FC00000u;
        return PpcFloatResult{
            widen_f32(kCanonicalNan),
            kVxSqrt,
            classify_f32(kCanonicalNan),
            false,
            false};
    }
    if (f32_is_infinity(bits)) {
        return PpcFloatResult{0, 0, classify_f32(0), false, false};
    }

    const auto root = calculate_f32_sqrt(bits, softfloat_round_minMag);
    const auto estimate = calculate_f32(
        PpcFloatBinaryOperation::Divide,
        0x3F800000u,
        root.bits,
        softfloat_round_minMag);
    std::uint32_t result = truncate_f32_estimate(estimate.bits);
    if ((fpscr & kNi) != 0 && (result & 0x7F800000u) == 0 &&
        (result & 0x007FFFFFu) != 0) {
        result = 0;
    }
    const std::uint8_t exceptions =
        static_cast<std::uint8_t>(root.exceptions | estimate.exceptions);
    return PpcFloatResult{
        widen_f32(result),
        map_softfloat_exceptions(exceptions) & ~kXx,
        classify_f32(result),
        false,
        false};
}

PpcFloatResult ppc_f64_reciprocal_sqrt_estimate(
    std::uint64_t bits,
    std::uint32_t fpscr) {
    bits = flush_f64_input(bits, fpscr);
    if (f64_is_nan(bits)) {
        const std::uint64_t result = bits | 0x0008000000000000ull;
        const std::uint32_t exception =
            f64_is_signaling_nan(bits) ? kVxSnan : 0;
        return PpcFloatResult{
            result, exception, classify_f64(result), false, false};
    }
    if (f64_is_zero_bits(bits)) {
        const std::uint32_t result =
            static_cast<std::uint32_t>(bits >> 32) & 0x80000000u;
        const std::uint32_t infinity = result | 0x7F800000u;
        return PpcFloatResult{
            widen_f32(infinity),
            kZx,
            classify_f32(infinity),
            false,
            false};
    }
    if ((bits & 0x8000000000000000ull) != 0) {
        constexpr std::uint64_t kCanonicalNan = 0x7FF8000000000000ull;
        return PpcFloatResult{
            kCanonicalNan,
            kVxSqrt,
            classify_f64(kCanonicalNan),
            false,
            false};
    }
    if (f64_is_infinity_bits(bits)) {
        return PpcFloatResult{0, 0, classify_f64(0), false, false};
    }

    const auto root = calculate_f64_sqrt(bits, softfloat_round_minMag);
    const auto reciprocal = calculate_f64(
        PpcFloatBinaryOperation::Divide,
        0x3FF0000000000000ull,
        root.bits,
        softfloat_round_minMag);
    std::uint64_t result = truncate_f64_estimate(reciprocal.bits);
    if ((fpscr & kNi) != 0 && (result & 0x7FF0000000000000ull) == 0 &&
        (result & 0x000FFFFFFFFFFFFFull) != 0) {
        result = 0;
    }
    const std::uint8_t exceptions = static_cast<std::uint8_t>(
        root.exceptions | reciprocal.exceptions);
    return PpcFloatResult{
        result,
        map_softfloat_exceptions(exceptions) & ~kXx,
        classify_f64(result),
        false,
        false};
}

void ppc_commit_scalar_result(
    PpcContext* context,
    std::uint32_t target,
    const PpcFloatResult& result,
    bool duplicate_single,
    bool record,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    float_native_detail::commit_scalar_result(
        context, target, result, duplicate_single, record, services, guest_pc);
}

void ppc_commit_paired_result(
    PpcContext* context,
    std::uint32_t target,
    const PpcFloatResult& lane0,
    const PpcFloatResult& lane1,
    bool status_from_lane1,
    bool record,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    // Status consumes only these merged fields; each destination keeps its
    // own lane bits after the enabled-exception check below.
    const PpcFloatResult combined{
        0u,
        lane0.exception_bits | lane1.exception_bits,
        status_from_lane1 ? lane1.fprf : lane0.fprf,
        static_cast<bool>(lane0.inexact | lane1.inexact),
        static_cast<bool>(lane0.rounded_up | lane1.rounded_up)};
    apply_result_status(context, combined);
    if (exception_is_enabled(context->fpscr, combined.exception_bits)) {
        guest_execution_fault(
            services,
            guest_pc,
            "enabled paired floating-point exception requires native trap delivery");
    }
    context->fpr_bits[target] = lane0.bits;
    context->ps1_bits[target] = lane1.bits;
    if (record) {
        update_cr1_from_fpscr(context);
    }
}

void ppc_commit_paired_binary_result(
    PpcContext* context,
    std::uint32_t target,
    PpcFloatBinaryOperation operation,
    std::uint32_t left_ps0,
    std::uint32_t left_ps1,
    std::uint32_t right_ps0,
    std::uint32_t right_ps1,
    bool record,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    const std::uint32_t fpscr = context->fpscr;
    left_ps0 = flush_f32_input(left_ps0, fpscr);
    left_ps1 = flush_f32_input(left_ps1, fpscr);
    right_ps0 = flush_f32_input(right_ps0, fpscr);
    right_ps1 = flush_f32_input(right_ps1, fpscr);
    PpcFloatResult lane0{};
    PpcFloatResult lane1{};
    if (!native_f32_paired_binary_fast(operation, left_ps0, left_ps1,
            right_ps0, right_ps1, fpscr, lane0, lane1)) {
        lane0 = ppc_f32_binary(operation, left_ps0, right_ps0, fpscr);
        lane1 = ppc_f32_binary(operation, left_ps1, right_ps1, fpscr);
    }
    // Preserve lane0 FPRF, both lanes' exception/FI/FR union, and the existing
    // enabled-fault boundary before either destination lane or CR1 is written.
    ppc_commit_paired_result(context, target, lane0, lane1, false, record,
        services, guest_pc);
}

void ppc_commit_paired_ternary_result(
    PpcContext* context,
    std::uint32_t target,
    PpcFloatTernaryOperation operation,
    std::uint32_t multiplicand_ps0,
    std::uint32_t multiplicand_ps1,
    std::uint32_t multiplier_ps0,
    std::uint32_t multiplier_ps1,
    std::uint32_t addend_ps0,
    std::uint32_t addend_ps1,
    bool record,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    const std::uint32_t fpscr = context->fpscr;
    multiplicand_ps0 = flush_f32_input(multiplicand_ps0, fpscr);
    multiplicand_ps1 = flush_f32_input(multiplicand_ps1, fpscr);
    multiplier_ps0 = flush_f32_input(multiplier_ps0, fpscr);
    multiplier_ps1 = flush_f32_input(multiplier_ps1, fpscr);
    addend_ps0 = flush_f32_input(addend_ps0, fpscr);
    addend_ps1 = flush_f32_input(addend_ps1, fpscr);
    PpcFloatResult lane0{};
    PpcFloatResult lane1{};
    if (!native_f32_paired_ternary_fast(operation, multiplicand_ps0,
            multiplicand_ps1, multiplier_ps0, multiplier_ps1, addend_ps0,
            addend_ps1, fpscr, lane0, lane1)) {
        lane0 = ppc_f32_ternary(operation, multiplicand_ps0, multiplier_ps0,
            addend_ps0, fpscr);
        lane1 = ppc_f32_ternary(operation, multiplicand_ps1, multiplier_ps1,
            addend_ps1, fpscr);
    }
    ppc_commit_paired_result(context, target, lane0, lane1, false, record,
        services, guest_pc);
}

}  // namespace galaxy
