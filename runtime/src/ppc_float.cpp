#include "galaxy/ppc_float.h"
#include "galaxy/ppc_estimate.h"
#include "galaxy/ppc_paired_float.h"

#include "galaxy/native_api.h"

#include <cmath>

#if defined(_M_X64) || defined(__SSE2__)
#include <emmintrin.h>
#endif

extern "C" {
#include "platform.h"
#include "softfloat.h"
}

namespace galaxy {
namespace {

bool native_f32_fma_wide_fast(
    float multiplicand, float multiplier, float addend, PpcFloatResult& result);

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

// Finite C is rounded with the Gekko fixed significand rule, independently
// of RN. Keep its extended exponent if the largest binary64 rounds upward.
float128_t scalar_single_multiplier(std::uint64_t bits) {
    const std::uint64_t fraction = bits & 0x000FFFFFFFFFFFFFull;
    std::uint64_t mask = 0xFFFFFFFFF8000000ull;
    std::uint64_t rounding_bit = 0x08000000ull;
    if ((bits & 0x7FF0000000000000ull) == 0u && fraction != 0u) {
        const unsigned shift = std::countl_zero(fraction) - 11u;
        // Logical shift plus high ones is a portable arithmetic shift.
        mask = ~(~mask >> shift);
        rounding_bit >>= shift;
    }
    const std::uint64_t rounded = (bits & mask) + (bits & rounding_bit);
    if ((rounded & 0x7FFFFFFFFFFFFFFFull) == 0x7FF0000000000000ull &&
        (bits & 0x7FF0000000000000ull) != 0x7FF0000000000000ull) {
        float128_t extended{};
        extended.v[1] = (bits & 0x8000000000000000ull) |
            (static_cast<std::uint64_t>(16383 + 1024) << 48u);
        return extended;
    }
    return f64_to_f128(float64_t{rounded});
}

struct ScalarSingleIntermediate {
    float128_t value{};
    bool inexact{};
};

ScalarSingleIntermediate calculate_scalar_single_intermediate(
    PpcFloatTernaryOperation operation,
    std::uint64_t multiplicand,
    std::uint64_t multiplier,
    std::uint64_t addend,
    std::uint32_t fpscr) {
    SoftFloatState state(softfloat_round_minMag);
    if (ternary_subtracts(operation)) addend ^= 0x8000000000000000ull;
    float128_t value = f128_mulAdd(f64_to_f128(float64_t{multiplicand}),
        scalar_single_multiplier(multiplier), f64_to_f128(float64_t{addend}));
    const bool inexact = (state.exceptions() & softfloat_flag_inexact) != 0u;
    // Round-to-odd: truncate, then retain discarded information in the low
    // significand bit. 113 bits and the extended exponent avoid binary64's
    // double-rounding, overflow and underflow before the final f32 rounding.
    if (inexact) value.v[0] |= 1u;
    if ((value.v[1] & 0x7FFFFFFFFFFFFFFFull) == 0u && value.v[0] == 0u &&
        ((multiplicand ^ multiplier ^ addend) & 0x8000000000000000ull) != 0u) {
        // Exact cancellation has the sign selected by the final RN, not
        // by the intermediate truncation mode. Equal-sign zeros keep sign.
        value.v[1] = (fpscr & 3u) == 3u ? 0x8000000000000000ull : 0u;
    }
    return {value, inexact};
}

SoftFloatResult32 round_scalar_single_intermediate(
    const ScalarSingleIntermediate& intermediate,
    std::uint_fast8_t rounding) {
    SoftFloatState state(rounding);
    const float32_t result = f128_to_f32(intermediate.value);
    return {result.v, static_cast<std::uint8_t>(state.exceptions() |
        (intermediate.inexact ? softfloat_flag_inexact : 0u))};
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

// Nearest-even binary64 add/subtract/multiply. The rounding error of a sum
// (Knuth TwoSum) or a product (Dekker/Veltkamp TwoProduct) is itself exactly
// representable, so FI and FR follow from the error term's sign without a
// second SoftFloat evaluation. Overflow, subnormal and exceptional operands or
// results keep the SoftFloat path. Only binary operations on SSE2-or-later
// hosts take this route; every operation is one correctly rounded binary64
// instruction (no fused contraction: the helpers below use intrinsics).
#if defined(_M_X64) || defined(__SSE2__)
GALAXY_ALWAYS_INLINE double f64_op_add(double x, double y) {
    return _mm_cvtsd_f64(_mm_add_sd(_mm_set_sd(x), _mm_set_sd(y)));
}
GALAXY_ALWAYS_INLINE double f64_op_sub(double x, double y) {
    return _mm_cvtsd_f64(_mm_sub_sd(_mm_set_sd(x), _mm_set_sd(y)));
}
GALAXY_ALWAYS_INLINE double f64_op_mul(double x, double y) {
    return _mm_cvtsd_f64(_mm_mul_sd(_mm_set_sd(x), _mm_set_sd(y)));
}
#endif

GALAXY_ALWAYS_INLINE PpcFloatResult native_f64_status_result(
    std::uint64_t bits,
    bool inexact,
    bool rounded_up) {
    return PpcFloatResult{
        bits, inexact ? kXx : 0u, classify_f64(bits), inexact, rounded_up};
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
#if defined(_M_X64) || defined(__SSE2__)
    if (operation == PpcFloatBinaryOperation::Add ||
        operation == PpcFloatBinaryOperation::Subtract) {
        const double b = operation == PpcFloatBinaryOperation::Add
            ? right_value : -right_value;
        const double sum = f64_op_add(left_value, b);
        const std::uint64_t sum_bits = std::bit_cast<std::uint64_t>(sum);
        if (!f64_is_normal_or_zero_result(sum_bits)) {
            return false;
        }
        const double bb = f64_op_sub(sum, left_value);
        const double error = f64_op_add(
            f64_op_sub(left_value, f64_op_sub(sum, bb)),
            f64_op_sub(b, bb));
        if (error == 0.0) {
            result = native_f64_exact_result(sum_bits);
            return true;
        }
        // |sum| exceeds the exact value exactly when the error has the
        // opposite sign. A nonzero error implies a nonzero sum.
        result = native_f64_status_result(
            sum_bits, true, (error < 0.0) != (sum < 0.0));
        return true;
    }
    if (operation == PpcFloatBinaryOperation::Multiply) {
        // Keep the operand split and the partial products far from the
        // binary64 exponent limits so every step of the error-free
        // transformation is exact.
        const auto operand_in_range = [](std::uint64_t bits) {
            const std::uint32_t exponent =
                static_cast<std::uint32_t>((bits >> 52) & 0x7FFu);
            return (bits & 0x7FFFFFFFFFFFFFFFull) == 0u ||
                   (exponent >= 723u && exponent <= 1323u);
        };
        if (!operand_in_range(left) || !operand_in_range(right)) {
            return false;
        }
        const double product = f64_op_mul(left_value, right_value);
        const std::uint64_t product_bits =
            std::bit_cast<std::uint64_t>(product);
        if ((product_bits & 0x7FFFFFFFFFFFFFFFull) == 0u) {
            // Zero operand (the operand range excludes underflow to zero).
            result = native_f64_exact_result(product_bits);
            return true;
        }
        const std::uint32_t product_exponent =
            static_cast<std::uint32_t>((product_bits >> 52) & 0x7FFu);
        if (product_exponent < 100u || product_exponent > 1900u) {
            return false;
        }
        constexpr double kSplit = 134217729.0;  // 2^27 + 1
        const auto split = [](double x, double& high, double& low) {
            const double c = f64_op_mul(kSplit, x);
            high = f64_op_sub(c, f64_op_sub(c, x));
            low = f64_op_sub(x, high);
        };
        double ah = 0.0, al = 0.0, bh = 0.0, bl = 0.0;
        split(left_value, ah, al);
        split(right_value, bh, bl);
        const double error = f64_op_add(
            f64_op_add(
                f64_op_add(f64_op_sub(f64_op_mul(ah, bh), product),
                           f64_op_mul(ah, bl)),
                f64_op_mul(al, bh)),
            f64_op_mul(al, bl));
        if (error == 0.0) {
            result = native_f64_exact_result(product_bits);
            return true;
        }
        result = native_f64_status_result(
            product_bits, true, (error < 0.0) != (product < 0.0));
        return true;
    }
    return false;
#else
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
    if (operation == PpcFloatBinaryOperation::Multiply &&
        (rounded_bits & 0x7FFFFFFFFFFFFFFFull) <= 0x0010000000000000ull &&
        !f64_is_zero_bits(left) && !f64_is_zero_bits(right)) {
        return false;
    }
    result = native_f64_exact_result(rounded_bits);
    return true;
#endif
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
        !f32_is_zero_or_normal(addend)) {
        return false;
    }

    const float multiplicand_value = std::bit_cast<float>(multiplicand);
    const float multiplier_value = std::bit_cast<float>(multiplier);
    const float addend_value = std::bit_cast<float>(addend);
    const float adjusted_addend =
        ternary_subtracts(operation) ? -addend_value : addend_value;
    if (!f32_fma_exact_in_double(multiplicand, multiplier, addend)) {
        // Wide exponent gap (e.g. position += velocity * dt): every operand is
        // a nonzero normal here, and the wide path keeps the software result.
        if (!native_f32_fma_wide_fast(
                multiplicand_value, multiplier_value, adjusted_addend, result)) {
            return false;
        }
    } else {
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
    }
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
        !native_f32_binary_inputs(addend_ps0, addend_ps1)) {
        return false;
    }
    if (!f32_fma_exact_in_double(multiplicand_ps0, multiplier_ps0, addend_ps0) ||
        !f32_fma_exact_in_double(multiplicand_ps1, multiplier_ps1, addend_ps1)) {
        // The packed exact-sum proof can decline otherwise ordinary operands.
        // Keep them inside the native paired boundary when both scalar native
        // proofs succeed, including the existing wide-exponent TwoSum path.
        // Lane results are local: if either proof declines, the caller still
        // recomputes both through the original general arithmetic/commit path.
        return native_f32_ternary_fast(operation, multiplicand_ps0, multiplier_ps0,
                   addend_ps0, fpscr, lane0) &&
            native_f32_ternary_fast(operation, multiplicand_ps1, multiplier_ps1,
                addend_ps1, fpscr, lane1);
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

// Binary32 status for a normal-or-zero result whose inexact/rounded-up
// relation to the exact value was derived by the caller.
GALAXY_ALWAYS_INLINE PpcFloatResult native_f32_status_result(
    std::uint32_t bits,
    bool inexact,
    bool rounded_up) {
    const bool zero = (bits & 0x7FFFFFFFu) == 0u;
    std::uint32_t exception_bits = inexact ? kXx : 0u;
    if (zero && inexact) {
        exception_bits |= kUx;
    }
    const std::uint32_t fprf = zero
        ? ((bits & 0x80000000u) != 0u ? 0x00012000u : 0x00002000u)
        : ((bits & 0x80000000u) != 0u ? 0x00008000u : 0x00004000u);
    return PpcFloatResult{
        widen_f32_bits(bits), exception_bits, fprf, inexact, rounded_up};
}

// Fused binary32 multiply-add for nonzero normal operands outside the range
// where the binary64 sum is provably exact. The product of two binary32 values
// is exact in binary64 (at most 48 bits). Knuth's TwoSum then gives sum + error
// == product + addend exactly, so the only way a plain conversion of `sum` to
// binary32 can differ from the correctly rounded fused result is double
// rounding onto an exact binary32 midpoint; the sign of `error` selects the
// side there. FI/FR derive from the same exact pair. Results near the
// subnormal or overflow limits, and exact cancellation, keep the software path.
bool native_f32_fma_wide_fast(
    float multiplicand,
    float multiplier,
    float addend,
    PpcFloatResult& result) {
    const double product =
        static_cast<double>(multiplicand) * static_cast<double>(multiplier);
    const double addend_value = static_cast<double>(addend);
    const double sum = product + addend_value;
    const double virtual_addend = sum - product;
    const double error =
        (product - (sum - virtual_addend)) + (addend_value - virtual_addend);

    // Binary64 exponent fields 898 / 1150 are 2^-125 / 2^127.
    const std::uint64_t sum_magnitude =
        std::bit_cast<std::uint64_t>(sum) & 0x7FFFFFFFFFFFFFFFull;
    if (sum_magnitude < (898ull << 52) || sum_magnitude >= (1150ull << 52)) {
        return false;
    }

    const float rounded = static_cast<float>(sum);
    std::uint32_t bits = std::bit_cast<std::uint32_t>(rounded);
    const double rounded_value = static_cast<double>(rounded);
    if (error != 0.0 && rounded_value != sum) {
        // Neighbor of `rounded` on the side of `sum`; the range guard keeps
        // both normal.
        const bool away_from_zero = std::fabs(sum) > std::fabs(rounded_value);
        const std::uint32_t neighbor_bits = away_from_zero ? bits + 1u : bits - 1u;
        const double neighbor_value =
            static_cast<double>(std::bit_cast<float>(neighbor_bits));
        if (sum == (rounded_value + neighbor_value) * 0.5) {
            const bool true_value_above = error > 0.0;
            const bool neighbor_above = neighbor_value > rounded_value;
            if (true_value_above == neighbor_above) {
                bits = neighbor_bits;
            }
        }
    }

    const double result_value = static_cast<double>(std::bit_cast<float>(bits));
    const bool inexact = error != 0.0 || result_value != sum;
    bool rounded_up = false;
    if (result_value != sum) {
        rounded_up = std::fabs(result_value) > std::fabs(sum);
    } else if (error != 0.0) {
        rounded_up = (error < 0.0) != (result_value < 0.0);
    }
    result = native_f32_status_result(bits, inexact, rounded_up);
    return true;
}

// Nearest-even binary32 division of finite normal/zero operands. A binary32
// product has at most 48 significant bits, so q*d is exact in binary64.
// Comparing that exact product with the dividend yields FI and FR without a
// second division: |q*d| > |n| exactly when the quotient was rounded up.
bool native_f32_divide_fast(
    std::uint32_t left,
    std::uint32_t right,
    std::uint32_t fpscr,
    PpcFloatResult& result) {
    if ((fpscr & 0x3u) != 0u || !native_f32_binary_inputs(left, right) ||
        (right & 0x7FFFFFFFu) == 0u) {
        return false;
    }
    const float numerator = std::bit_cast<float>(left);
    const float denominator = std::bit_cast<float>(right);
    const float quotient = numerator / denominator;
    const std::uint32_t quotient_bits = std::bit_cast<std::uint32_t>(quotient);
    if (!native_f32_result_is_eligible(quotient_bits)) {
        return false;
    }
    const double product =
        static_cast<double>(quotient) * static_cast<double>(denominator);
    const double dividend = static_cast<double>(numerator);
    result = native_f32_status_result(
        quotient_bits,
        product != dividend,
        std::fabs(product) > std::fabs(dividend));
    return true;
}

// frsp: nearest-even binary64 -> binary32 for a value whose binary32 result
// is normal. FI/FR follow from comparing the exact widened result.
bool native_round_f64_to_f32_fast(
    std::uint64_t bits,
    std::uint32_t fpscr,
    PpcFloatResult& result) {
    if ((fpscr & 0x3u) != 0u) {
        return false;
    }
    const std::uint32_t exponent =
        static_cast<std::uint32_t>((bits >> 52) & 0x7FFu);
    if (exponent < 897u || exponent > 1150u) {
        if ((bits & 0x7FFFFFFFFFFFFFFFull) == 0u) {
            result = native_f32_status_result(
                static_cast<std::uint32_t>(bits >> 32) & 0x80000000u,
                false, false);
            return true;
        }
        return false;
    }
    const double value = std::bit_cast<double>(bits);
    const float rounded = static_cast<float>(value);
    const std::uint32_t rounded_bits = std::bit_cast<std::uint32_t>(rounded);
    if (!native_f32_result_is_eligible(rounded_bits)) {
        return false;
    }
    const double widened = static_cast<double>(rounded);
    result = native_f32_status_result(
        rounded_bits,
        widened != value,
        std::fabs(widened) > std::fabs(value));
    return true;
}

// fctiwz for a finite value whose truncation fits int32.
bool native_f64_to_i32_round_zero_fast(
    std::uint64_t bits,
    PpcFloatResult& result) {
    const double value = std::bit_cast<double>(bits);
    if (!(value > -2147483649.0 && value < 2147483648.0)) {
        return false;
    }
    const std::int32_t integer = static_cast<std::int32_t>(value);
    const bool inexact = static_cast<double>(integer) != value;
    result = PpcFloatResult{
        0xFFF8000000000000ull | static_cast<std::uint32_t>(integer),
        inexact ? kXx : 0u,
        0u,
        inexact,
        false};
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
    PpcFloatResult fast_result{};
    if (native_round_f64_to_f32_fast(bits, fpscr, fast_result)) {
        return fast_result;
    }
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
    PpcFloatResult fast_result{};
    if (native_f64_to_i32_round_zero_fast(bits, fast_result)) {
        return fast_result;
    }
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
    if (operation == PpcFloatBinaryOperation::Divide &&
        native_f32_divide_fast(left, right, fpscr, fast_result)) {
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
    if (operation == PpcFloatBinaryOperation::Divide &&
        try_narrow_normal_or_zero_widened_f32(left, left_f32) &&
        try_narrow_normal_or_zero_widened_f32(right, right_f32)) {
        PpcFloatResult divided{};
        if (native_f32_divide_fast(left_f32, right_f32, fpscr, divided)) {
            return divided;
        }
    }
    if ((fpscr & kNi) == 0u &&
        try_narrow_widened_f32(left, left_f32) &&
        try_narrow_widened_f32(right, right_f32)) {
        // Exact widened binary32 operands require a binary32 result/status,
        // including NI-off division declined by the native normal-result guard.
        // Dividing to binary64 and OR-ing its FR into the narrowing status can
        // mark a single result rounded up solely because the intermediate was.
        // With NI, scalar sources remain binary64: a widened binary32
        // subnormal is normal in that source format. Keep the existing wide
        // path rather than flush both sources through the paired f32 helper.
        return ppc_f32_binary(operation, left_f32, right_f32, fpscr);
    }

    // Scalar single operands retain their binary64 source format, including
    // NI. Reuse the checked extended-exponent single-rounding fused core:
    // addition/subtraction use an exact unit multiplier; multiplication uses
    // a product-sign zero, retaining signed-zero and fixed25-bit C semantics.
    left = flush_f64_input(left, fpscr);
    right = flush_f64_input(right, fpscr);
    if (f64_is_finite(left) && f64_is_finite(right)) {
        if (operation == PpcFloatBinaryOperation::Add ||
            operation == PpcFloatBinaryOperation::Subtract) {
            return ppc_f64_ternary_to_f32(
                operation == PpcFloatBinaryOperation::Add
                    ? PpcFloatTernaryOperation::MultiplyAdd
                    : PpcFloatTernaryOperation::MultiplySubtract,
                left, 0x3FF0000000000000ull, right, fpscr);
        }
        if (operation == PpcFloatBinaryOperation::Multiply) {
            return ppc_f64_ternary_to_f32(PpcFloatTernaryOperation::MultiplyAdd,
                left, right, (left ^ right) & 0x8000000000000000ull, fpscr);
        }
        if ((right & 0x7FFFFFFFFFFFFFFFull) != 0u) {
            // A53-bit round-to-odd quotient suffices for one final24-bit
            // rounding. At binary64 overflow/underflow, a truncated finite
            // saturation or signed tiny jam still lies far beyond binary32's
            // range and retains the required rounding-direction information.
            const auto quotient = calculate_f64(operation, left, right,
                softfloat_round_minMag);
            const bool wide_inexact =
                (quotient.exceptions & softfloat_flag_inexact) != 0u;
            const auto jammed = quotient.bits | (wide_inexact ? 1ull : 0ull);
            const auto primary = calculate_f64_to_f32(jammed,
                softfloat_rounding_mode(fpscr));
            const bool inexact = wide_inexact ||
                (primary.exceptions & softfloat_flag_inexact) != 0u;
            const bool rounded_up = inexact && primary.bits !=
                calculate_f64_to_f32(jammed, softfloat_round_minMag).bits;
            auto bits = primary.bits;
            if ((fpscr & kNi) != 0u &&
                (jammed & 0x7FFFFFFFFFFFFFFFull) < 0x3810000000000000ull) {
                bits &= 0x80000000u;
            }
            return {widen_f32(bits),
                map_softfloat_exceptions(primary.exceptions) |
                    (inexact ? kXx : 0u),
                classify_f32(bits), inexact, rounded_up};
        }
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
                : (f64_is_nan(addend) ? addend : multiplier);
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
                : (f32_is_nan(addend) ? addend : multiplier);
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

PpcFloatResult ppc_f64_ternary_to_f32(
    PpcFloatTernaryOperation operation,
    std::uint64_t multiplicand,
    std::uint64_t multiplier,
    std::uint64_t addend,
    std::uint32_t fpscr) {
    std::uint32_t a32{}, c32{}, b32{};
    if ((fpscr & kNi) == 0u &&
        try_narrow_widened_f32(multiplicand, a32) &&
        try_narrow_widened_f32(multiplier, c32) &&
        try_narrow_widened_f32(addend, b32)) {
        return ppc_f32_ternary(operation, a32, c32, b32, fpscr);
    }
    multiplicand = flush_f64_input(multiplicand, fpscr);
    multiplier = flush_f64_input(multiplier, fpscr);
    addend = flush_f64_input(addend, fpscr);
    // NI does not exclude normal/zero binary32 operands widened to f64.
    // In particular, do NOT route widened f32 subnormals through f32 NI input
    // flushing: they are normal scalar f64 values. The exact source proof
    // below excludes them. The native result proof also declines minimum
    // normal and nonzero subnormal outputs, preserving scalar pre-round NI
    // behavior at the boundary where it differs from paired arithmetic.
    PpcFloatResult ni_fast{};
    if ((fpscr & (kNi | 3u)) == kNi &&
        try_narrow_normal_or_zero_widened_f32(multiplicand, a32) &&
        try_narrow_normal_or_zero_widened_f32(multiplier, c32) &&
        try_narrow_normal_or_zero_widened_f32(addend, b32) &&
        native_f32_ternary_fast(operation, a32, c32, b32, fpscr, ni_fast)) {
        return ni_fast;
    }
    if (!f64_is_finite(multiplicand) || !f64_is_finite(multiplier) ||
        !f64_is_finite(addend)) {
        const auto wide = ppc_f64_ternary(operation, multiplicand, multiplier,
            addend, fpscr);
        auto result = ppc_round_f64_to_f32(wide.bits, fpscr);
        result.exception_bits |= wide.exception_bits;
        return result;
    }
    const auto intermediate = calculate_scalar_single_intermediate(operation,
        multiplicand, multiplier, addend, fpscr);
    const auto primary = round_scalar_single_intermediate(intermediate,
        softfloat_rounding_mode(fpscr));
    const bool inexact = (primary.exceptions & softfloat_flag_inexact) != 0u;
    const bool rounded_up = inexact && primary.bits !=
        round_scalar_single_intermediate(intermediate, softfloat_round_minMag).bits;
    std::uint32_t bits = primary.bits;
    // NI flushes a tiny exact fused result before final rounding. A value
    // immediately below the normal boundary must not round into a normal.
    if ((fpscr & kNi) != 0u &&
        (intermediate.value.v[1] & 0x7FFFFFFFFFFFFFFFull) <
            (static_cast<std::uint64_t>(16383 - 126) << 48u)) {
        bits &= 0x80000000u;
    }
    if (ternary_negates(operation)) bits ^= 0x80000000u;
    return {widen_f32(bits), map_softfloat_exceptions(primary.exceptions),
        classify_f32(bits), inexact, rounded_up};
}

void ppc_commit_scalar_single_ternary(
    PpcContext* context,
    std::uint32_t target,
    PpcFloatTernaryOperation operation,
    std::uint32_t multiplicand_register,
    std::uint32_t multiplier_register,
    std::uint32_t addend_register,
    bool record,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    const auto multiplicand = context->fpr_bits[multiplicand_register];
    const auto multiplier = context->fpr_bits[multiplier_register];
    const auto addend = context->fpr_bits[addend_register];
    std::uint32_t a32{}, c32{}, b32{};
    PpcFloatResult fast{};
    // Both input and output proofs exclude the scalar/paired NI differences;
    // qualifying normal operands need no binary128 fallback in NI mode.
    if (try_narrow_normal_or_zero_widened_f32(multiplicand, a32) &&
        try_narrow_normal_or_zero_widened_f32(multiplier, c32) &&
        try_narrow_normal_or_zero_widened_f32(addend, b32) &&
        native_f32_ternary_fast(operation, a32, c32, b32, context->fpscr, fast)) {
        commit_scalar_result(context, target, fast, true, record, services, guest_pc);
        return;
    }
    const auto result = ppc_f64_ternary_to_f32(operation, multiplicand,
        multiplier, addend, context->fpscr);
    ppc_commit_scalar_result(context, target, result, true, record, services, guest_pc);
}

PpcFloatResult ppc_f32_passthrough(std::uint32_t bits) {
    return PpcFloatResult{widen_f32(bits), 0, classify_f32(bits), false, false};
}

PpcFloatResult ppc_f64_reciprocal_estimate(
    std::uint64_t bits, std::uint32_t fpscr) {
    const auto result = estimate_detail::reciprocal_bits(bits);
    const bool special = f64_is_zero_bits(bits) || f64_is_nan(bits) ||
        f64_is_infinity_bits(bits);
    const auto exceptions = f64_is_zero_bits(bits) ? kZx :
        (f64_is_signaling_nan(bits) ? kVxSnan : 0u);
    // Finite results are exact widened normal singles, signed zero or max
    // single; classify_f64 gives the same FPRF. Preserve full quiet NaN payloads.
    // Estimates do not use NI input flushing or raise division OX/UX/XX.
    return PpcFloatResult{result, exceptions, classify_f64(result),
        !special && (fpscr & kFi) != 0u, !special && (fpscr & kFr) != 0u};
}

PpcFloatResult ppc_f32_reciprocal_estimate(
    std::uint32_t bits, std::uint32_t fpscr) {
    const auto exponent = (bits >> 23u) & 0xFFu;
    if (exponent != 0u && exponent != 0xFFu) {
        const auto index = (bits & 0x007FFFFFu) >> 8u;
        const auto& entry = estimate_detail::kReciprocal[index / 1024u];
        const auto fraction = entry.base - (entry.decrement * (index % 1024u) + 1u) / 2u;
        const std::uint32_t single = (bits & 0x80000000u) |
            (exponent >= 253u ? 0u : ((253u - exponent) << 23u) | fraction);
        return PpcFloatResult{widen_f32(single), 0u, classify_f32(single),
            (fpscr & kFi) != 0u, (fpscr & kFr) != 0u};
    }
    return ppc_f64_reciprocal_estimate(widen_f32(bits), fpscr);
}

PpcFloatResult ppc_f64_reciprocal_sqrt_estimate(
    std::uint64_t bits, std::uint32_t fpscr) {
    const auto result = estimate_detail::reciprocal_root_bits(bits);
    const bool zero = f64_is_zero_bits(bits);
    const bool nan = f64_is_nan(bits);
    const bool negative = (bits & estimate_detail::kSign) != 0u && !zero && !nan;
    const bool special = zero || nan || negative || f64_is_infinity_bits(bits);
    const auto exceptions = zero ? kZx : (negative ? kVxSqrt :
        (f64_is_signaling_nan(bits) ? kVxSnan : 0u));
    return PpcFloatResult{result, exceptions, classify_f64(result),
        !special && (fpscr & kFi) != 0u, !special && (fpscr & kFr) != 0u};
}

PpcFloatResult ppc_f32_reciprocal_sqrt_estimate(
    std::uint32_t bits, std::uint32_t fpscr) {
    const auto input_exponent = (bits >> 23u) & 0xFFu;
    if ((bits & 0x80000000u) == 0u && input_exponent != 0u && input_exponent != 0xFFu) {
        // Same table/interpolation directly in the source/result format. Avoid
        // constructing a binary64 result and making a second struct-return call.
        const auto index = ((input_exponent & 1u) << 15u) |
            ((bits & 0x007FFFFFu) >> 8u);
        const auto& entry = estimate_detail::kReciprocalRoot[index / 2048u];
        const auto fraction = static_cast<std::uint32_t>(entry.base +
            entry.decrement * static_cast<std::int32_t>(index % 2048u));
        std::uint32_t single = ((190u - (input_exponent + 1u) / 2u) << 23u) |
            (fraction >> 3u);
        const auto discarded = fraction & 7u;
        const auto rounding = fpscr & 3u;
        const bool increment = rounding == 0u
            ? discarded > 4u || (discarded == 4u && (single & 1u) != 0u)
            : rounding == 2u && discarded != 0u;
        single += increment ? 1u : 0u;
        return PpcFloatResult{widen_f32(single), 0u, classify_f32(single),
            (fpscr & kFi) != 0u, (fpscr & kFr) != 0u};
    }
    auto result = ppc_f64_reciprocal_sqrt_estimate(widen_f32(bits), fpscr);
    // Positive finite binary32 inputs produce a normal binary32-range root
    // estimate. Round its 26-bit mantissa to binary32 using guest RN; estimates
    // do not set arithmetic FI/FR/XX from this conversion. Specials are exact
    // widened singles (including quieted binary32 NaN payloads).
    const auto exponent = static_cast<std::uint32_t>((result.bits >> 52u) & 0x7FFu);
    if (exponent != 0u && exponent != 0x7FFu) {
        std::uint32_t single = ((exponent - 896u) << 23u) |
            (static_cast<std::uint32_t>(result.bits >> 29u) & 0x007FFFFFu);
        const auto discarded = result.bits & 0x1FFFFFFFull;
        const auto rounding = fpscr & 3u;
        const bool increment = rounding == 0u
            ? discarded > 0x10000000ull ||
                (discarded == 0x10000000ull && (single & 1u) != 0u)
            : rounding == 2u && discarded != 0u;
        single += increment ? 1u : 0u;
        result.bits = widen_f32(single);
        result.fprf = classify_f32(single);
    }
    return result;
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

void ppc_commit_paired_estimate_result(
    PpcContext* context, std::uint32_t target,
    const PpcFloatResult& lane0, const PpcFloatResult& lane1,
    bool record, const NativeServicesV1* services, std::uint32_t guest_pc) {
    auto first = lane0;
    auto second = lane1;
    first.inexact = second.inexact = lane0.inexact && lane1.inexact;
    first.rounded_up = second.rounded_up = lane0.rounded_up && lane1.rounded_up;
    ppc_commit_paired_result(context, target, first, second, false, record,
        services, guest_pc);
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

// Private four-register ABI trial. Descriptor layout: target[4:0], left[9:5],
// right[14:10], operation[16:15], lane0-is-PS1[17], lane1-is-PS1[18], Rc[19].
// The descriptor is emitted from decoded, validated instructions. Existing
// MSR/FPU retry and instruction checkpoint remain at the generated call site.
namespace {
// One arithmetic implementation serves the existing struct carrier and the
// opt-in register-sized binary32/status carrier.
template<class Output = PpcFloatResult>
GALAXY_ALWAYS_INLINE Output indexed_make_f32_native_result(
    std::uint32_t bits, std::uint32_t exceptions, std::uint32_t fprf,
    bool inexact, bool rounded_up) {
    if constexpr (std::is_same_v<Output, std::uint64_t>) {
        const std::uint32_t status = exceptions | fprf |
            (inexact ? kFi : 0u) | (rounded_up ? kFr : 0u);
        return bits | (std::uint64_t(status) << 32u);
    } else {
        static_assert(std::is_same_v<Output, PpcFloatResult>);
        return PpcFloatResult{widen_f32_bits(bits), exceptions, fprf,
            inexact, rounded_up};
    }
}

template<class Output = PpcFloatResult>
GALAXY_ALWAYS_INLINE Output indexed_native_f32_normal_or_zero_result(
    std::uint32_t bits,
    double exact) {
    // Native binary32 callers prove that `bits` is either a normal
    // result above minimum normal or signed zero. Keep the status assembly
    // specialized to that contract: infinity/NaN and subnormal results have
    // already selected the SoftFloat path, while a nonzero exact value that
    // rounds to zero still needs underflow and inexact.
    const std::uint64_t widened = widen_f32_bits(bits);
    // Both values are finite. IEEE binary64 magnitude encodings have the
    // same order as their nonnegative values, including underflow to zero.
    // Removing the sign also makes opposite signed zeros equal, as required
    // for FI. FR records an increase in magnitude regardless of sign.
    const std::uint64_t rounded_magnitude = widened & 0x7FFFFFFFFFFFFFFFull;
    const std::uint64_t exact_magnitude =
        std::bit_cast<std::uint64_t>(exact) & 0x7FFFFFFFFFFFFFFFull;
    const bool inexact = rounded_magnitude != exact_magnitude;
    std::uint32_t exception_bits = inexact ? kXx : 0;
    const bool zero = (bits & 0x7FFFFFFFu) == 0u;
    if (zero && inexact) {
        exception_bits |= kUx;
    }
    const bool rounded_up = rounded_magnitude > exact_magnitude;
    const std::uint32_t fprf = zero
        ? ((bits & 0x80000000u) != 0u ? 0x00012000u : 0x00002000u)
        : ((bits & 0x80000000u) != 0u ? 0x00008000u : 0x00004000u);
    if constexpr (std::is_same_v<Output, std::uint64_t>) {
        return indexed_make_f32_native_result<Output>(bits, exception_bits, fprf,
            inexact, rounded_up);
    } else {
        // Reuse the widening already needed for FI/FR. Repeating it here
        // grows the existing inlined scalar path even when packed returns
        // are disabled.
        static_assert(std::is_same_v<Output, PpcFloatResult>);
        return PpcFloatResult{widened, exception_bits, fprf,
            inexact, rounded_up};
    }
}

// Add/subtract/multiply core for operands the caller has already proved to be
// finite normal-or-zero binary32 under nearest-even rounding.
template <PpcFloatBinaryOperation Operation, class Output = PpcFloatResult>
GALAXY_ALWAYS_INLINE bool indexed_native_f32_binary_core(
    std::uint32_t left,
    std::uint32_t right,
    Output& result) {
    static_assert(Operation != PpcFloatBinaryOperation::Divide);
    if constexpr (Operation == PpcFloatBinaryOperation::Add ||
                  Operation == PpcFloatBinaryOperation::Subtract) {
        if (!f32_add_sub_exact_in_double(left, right)) {
            // Both operands are nonzero normal binary32 values, separated by
            // more than 28 exponent steps. The smaller magnitude is strictly
            // below half an ULP on either side of the larger value, including
            // a binade boundary. Nearest-even therefore keeps the larger
            // value. The sum is inexact; opposite effective signs round its
            // magnitude up, and matching signs round it down. Derive those
            // status bits directly instead of losing the small operand in
            // binary64 or evaluating the software operation twice to recover
            // FI/FR.
            const std::uint32_t adjusted_right = right ^
                (Operation == PpcFloatBinaryOperation::Subtract
                     ? 0x80000000u : 0u);
            const std::uint32_t dominant =
                (left & 0x7FFFFFFFu) > (right & 0x7FFFFFFFu)
                    ? left : adjusted_right;
            result = indexed_make_f32_native_result<Output>(dominant, kXx,
                classify_f32(dominant), true,
                ((left ^ adjusted_right) & 0x80000000u) != 0u);
            return true;
        }
    }

    const float left_value = std::bit_cast<float>(left);
    const float right_value = std::bit_cast<float>(right);
    double exact = 0.0;
    float rounded = 0.0f;
    if constexpr (Operation == PpcFloatBinaryOperation::Add) {
        exact = static_cast<double>(left_value) + static_cast<double>(right_value);
        rounded = left_value + right_value;
    } else if constexpr (Operation == PpcFloatBinaryOperation::Subtract) {
        exact = static_cast<double>(left_value) - static_cast<double>(right_value);
        rounded = left_value - right_value;
    } else {
        exact = static_cast<double>(left_value) * static_cast<double>(right_value);
        rounded = left_value * right_value;
    }
    const std::uint32_t rounded_bits = std::bit_cast<std::uint32_t>(rounded);
    if (!native_f32_result_is_eligible(rounded_bits)) {
        return false;
    }
    result = indexed_native_f32_normal_or_zero_result<Output>(rounded_bits, exact);
    return true;
}

template<class Output = PpcFloatResult>
GALAXY_ALWAYS_INLINE bool indexed_native_f32_binary_fast(
    PpcFloatBinaryOperation operation,
    std::uint32_t left,
    std::uint32_t right,
    std::uint32_t fpscr,
    Output& result) {
    // NI inputs have already been flushed by the caller. Normal/zero output
    // needs no NI adjustment; nonzero subnormal results retain the software
    // path below. NI therefore does not exclude this exact native path.
    if ((fpscr & 0x3u) != 0 || operation == PpcFloatBinaryOperation::Divide ||
        !native_f32_binary_inputs(left, right)) {
        return false;
    }
    switch (operation) {
    case PpcFloatBinaryOperation::Add:
        return indexed_native_f32_binary_core<PpcFloatBinaryOperation::Add>(
            left, right, result);
    case PpcFloatBinaryOperation::Subtract:
        return indexed_native_f32_binary_core<PpcFloatBinaryOperation::Subtract>(
            left, right, result);
    case PpcFloatBinaryOperation::Multiply:
        return indexed_native_f32_binary_core<PpcFloatBinaryOperation::Multiply>(
            left, right, result);
    case PpcFloatBinaryOperation::Divide:
        return false;
    }
    return false;
}

template<class Output>
GALAXY_ALWAYS_INLINE Output indexed_binary_impl(
    PpcFloatBinaryOperation operation,
    std::uint32_t left,
    std::uint32_t right,
    std::uint32_t fpscr) {
    left = flush_f32_input(left, fpscr);
    right = flush_f32_input(right, fpscr);
    // The fast guard admits only finite operands and non-division arithmetic,
    // which cannot raise an invalid-operation detail. Exceptional operands
    // retain the classification below without paying for it on this path.
    Output fast_result{};
    if (indexed_native_f32_binary_fast(operation, left, right, fpscr, fast_result)) {
        return fast_result;
    }
    if (operation == PpcFloatBinaryOperation::Divide) {
        PpcFloatResult divided{};
        if (native_f32_divide_fast(left, right, fpscr, divided)) {
            if constexpr (std::is_same_v<Output, PpcFloatResult>) {
                return divided;
            } else {
                return indexed_make_f32_native_result<Output>(
                    native_stored_single_bits(divided.bits, 0u),
                    divided.exception_bits, divided.fprf,
                    divided.inexact, divided.rounded_up);
            }
        }
    }
    const std::uint32_t invalid = invalid_detail_f32(operation, left, right);
    if (f32_is_nan(left) || f32_is_nan(right)) {
        const std::uint32_t source = f32_is_nan(left) ? left : right;
        const std::uint32_t result = source | 0x00400000u;
        return indexed_make_f32_native_result<Output>(
            result, invalid, classify_f32(result), false, false);
    }
    if (invalid != 0) {
        constexpr std::uint32_t kCanonicalNan = 0x7FC00000u;
        return indexed_make_f32_native_result<Output>(kCanonicalNan,
            invalid, classify_f32(kCanonicalNan), false, false);
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
    return indexed_make_f32_native_result<Output>(bits,
        map_softfloat_exceptions(primary.exceptions), classify_f32(bits),
        inexact, rounded_up);
}


GALAXY_NOINLINE std::uint64_t indexed_binary(
    PpcFloatBinaryOperation op, std::uint32_t a, std::uint32_t b,
    std::uint32_t fpscr) {
    return indexed_binary_impl<std::uint64_t>(op, a, b, fpscr);
}
GALAXY_ALWAYS_INLINE void indexed_commit(
    PpcContext* context, std::uint32_t target,
    std::uint64_t packed0, std::uint64_t packed1,
    bool record, const NativeServicesV1* services, std::uint32_t guest_pc) {
    // Arithmetic returns exception details, not the FX/VX/FEX summaries.
    // All three status fields are disjoint; lane1 must not supply FPRF.
    constexpr std::uint32_t exceptions =
        kOx | kUx | kZx | kXx | kInvalidDetailMask;
    static_assert((exceptions & (kFprfMask | kFi | kFr)) == 0u);
    static_assert(exceptions == 0x1FF80700u);
    const std::uint32_t s0 = std::uint32_t(packed0 >> 32u);
    const std::uint32_t s1 = std::uint32_t(packed1 >> 32u);
    const std::uint32_t both = s0 | s1;
    const PpcFloatResult status{0u, both & exceptions,
        s0 & kFprfMask, (both & kFi) != 0u,
        (both & kFr) != 0u};
    apply_result_status(context, status);
    if (exception_is_enabled(context->fpscr, status.exception_bits)) {
        guest_execution_fault(services, guest_pc,
            "enabled paired floating-point exception requires native trap delivery");
    }
    context->fpr_bits[target] = widen_f32_bits(std::uint32_t(packed0));
    context->ps1_bits[target] = widen_f32_bits(std::uint32_t(packed1));
    if (record) update_cr1_from_fpscr(context);
}

} // namespace

void ppc_execute_indexed_paired_binary(PpcContext* context,
    std::uint32_t descriptor, const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    const auto target = descriptor & 31u;
    const auto left = (descriptor >> 5u) & 31u;
    const auto right = (descriptor >> 10u) & 31u;
    const auto op = static_cast<PpcFloatBinaryOperation>((descriptor >> 15u) & 3u);
    require_paired_single_mode(context, false, services, guest_pc);
    // Keep checks in the exact original sequence; capture every input before
    // publishing either output, including target/source aliases and broadcasts.
    const auto a0 = require_single_precision_bits(context->fpr_bits[left], services, guest_pc);
    const auto a1 = require_single_precision_bits(context->ps1_bits[left], services, guest_pc);
    const auto b0 = require_single_precision_bits(
        (descriptor & (1u << 17u)) ? context->ps1_bits[right] : context->fpr_bits[right], services, guest_pc);
    const auto b1 = require_single_precision_bits(
        (descriptor & (1u << 18u)) ? context->ps1_bits[right] : context->fpr_bits[right], services, guest_pc);
    const auto fpscr = context->fpscr;
    // Reuse the existing two-lane exact native proof at this complete
    // boundary. Source checks above retain their order and capture aliases
    // before any destination/status write. Exceptional lanes keep the
    // original scalar carrier path below.
    PpcFloatResult lane0{}, lane1{};
    if (native_f32_paired_binary_fast(op, flush_f32_input(a0, fpscr), flush_f32_input(a1, fpscr),
            flush_f32_input(b0, fpscr), flush_f32_input(b1, fpscr), fpscr, lane0, lane1)) {
        ppc_commit_paired_result(context, target, lane0, lane1, false,
            (descriptor & (1u << 19u)) != 0u, services, guest_pc);
        return;
    }
    const auto r0 = indexed_binary(op, a0, b0, fpscr);
    const auto r1 = indexed_binary(op, a1, b1, fpscr);
    indexed_commit(context, target, r0, r1, (descriptor & (1u << 19u)) != 0u,
        services, guest_pc);
}

// Private complete paired-ternary four-register boundary. Descriptor fields:
// target[4:0], multiplicand[9:5], multiplier[14:10], addend[19:15], op[21:20],
// multiplier-lane0-is-PS1[22], multiplier-lane1-is-PS1[23], Rc[24].
namespace {
template<class Output>
GALAXY_ALWAYS_INLINE bool indexed_native_f32_fma_wide_fast(
    float multiplicand,
    float multiplier,
    float addend,
    Output& result) {
    const double product =
        static_cast<double>(multiplicand) * static_cast<double>(multiplier);
    const double addend_value = static_cast<double>(addend);
    const double sum = product + addend_value;
    const double virtual_addend = sum - product;
    const double error =
        (product - (sum - virtual_addend)) + (addend_value - virtual_addend);

    // Binary64 exponent fields 898 / 1150 are 2^-125 / 2^127.
    const std::uint64_t sum_magnitude =
        std::bit_cast<std::uint64_t>(sum) & 0x7FFFFFFFFFFFFFFFull;
    if (sum_magnitude < (898ull << 52) || sum_magnitude >= (1150ull << 52)) {
        return false;
    }

    const float rounded = static_cast<float>(sum);
    std::uint32_t bits = std::bit_cast<std::uint32_t>(rounded);
    const double rounded_value = static_cast<double>(rounded);
    if (error != 0.0 && rounded_value != sum) {
        // Neighbor of `rounded` on the side of `sum`; the range guard keeps
        // both normal.
        const bool away_from_zero = std::fabs(sum) > std::fabs(rounded_value);
        const std::uint32_t neighbor_bits = away_from_zero ? bits + 1u : bits - 1u;
        const double neighbor_value =
            static_cast<double>(std::bit_cast<float>(neighbor_bits));
        if (sum == (rounded_value + neighbor_value) * 0.5) {
            const bool true_value_above = error > 0.0;
            const bool neighbor_above = neighbor_value > rounded_value;
            if (true_value_above == neighbor_above) {
                bits = neighbor_bits;
            }
        }
    }

    const double result_value = static_cast<double>(std::bit_cast<float>(bits));
    const bool inexact = error != 0.0 || result_value != sum;
    bool rounded_up = false;
    if (result_value != sum) {
        rounded_up = std::fabs(result_value) > std::fabs(sum);
    } else if (error != 0.0) {
        rounded_up = (error < 0.0) != (result_value < 0.0);
    }
    result = indexed_make_f32_native_result<Output>(bits,
        inexact ? kXx : 0u, classify_f32(bits), inexact, rounded_up);
    return true;
}

template<class Output>
GALAXY_ALWAYS_INLINE bool indexed_native_f32_ternary_fast(
    PpcFloatTernaryOperation operation,
    std::uint32_t multiplicand,
    std::uint32_t multiplier,
    std::uint32_t addend,
    std::uint32_t fpscr,
    Output& result) {
    if ((fpscr & 0x3u) != 0 || !f32_is_finite(multiplicand) ||
        !f32_is_finite(multiplier) || !f32_is_finite(addend) ||
        !f32_is_zero_or_normal(multiplicand) ||
        !f32_is_zero_or_normal(multiplier) ||
        !f32_is_zero_or_normal(addend)) {
        return false;
    }

    const float multiplicand_value = std::bit_cast<float>(multiplicand);
    const float multiplier_value = std::bit_cast<float>(multiplier);
    const float addend_value = std::bit_cast<float>(addend);
    const float adjusted_addend =
        ternary_subtracts(operation) ? -addend_value : addend_value;
    if (!f32_fma_exact_in_double(multiplicand, multiplier, addend)) {
        // Wide exponent gap (e.g. position += velocity * dt): every operand is
        // a nonzero normal here, and the wide path keeps the software result.
        if (!indexed_native_f32_fma_wide_fast<Output>(
                multiplicand_value, multiplier_value, adjusted_addend, result)) {
            return false;
        }
    } else {
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

    result = indexed_native_f32_normal_or_zero_result<Output>(rounded_bits, exact);
    }
    if (ternary_negates(operation)) {
        // Eligibility already proves a normal or signed-zero result. Negation
        // changes only its sign: the widened encoding keeps its magnitude,
        // while FPRF swaps +/-normal or +/-zero. FI/FR describe the original
        // rounding and remain unchanged, just as in the software path.
        static_assert(std::is_same_v<Output, std::uint64_t>);
        const auto status = std::uint32_t(result >> 32u);
        result ^= 0x80000000ull | (std::uint64_t(
            (status & 0x00002000u) != 0u ? 0x00010000u : 0x0000C000u) << 32u);
    }
    return true;
}

template<class Output>
GALAXY_ALWAYS_INLINE Output indexed_ternary_impl(
    PpcFloatTernaryOperation operation,
    std::uint32_t multiplicand,
    std::uint32_t multiplier,
    std::uint32_t addend,
    std::uint32_t fpscr) {
    multiplicand = flush_f32_input(multiplicand, fpscr);
    multiplier = flush_f32_input(multiplier, fpscr);
    addend = flush_f32_input(addend, fpscr);
    Output fast_result{};
    if (indexed_native_f32_ternary_fast<Output>(
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
                : (f32_is_nan(addend) ? addend : multiplier);
        const std::uint32_t result = source | 0x00400000u;
        return indexed_make_f32_native_result<Output>(
            result, invalid, classify_f32(result), false, false);
    }
    if (invalid != 0) {
        constexpr std::uint32_t kCanonicalNan = 0x7FC00000u;
        return indexed_make_f32_native_result<Output>(kCanonicalNan,
            invalid, classify_f32(kCanonicalNan), false, false);
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
    return indexed_make_f32_native_result<Output>(bits,
        map_softfloat_exceptions(primary.exceptions), classify_f32(bits),
        inexact, rounded_up);
}


GALAXY_NOINLINE std::uint64_t indexed_ternary(
    PpcFloatTernaryOperation op, std::uint32_t a, std::uint32_t c,
    std::uint32_t b, std::uint32_t fpscr) {
    return indexed_ternary_impl<std::uint64_t>(op, a, c, b, fpscr);
}
} // namespace

void ppc_execute_indexed_paired_ternary(PpcContext* context,
    std::uint32_t descriptor, const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    const auto target = descriptor & 31u;
    const auto multiplicand = (descriptor >> 5u) & 31u;
    const auto multiplier = (descriptor >> 10u) & 31u;
    const auto addend = (descriptor >> 15u) & 31u;
    const auto op = static_cast<PpcFloatTernaryOperation>((descriptor >> 20u) & 3u);
    require_paired_single_mode(context, false, services, guest_pc);
    // Exact original check order and alias-safe source capture. Architectural
    // FPU retry/checkpoints remain at each original generated call site.
    const auto a0 = require_single_precision_bits(context->fpr_bits[multiplicand], services, guest_pc);
    const auto a1 = require_single_precision_bits(context->ps1_bits[multiplicand], services, guest_pc);
    const auto c0 = require_single_precision_bits((descriptor & (1u << 22u)) ?
        context->ps1_bits[multiplier] : context->fpr_bits[multiplier], services, guest_pc);
    const auto c1 = require_single_precision_bits((descriptor & (1u << 23u)) ?
        context->ps1_bits[multiplier] : context->fpr_bits[multiplier], services, guest_pc);
    const auto b0 = require_single_precision_bits(context->fpr_bits[addend], services, guest_pc);
    const auto b1 = require_single_precision_bits(context->ps1_bits[addend], services, guest_pc);
    const auto fpscr = context->fpscr;
    const auto r0 = indexed_ternary(op, a0, c0, b0, fpscr);
    const auto r1 = indexed_ternary(op, a1, c1, b1, fpscr);
    indexed_commit(context, target, r0, r1, (descriptor & (1u << 24u)) != 0u,
        services, guest_pc);
}

}  // namespace galaxy
