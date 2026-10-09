#pragma once

#include <cstdint>

namespace galaxy {

inline constexpr std::uint8_t kPpcFloatExceptionInexact = 1;

struct PpcContext;
struct NativeServicesV1;

enum class PpcFloatBinaryOperation : std::uint8_t {
    Add,
    Subtract,
    Multiply,
    Divide,
};

enum class PpcFloatTernaryOperation : std::uint8_t {
    MultiplyAdd,
    MultiplySubtract,
    NegativeMultiplyAdd,
    NegativeMultiplySubtract,
};

struct PpcFloat32Result {
    std::uint32_t bits{};
    std::uint8_t exceptions{};
};

struct PpcFloatResult {
    std::uint64_t bits{};
    std::uint32_t exception_bits{};
    std::uint32_t fprf{};
    bool inexact{};
    bool rounded_up{};
};

PpcFloat32Result ppc_narrow_f64_to_f32(std::uint64_t bits, std::uint32_t fpscr);

namespace float_native_detail {

// Loads and binary32 arithmetic store exact widened normals or signed zero.
// This fast path excludes NaN, infinity, subnormals and any binary64 value
// that needs rounding; those use the full f64-to-f32 arithmetic path.
#if defined(_MSC_VER)
__forceinline
#else
inline __attribute__((always_inline))
#endif
bool try_narrow_normal_or_zero_widened_f32(
    std::uint64_t bits,
    std::uint32_t& output) {
    if ((bits & 0x7FFFFFFFFFFFFFFFull) == 0u) {
        output = static_cast<std::uint32_t>(bits >> 32u) & 0x80000000u;
        return true;
    }
    const auto exponent = static_cast<std::uint32_t>((bits >> 52u) & 0x7FFu);
    if (exponent < 897u || exponent > 1150u ||
        (bits & 0x1FFFFFFFull) != 0u) {
        return false;
    }
    output = (static_cast<std::uint32_t>(bits >> 32u) & 0x80000000u) |
        ((exponent - 896u) << 23u) |
        (static_cast<std::uint32_t>(bits >> 29u) & 0x007FFFFFu);
    return true;
}


}  // namespace float_native_detail

PpcFloatResult ppc_round_f64_to_f32(std::uint64_t bits, std::uint32_t fpscr);

PpcFloatResult ppc_f64_to_i32_round_zero(
    std::uint64_t bits,
    std::uint32_t fpscr);

PpcFloatResult ppc_f64_binary(
    PpcFloatBinaryOperation operation,
    std::uint64_t left,
    std::uint64_t right,
    std::uint32_t fpscr);

PpcFloatResult ppc_f32_binary(
    PpcFloatBinaryOperation operation,
    std::uint32_t left,
    std::uint32_t right,
    std::uint32_t fpscr);

// Generated paired binary boundary. Descriptor uses validated register indices:
// target[4:0], left[9:5], right[14:10], operation[16:15], right-lane0 PS1[17],
// right-lane1 PS1[18], Rc[19]. MSR/FPU retry remains at the generated site.
// HID2/source checks, status/fault delivery and output ordering live here.
void ppc_execute_indexed_paired_binary(PpcContext* context,
    std::uint32_t descriptor, const NativeServicesV1* services,
    std::uint32_t guest_pc);

// Complete generated paired ternary boundary. Descriptor: target[4:0],
// multiplicand[9:5], multiplier[14:10], addend[19:15], operation[21:20],
// multiplier lane0/1 selects PS1[22:23], Rc[24]. Source/fault ordering and
// both outputs remain atomic with respect to enabled arithmetic exceptions.
// MSR/FPU retry and guest checkpoints remain at the generated instruction.
void ppc_execute_indexed_paired_ternary(PpcContext* context,
    std::uint32_t descriptor, const NativeServicesV1* services,
    std::uint32_t guest_pc);

PpcFloatResult ppc_f64_binary_to_f32(
    PpcFloatBinaryOperation operation,
    std::uint64_t left,
    std::uint64_t right,
    std::uint32_t fpscr);

PpcFloatResult ppc_f64_ternary(
    PpcFloatTernaryOperation operation,
    std::uint64_t multiplicand,
    std::uint64_t multiplier,
    std::uint64_t addend,
    std::uint32_t fpscr);

PpcFloatResult ppc_f32_ternary(
    PpcFloatTernaryOperation operation,
    std::uint32_t multiplicand,
    std::uint32_t multiplier,
    std::uint32_t addend,
    std::uint32_t fpscr);

// Scalar single FMA consumes binary64 A/B and a 25-significant-bit C;
// only the fused result is rounded to binary32.
PpcFloatResult ppc_f64_ternary_to_f32(
    PpcFloatTernaryOperation operation,
    std::uint64_t multiplicand,
    std::uint64_t multiplier,
    std::uint64_t addend,
    std::uint32_t fpscr);

// Capture all sources before the arithmetic/commit boundary so destination
// aliasing and enabled exceptions retain the architectural write order.
void ppc_commit_scalar_single_ternary(
    PpcContext* context,
    std::uint32_t target,
    PpcFloatTernaryOperation operation,
    std::uint32_t multiplicand_register,
    std::uint32_t multiplier_register,
    std::uint32_t addend_register,
    bool record,
    const NativeServicesV1* services,
    std::uint32_t guest_pc);

PpcFloatResult ppc_f32_passthrough(std::uint32_t bits);

PpcFloatResult ppc_f32_reciprocal_estimate(
    std::uint32_t bits,
    std::uint32_t fpscr);

// Scalar fres consumes binary64; finite results are widened binary32 estimates.
// NaNs retain the full quieted binary64 payload. Ordinary FI/FR are preserved.
PpcFloatResult ppc_f64_reciprocal_estimate(
    std::uint64_t bits,
    std::uint32_t fpscr);

PpcFloatResult ppc_f32_reciprocal_sqrt_estimate(
    std::uint32_t bits,
    std::uint32_t fpscr);

PpcFloatResult ppc_f64_reciprocal_sqrt_estimate(
    std::uint64_t bits,
    std::uint32_t fpscr);

void ppc_commit_scalar_result(
    PpcContext* context,
    std::uint32_t target,
    const PpcFloatResult& result,
    bool duplicate_single,
    bool record,
    const NativeServicesV1* services,
    std::uint32_t guest_pc);

void ppc_commit_paired_result(
    PpcContext* context,
    std::uint32_t target,
    const PpcFloatResult& lane0,
    const PpcFloatResult& lane1,
    bool status_from_lane1,
    bool record,
    const NativeServicesV1* services,
    std::uint32_t guest_pc);

// Estimate FI/FR are retained only when both inputs are ordinary. Arithmetic
// paired commit instead ORs each lane's newly computed FI/FR.
void ppc_commit_paired_estimate_result(
    PpcContext* context, std::uint32_t target,
    const PpcFloatResult& lane0, const PpcFloatResult& lane1,
    bool record, const NativeServicesV1* services, std::uint32_t guest_pc);

// The generated caller checks paired-single availability and captures all
// four narrowed source lanes before entering this arithmetic/commit boundary.
// Captured inputs remain valid when target aliases either source register.
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
    std::uint32_t guest_pc);

}  // namespace galaxy
