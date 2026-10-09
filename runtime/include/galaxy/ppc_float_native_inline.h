// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Private implementation shared by the outlined float library and opt-in
// generated scalar lowering. The math, status and commit bodies were moved
// from ppc_float.cpp without changing their predicates or operation order.
#include "galaxy/native_api.h"

namespace galaxy {
namespace float_native_detail {

constexpr std::uint32_t kFx = 0x80000000u;
constexpr std::uint32_t kFex = 0x40000000u;
constexpr std::uint32_t kVx = 0x20000000u;
constexpr std::uint32_t kOx = 0x10000000u;
constexpr std::uint32_t kUx = 0x08000000u;
constexpr std::uint32_t kZx = 0x04000000u;
constexpr std::uint32_t kXx = 0x02000000u;
constexpr std::uint32_t kVxSnan = 0x01000000u;
constexpr std::uint32_t kVxIsi = 0x00800000u;
constexpr std::uint32_t kVxIdi = 0x00400000u;
constexpr std::uint32_t kVxZdz = 0x00200000u;
constexpr std::uint32_t kVxImz = 0x00100000u;
constexpr std::uint32_t kVxSqrt = 0x00000200u;
constexpr std::uint32_t kVxCvi = 0x00000100u;
constexpr std::uint32_t kFr = 0x00040000u;
constexpr std::uint32_t kFi = 0x00020000u;
constexpr std::uint32_t kFprfMask = 0x0001F000u;
constexpr std::uint32_t kInvalidDetailMask = 0x01F80700u;
constexpr std::uint32_t kVe = 0x00000080u;
constexpr std::uint32_t kOe = 0x00000040u;
constexpr std::uint32_t kUe = 0x00000020u;
constexpr std::uint32_t kZe = 0x00000010u;
constexpr std::uint32_t kXe = 0x00000008u;
constexpr std::uint32_t kNi = 0x00000004u;

GALAXY_ALWAYS_INLINE bool f32_is_finite(std::uint32_t bits) {
    return (bits & 0x7F800000u) != 0x7F800000u;
}

GALAXY_ALWAYS_INLINE bool f32_is_zero_or_normal(std::uint32_t bits) {
    const std::uint32_t exponent = bits & 0x7F800000u;
    return exponent != 0 || (bits & 0x007FFFFFu) == 0;
}

GALAXY_ALWAYS_INLINE bool f32_is_normal_or_zero_result(std::uint32_t bits) {
    const std::uint32_t exponent = bits & 0x7F800000u;
    return (exponent != 0 && exponent != 0x7F800000u) ||
           (bits & 0x7FFFFFFFu) == 0;
}

GALAXY_ALWAYS_INLINE std::uint32_t f32_effective_exponent(std::uint32_t bits) {
    return (bits >> 23) & 0xFFu;
}

GALAXY_ALWAYS_INLINE bool f32_add_sub_exact_in_double(std::uint32_t left, std::uint32_t right) {
    const std::uint32_t left_magnitude = left & 0x7FFFFFFFu;
    const std::uint32_t right_magnitude = right & 0x7FFFFFFFu;
    if (left_magnitude == 0 || right_magnitude == 0) {
        return true;
    }
    const std::uint32_t left_exponent = f32_effective_exponent(left);
    const std::uint32_t right_exponent = f32_effective_exponent(right);
    const std::uint32_t distance =
        left_exponent > right_exponent ? left_exponent - right_exponent
                                       : right_exponent - left_exponent;
    return distance <= 28u;
}

GALAXY_ALWAYS_INLINE std::uint32_t classify_f32(std::uint32_t bits) {
    const bool negative = (bits >> 31) != 0;
    const std::uint32_t exponent = (bits >> 23) & 0xFFu;
    const std::uint32_t fraction = bits & 0x007FFFFFu;
    if (exponent == 0xFFu) {
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

GALAXY_ALWAYS_INLINE bool native_f32_binary_inputs(
    std::uint32_t left,
    std::uint32_t right) {
    return f32_is_finite(left) && f32_is_finite(right) &&
           f32_is_zero_or_normal(left) && f32_is_zero_or_normal(right);
}

GALAXY_ALWAYS_INLINE bool native_f32_result_is_eligible(std::uint32_t bits) {
    // A value just below minimum normal can round up to minimum normal
    // while the software path still records underflow. Preserve that
    // boundary's tininess decision in every native binary32 caller.
    return f32_is_normal_or_zero_result(bits) &&
           (bits & 0x7FFFFFFFu) != 0x00800000u;
}

GALAXY_ALWAYS_INLINE PpcFloatResult native_f32_normal_or_zero_result(
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
    return PpcFloatResult{
        widened,
        exception_bits,
        fprf,
        inexact,
        rounded_up};
}

// Add/subtract/multiply core for operands the caller has already proved to be
// finite normal-or-zero binary32 under nearest-even rounding.
template <PpcFloatBinaryOperation Operation>
GALAXY_ALWAYS_INLINE bool native_f32_binary_core(
    std::uint32_t left,
    std::uint32_t right,
    PpcFloatResult& result) {
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
            result = PpcFloatResult{
                widen_f32_bits(dominant), kXx, classify_f32(dominant), true,
                ((left ^ adjusted_right) & 0x80000000u) != 0u};
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
    result = native_f32_normal_or_zero_result(rounded_bits, exact);
    return true;
}

GALAXY_ALWAYS_INLINE bool native_f32_binary_fast(
    PpcFloatBinaryOperation operation,
    std::uint32_t left,
    std::uint32_t right,
    std::uint32_t fpscr,
    PpcFloatResult& result) {
    // NI inputs have already been flushed by the caller. Normal/zero output
    // needs no NI adjustment; nonzero subnormal results retain the software
    // path below. NI therefore does not exclude this exact native path.
    if ((fpscr & 0x3u) != 0 || operation == PpcFloatBinaryOperation::Divide ||
        !native_f32_binary_inputs(left, right)) {
        return false;
    }
    switch (operation) {
    case PpcFloatBinaryOperation::Add:
        return native_f32_binary_core<PpcFloatBinaryOperation::Add>(
            left, right, result);
    case PpcFloatBinaryOperation::Subtract:
        return native_f32_binary_core<PpcFloatBinaryOperation::Subtract>(
            left, right, result);
    case PpcFloatBinaryOperation::Multiply:
        return native_f32_binary_core<PpcFloatBinaryOperation::Multiply>(
            left, right, result);
    case PpcFloatBinaryOperation::Divide:
        return false;
    }
    return false;
}

GALAXY_ALWAYS_INLINE bool exception_is_enabled(std::uint32_t fpscr, std::uint32_t exception_bits) {
    // OX/UX/ZX/XX sit 22 bits above their corresponding enables. Invalid
    // delivery depends on a detail bit, not an independently supplied VX.
    const std::uint32_t raised_enables =
        ((exception_bits >> 22u) & (kOe | kUe | kZe | kXe)) |
        ((exception_bits & kInvalidDetailMask) != 0 ? kVe : 0u);
    return (fpscr & raised_enables) != 0;
}

GALAXY_ALWAYS_INLINE void apply_result_status(PpcContext* context, const PpcFloatResult& result) {
    std::uint32_t fpscr = context->fpscr;
    // Common steady state: every raised exception bit is already sticky in
    // FPSCR (so FX cannot change), no invalid-operation detail is recorded
    // and no exception is enabled (so VX and FEX are clear). The general
    // sequence below then reduces to replacing VX/FEX/FPRF/FR/FI, which this
    // branch does directly with the same result.
    constexpr std::uint32_t kEnableBits = kVe | kOe | kUe | kZe | kXe;
    if ((result.exception_bits & ~fpscr) == 0u &&
        (fpscr & (kInvalidDetailMask | kEnableBits)) == 0u) [[likely]] {
        context->fpscr = (fpscr & ~(kFex | kVx | kFprfMask | kFr | kFi)) |
            result.fprf | (result.inexact ? kFi : 0u) |
            (result.rounded_up ? kFr : 0u);
        return;
    }
    if ((result.exception_bits & ~fpscr) != 0) {
        fpscr |= kFx;
    }
    fpscr |= result.exception_bits;
    fpscr = (fpscr & ~kVx) | ((fpscr & kInvalidDetailMask) != 0 ? kVx : 0u);
    // All five summary exceptions now align with their enable bits after
    // this shift. Recompute both summaries even if the incoming FPSCR was
    // inconsistent, and publish the complete status with one context store.
    const bool enabled =
        ((fpscr >> 22u) & fpscr & (kVe | kOe | kUe | kZe | kXe)) != 0;
    fpscr = (fpscr & ~(kFex | kFprfMask | kFr | kFi)) |
        (enabled ? kFex : 0u) | result.fprf |
        (result.inexact ? kFi : 0u) | (result.rounded_up ? kFr : 0u);
    context->fpscr = fpscr;
}

GALAXY_ALWAYS_INLINE void commit_scalar_result(
    PpcContext* context,
    std::uint32_t target,
    const PpcFloatResult& result,
    bool duplicate_single,
    bool record,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    apply_result_status(context, result);
    if (exception_is_enabled(context->fpscr, result.exception_bits)) {
        guest_execution_fault(
            services,
            guest_pc,
            "enabled floating-point exception requires native trap delivery");
    }
    context->fpr_bits[target] = result.bits;
    if (duplicate_single && paired_singles_enabled(context)) {
        context->ps1_bits[target] = result.bits;
    }
    if (record) {
        update_cr1_from_fpscr(context);
    }
}


}  // namespace float_native_detail

// False means no guest context field was changed. The generated instruction
// then executes its complete original arithmetic and commit. Captured source
// bits make target/source aliases safe. Enabled exceptions use the same
// status publication and fault-before-register-write contract as the library.
template <PpcFloatBinaryOperation Operation>
// Private code-size/throughput comparison. Share the exact fast path across
// translated call sites; arithmetic, failed-guard and commit semantics stay
// identical. Keep the established inlined build until matched validation.
#if defined(GALAXY_OUTLINE_SCALAR_FP_FAST_PATH) && GALAXY_OUTLINE_SCALAR_FP_FAST_PATH
GALAXY_NOINLINE inline
#else
GALAXY_ALWAYS_INLINE
#endif
bool try_commit_widened_scalar_binary(
    PpcContext* context,
    std::uint32_t target,
    std::uint64_t left,
    std::uint64_t right,
    bool record,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    static_assert(Operation == PpcFloatBinaryOperation::Add ||
        Operation == PpcFloatBinaryOperation::Subtract ||
        Operation == PpcFloatBinaryOperation::Multiply);
    const std::uint32_t fpscr = context->fpscr;
    std::uint32_t left_f32 = 0u;
    std::uint32_t right_f32 = 0u;
    if ((fpscr & 3u) != 0u ||
        !float_native_detail::try_narrow_normal_or_zero_widened_f32(left, left_f32) ||
        !float_native_detail::try_narrow_normal_or_zero_widened_f32(right, right_f32)) {
        return false;
    }
    PpcFloatResult result{};
    // Classify before any NI result flush. A native subnormal that the old
    // path would flush to signed zero must still choose that complete path.
    // The narrowing above already proved both operands finite normal-or-zero
    // under nearest-even rounding, so the core skips those repeated checks.
    if (!float_native_detail::native_f32_binary_core<Operation>(
            left_f32, right_f32, result)) {
        return false;
    }
    float_native_detail::commit_scalar_result(
        context, target, result, true, record, services, guest_pc);
    return true;
}

}  // namespace galaxy
