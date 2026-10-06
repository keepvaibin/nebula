#include "galaxy/ppc_float.h"
#include "galaxy/ppc_paired_float.h"

#include "galaxy/native_api.h"

extern "C" {
#include "platform.h"
#include "softfloat.h"
}

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <iterator>
#include <string_view>

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

bool stored_single_matches_bits_and_preserves_context(
    std::uint64_t source,
    std::uint32_t fpscr,
    std::uint32_t expected) {
    std::array<std::byte, 16> bytes{};
    galaxy::GuestMemoryV1 memory{};
    memory.fast_regions[8].host_base = bytes.data();
    memory.fast_regions[8].size = static_cast<std::uint32_t>(bytes.size());
    // Guard bytes catch an unintended width/offset change in the actual store.
    bytes.fill(std::byte{0xA5});
    galaxy::PpcContext context{};
    context.fpr_bits[5] = source;
    context.ps1_bits[5] = 0x1122334455667788ull;
    context.fpscr = fpscr;
    context.cr = 0x12345678u;
    context.pc = 0x80001234u;
    const auto before = context;
    galaxy::store_fpr_single(&context, 5u, &memory, 0x80000004u, nullptr, context.pc);
    std::uint32_t stored = 0u;
    std::memcpy(&stored, bytes.data() + 4u, sizeof(stored));
    return expect(galaxy::byte_swap_u32(stored) == expected &&
                      std::memcmp(&before, &context, sizeof(context)) == 0 &&
                      bytes[3] == std::byte{0xA5} && bytes[8] == std::byte{0xA5},
                  "actual scalar store preserves reference bits, complete context and adjacent bytes");
}

bool exact_binary32_store_conversions() {
    // Use the host's exact binary32-to-binary64 widening as an independent
    // oracle, rather than the bit manipulation used by the runtime guard.
    const auto check = [](std::uint32_t bits) {
        const auto wide = std::bit_cast<std::uint64_t>(
            static_cast<double>(std::bit_cast<float>(bits)));
        for (std::uint32_t mode = 0u; mode < 8u; ++mode) {
            // Cover all RN/NI combinations with existing sticky flags and
            // exception enables set: an exact store must not raise any flag.
            const auto result = galaxy::ppc_narrow_f64_to_f32(
                wide, 0xFFF807F8u | mode);
            if (result.bits != bits || result.exceptions != 0u ||
                !stored_single_matches_bits_and_preserves_context(wide, 0xFFF807F8u | mode, bits)) {
                std::cerr << "FAILED: exact binary32 store bits=0x"
                          << std::hex << bits << " mode=" << mode
                          << " result=0x" << result.bits << " flags=0x"
                          << static_cast<unsigned>(result.exceptions)
                          << std::dec << '\n';
                return false;
            }
        }
        return true;
    };
    constexpr std::array<std::uint32_t, 7> fractions{
        0u, 1u, 2u, 0x003FFFFFu, 0x00400000u, 0x007FFFFEu, 0x007FFFFFu};
    for (std::uint32_t sign : {0u, 0x80000000u}) {
        for (std::uint32_t exponent = 0u; exponent < 255u; ++exponent) {
            for (std::uint32_t fraction : fractions) {
                if (!check(sign | (exponent << 23u) | fraction)) {
                    return false;
                }
            }
        }
        if (!check(sign | 0x7F800000u)) {
            return false;
        }
    }
    std::uint32_t random = 0x4D475231u;
    for (std::uint32_t i = 0u; i < 65536u; ++i) {
        random ^= random << 13u;
        random ^= random >> 17u;
        random ^= random << 5u;
        if ((random & 0x7F800000u) != 0x7F800000u && !check(random)) {
            return false;
        }
    }
    // Corrupt every low bit excluded by exact widening. Check the public
    // store path against independent SoftFloat rounding/flags, including
    // boundary/subnormal/infinity/NaN inputs and all RN/NI combinations.
    {
        struct RestoreGlobals {
            std::uint_fast8_t rounding = softfloat_roundingMode;
            std::uint_fast8_t tininess = softfloat_detectTininess;
            std::uint_fast8_t flags = softfloat_exceptionFlags;
            ~RestoreGlobals() {
                softfloat_roundingMode = rounding;
                softfloat_detectTininess = tininess;
                softfloat_exceptionFlags = flags;
            }
        } restore;
        constexpr std::array<std::uint64_t, 8> bases{
            0u, 0x3810000000000000ull, 0x380FFFFFC0000000ull,
            0x3FF0000000000000ull, 0x47EFFFFFE0000000ull,
            0x47F0000000000000ull, 0x7FF0000000000000ull,
            0x7FF8000000000000ull};
        constexpr std::array<std::uint_fast8_t, 4> rounds{
            softfloat_round_near_even, softfloat_round_minMag,
            softfloat_round_max, softfloat_round_min};
        for (auto base : bases) for (std::uint64_t sign : {0ull, 0x8000000000000000ull}) {
            for (unsigned bit = 0; bit < 29; ++bit) for (unsigned mode = 0; mode < 8; ++mode) {
                const auto source = base | sign | (1ull << bit);
                softfloat_roundingMode = rounds[mode & 3u];
                softfloat_detectTininess = softfloat_tininess_afterRounding;
                softfloat_exceptionFlags = 0;
                const auto reference = f64_to_f32(float64_t{source});
                const auto flags = softfloat_exceptionFlags;
                softfloat_roundingMode = softfloat_round_max;
                softfloat_detectTininess = softfloat_tininess_beforeRounding;
                softfloat_exceptionFlags = softfloat_flag_invalid | softfloat_flag_inexact;
                const auto actual = galaxy::ppc_narrow_f64_to_f32(source, mode);
                if (!stored_single_matches_bits_and_preserves_context(source, mode, reference.v) ||
                    !expect(actual.bits == reference.v && actual.exceptions == flags,
                            "single store rejects non-widened encodings with reference rounding/flags") ||
                    !expect(softfloat_roundingMode == softfloat_round_max &&
                                softfloat_detectTininess == softfloat_tininess_beforeRounding &&
                                softfloat_exceptionFlags == (softfloat_flag_invalid | softfloat_flag_inexact),
                            "single store preserves caller SoftFloat globals")) {
                    return false;
                }
            }
        }
    }
    // A widened signaling NaN must not be mistaken for an exact, flag-free
    // value. Also retain a non-widened NaN whose low payload bits are lost.
    constexpr std::uint8_t kInvalidConversionException = 16u;
    for (std::uint32_t mode = 0u; mode < 4u; ++mode) {
        const auto signaling = galaxy::ppc_narrow_f64_to_f32(
            0x7FF0000020000000ull, mode);
        const auto quiet = galaxy::ppc_narrow_f64_to_f32(
            0xFFF8000000000001ull, mode);
        if (!expect(signaling.bits == 0x7FC00001u &&
                        signaling.exceptions == kInvalidConversionException,
                    "single store quiets signaling NaN and raises invalid") ||
            !expect(quiet.bits == 0xFFC00000u && quiet.exceptions == 0u,
                    "single store preserves quiet NaN sign and payload")) {
            return false;
        }
    }
    return true;
}

std::uint32_t reference_flush_subnormal(std::uint32_t bits) {
    return ((bits >> 23u) & 0xFFu) == 0u ? bits & 0x80000000u : bits;
}

bool binary32_matches_software_reference_with_ni() {
    const auto saved_rounding = softfloat_roundingMode;
    const auto saved_tininess = softfloat_detectTininess;
    const auto saved_exceptions = softfloat_exceptionFlags;
    softfloat_detectTininess = softfloat_tininess_afterRounding;
    const auto check = [](std::uint32_t a, std::uint32_t b, std::uint32_t rn = 0u) {
        constexpr std::array<std::uint_fast8_t, 4> rounding_modes{
            softfloat_round_near_even, softfloat_round_minMag,
            softfloat_round_max, softfloat_round_min};
        for (std::uint32_t mode : {0u, 4u}) {
            const float32_t left{mode ? reference_flush_subnormal(a) : a};
            const float32_t right{mode ? reference_flush_subnormal(b) : b};
            for (const auto operation : {
                     galaxy::PpcFloatBinaryOperation::Add,
                     galaxy::PpcFloatBinaryOperation::Subtract,
                     galaxy::PpcFloatBinaryOperation::Multiply}) {
                const auto reference = [&]() {
                    switch (operation) {
                        case galaxy::PpcFloatBinaryOperation::Add:
                            return f32_add(left, right);
                        case galaxy::PpcFloatBinaryOperation::Subtract:
                            return f32_sub(left, right);
                        default:
                            return f32_mul(left, right);
                    }
                };
                softfloat_roundingMode = rounding_modes[rn];
                softfloat_exceptionFlags = 0;
                const auto primary = reference();
                const auto flags = softfloat_exceptionFlags;
                softfloat_roundingMode = softfloat_round_minMag;
                softfloat_exceptionFlags = 0;
                const auto truncated = reference();
                const auto expected_bits = mode
                    ? reference_flush_subnormal(primary.v) : primary.v;
                const std::uint32_t expected_exceptions =
                    ((flags & softfloat_flag_inexact) != 0 ? 0x02000000u : 0u) |
                    ((flags & softfloat_flag_underflow) != 0 ? 0x08000000u : 0u) |
                    ((flags & softfloat_flag_overflow) != 0 ? 0x10000000u : 0u);
                const bool inexact = (flags & softfloat_flag_inexact) != 0;
                const bool rounded_up = inexact && primary.v != truncated.v;
                // A native admission or software fallback must preserve the
                // caller's SoftFloat globals, even when NI changes an input.
                softfloat_roundingMode = softfloat_round_max;
                softfloat_detectTininess = softfloat_tininess_beforeRounding;
                softfloat_exceptionFlags = softfloat_flag_invalid | softfloat_flag_inexact;
                const auto actual = galaxy::ppc_f32_binary(operation, a, b, mode | rn);
                if (!expect(softfloat_roundingMode == softfloat_round_max &&
                                softfloat_detectTininess == softfloat_tininess_beforeRounding &&
                                softfloat_exceptionFlags ==
                                    (softfloat_flag_invalid | softfloat_flag_inexact),
                            "binary32 arithmetic preserves caller SoftFloat globals")) {
                    return false;
                }
                softfloat_detectTininess = softfloat_tininess_afterRounding;
                if (actual.bits != galaxy::widen_f32_bits(expected_bits) ||
                    actual.exception_bits != expected_exceptions ||
                    actual.inexact != inexact || actual.rounded_up != rounded_up ||
                    actual.fprf != galaxy::ppc_f32_passthrough(expected_bits).fprf) {
                    std::cerr << "FAILED: binary32 reference a=0x" << std::hex
                              << a << " b=0x" << b << " mode=" << (mode | rn)
                              << " operation=" << static_cast<unsigned>(operation)
                              << " expected=0x" << expected_bits
                              << " actual-wide=0x" << actual.bits
                              << " expected-status=" << expected_exceptions
                              << " actual-status=" << actual.exception_bits
                              << std::dec << '\n';
                    return false;
                }
            }
        }
        return true;
    };
    bool passed = true;
    constexpr std::array<std::uint32_t, 16> cases{
        0u, 0x80000000u, 1u, 0x80000001u, 0x007FFFFFu, 0x807FFFFFu,
        0x00800000u, 0x80800000u, 0x00800001u, 0x80800001u,
        0x3F000000u, 0x3F800000u, 0x3F800001u, 0xBF800001u,
        0x7F7FFFFFu, 0xFF7FFFFFu};
    for (auto a : cases) for (auto b : cases) {
        for (std::uint32_t rn = 0u; rn < 4u; ++rn) passed &= check(a, b, rn);
    }
    // Multiplication by one half spans the largest subnormal, a midpoint
    // that rounds up to minimum normal, minimum normal, and its successor.
    // Check both operand orders and signs under every RN/NI combination;
    // derive flags from SoftFloat rather than the rounded result's exponent.
    for (auto magnitude : {0x00FFFFFEu, 0x00FFFFFFu, 0x01000000u, 0x01000001u}) {
        for (auto sign : {0u, 0x80000000u}) {
            for (std::uint32_t rn = 0u; rn < 4u; ++rn) {
                passed &= check(magnitude | sign, 0x3F000000u, rn);
                passed &= check(0x3F000000u, magnitude | sign, rn);
            }
        }
    }
    // Exercise both sides of the exponent-distance guard, both dominant
    // operands, binade boundaries, and all effective addition/subtraction
    // signs. A distant nonzero operand must still set FI and the right FR.
    for (auto distance : {27u, 28u, 29u, 30u, 100u}) {
        for (auto fraction : {0u, 1u, 0x007FFFFFu}) {
            for (auto sign_a : {0u, 0x80000000u}) {
                for (auto sign_b : {0u, 0x80000000u}) {
                    const auto a = sign_a | 0x3F800000u | fraction;
                    const auto b = sign_b | ((127u - distance) << 23u) | fraction;
                    passed &= check(a, b);
                    passed &= check(b, a);
                }
            }
        }
    }
    std::uint32_t random = 0x4E494650u;
    const auto next_finite = [&]() {
        random ^= random << 13u;
        random ^= random >> 17u;
        random ^= random << 5u;
        return (random & 0x7F800000u) == 0x7F800000u
            ? random & ~0x00800000u : random;
    };
    for (std::uint32_t i = 0u; passed && i < 65536u; ++i) {
        const auto a = next_finite();
        const auto b = next_finite();
        passed &= check(a, b);
    }
    softfloat_roundingMode = saved_rounding;
    softfloat_detectTininess = saved_tininess;
    softfloat_exceptionFlags = saved_exceptions;
    return passed;
}

bool binary64_scaling_preserves_underflow_status() {
    const auto saved_rounding = softfloat_roundingMode;
    const auto saved_tininess = softfloat_detectTininess;
    const auto saved_exceptions = softfloat_exceptionFlags;
    softfloat_detectTininess = softfloat_tininess_afterRounding;
    constexpr std::array<std::uint_fast8_t, 4> rounding_modes{
        softfloat_round_near_even, softfloat_round_minMag,
        softfloat_round_max, softfloat_round_min};
    constexpr std::array<std::array<std::uint64_t, 2>, 15> cases{{
        {0x0010000000000000ull, 0x0010000000000000ull}, // rounds to zero
        {0x0010000000000000ull, 0x3CA0000000000000ull}, // half minimum subnormal
        {0x0010000000000001ull, 0x3FE0000000000000ull}, // inexact subnormal
        {0x0010000000000000ull, 0x3FE0000000000000ull}, // exact subnormal
        {0x0010000000000000ull, 0x4000000000000000ull}, // exact normal
        {0x7FEFFFFFFFFFFFFFull, 0x4000000000000000ull}, // overflow
        {0x0010000000000000ull, 0x0000000000000000ull}, // exact zero
        {0x001FFFFFFFFFFFFEull, 0x3FE0000000000000ull}, // largest subnormal
        {0x001FFFFFFFFFFFFFull, 0x3FE0000000000000ull}, // rounds up to minimum normal
        {0x0020000000000000ull, 0x3FE0000000000000ull}, // exact minimum normal
        {0x0020000000000001ull, 0x3FE0000000000000ull}, // next normal
        {0x3FE0000000000000ull, 0x001FFFFFFFFFFFFEull}, // swapped boundary operands
        {0x3FE0000000000000ull, 0x001FFFFFFFFFFFFFull},
        {0x3FE0000000000000ull, 0x0020000000000000ull},
        {0x3FE0000000000000ull, 0x0020000000000001ull},
    }};
    bool passed = true;
    for (const auto& operands : cases) {
        for (const auto sign : {0ull, 0x8000000000000000ull}) {
            for (std::uint32_t mode = 0u; mode < 8u; ++mode) {
                const std::uint64_t a = operands[0] ^ sign;
                const std::uint64_t b = operands[1];
                // Multiplication plus all four fused forms, with both signs
                // of zero addend. These are the existing native scale paths.
                for (std::uint32_t variant = 0u; variant < 9u; ++variant) {
                    const auto operation = static_cast<galaxy::PpcFloatTernaryOperation>(
                        variant == 0u ? 0u : (variant - 1u) / 2u);
                    const std::uint64_t c = variant != 0u && (variant & 1u) == 0u
                        ? 0x8000000000000000ull : 0ull;
                    const bool subtract = variant != 0u &&
                        (operation == galaxy::PpcFloatTernaryOperation::MultiplySubtract ||
                         operation == galaxy::PpcFloatTernaryOperation::NegativeMultiplySubtract);
                    const bool negate = variant != 0u &&
                        (operation == galaxy::PpcFloatTernaryOperation::NegativeMultiplyAdd ||
                         operation == galaxy::PpcFloatTernaryOperation::NegativeMultiplySubtract);
                    const auto reference = [&]() {
                        return variant == 0u
                            ? f64_mul(float64_t{a}, float64_t{b})
                            : f64_mulAdd(float64_t{a}, float64_t{b},
                                  float64_t{c ^ (subtract ? 0x8000000000000000ull : 0ull)});
                    };
                    softfloat_roundingMode = rounding_modes[mode & 3u];
                    softfloat_exceptionFlags = 0;
                    const auto primary = reference();
                    const auto flags = softfloat_exceptionFlags;
                    softfloat_roundingMode = softfloat_round_minMag;
                    softfloat_exceptionFlags = 0;
                    const auto truncated = reference();
                    auto expected_bits = primary.v;
                    if ((mode & 4u) != 0u &&
                        (expected_bits & 0x7FF0000000000000ull) == 0u) {
                        expected_bits &= 0x8000000000000000ull;
                    }
                    expected_bits ^= negate ? 0x8000000000000000ull : 0ull;
                    const std::uint32_t expected_exceptions =
                        ((flags & softfloat_flag_inexact) != 0 ? 0x02000000u : 0u) |
                        ((flags & softfloat_flag_underflow) != 0 ? 0x08000000u : 0u) |
                        ((flags & softfloat_flag_overflow) != 0 ? 0x10000000u : 0u);
                    const bool inexact = (flags & softfloat_flag_inexact) != 0;
                    const bool rounded_up = inexact && primary.v != truncated.v;
                    const auto actual = variant == 0u
                        ? galaxy::ppc_f64_binary(galaxy::PpcFloatBinaryOperation::Multiply, a, b, mode)
                        : galaxy::ppc_f64_ternary(operation, a, b, c, mode);
                    if (actual.bits != expected_bits ||
                        actual.exception_bits != expected_exceptions ||
                        actual.inexact != inexact || actual.rounded_up != rounded_up) {
                        std::cerr << "FAILED: binary64 scaling a=0x" << std::hex
                                  << a << " b=0x" << b << " c=0x" << c
                                  << " mode=" << mode << " variant=" << variant
                                  << " expected=0x" << expected_bits
                                  << " actual=0x" << actual.bits
                                  << " expected-status=" << expected_exceptions
                                  << " actual-status=" << actual.exception_bits
                                  << std::dec << '\n';
                        passed = false;
                    }
                }
            }
        }
    }
    softfloat_roundingMode = saved_rounding;
    softfloat_detectTininess = saved_tininess;
    softfloat_exceptionFlags = saved_exceptions;
    return passed;
}

bool fused_binary32_matches_software_reference(std::uint32_t mode) {
    const auto saved_rounding = softfloat_roundingMode;
    const auto saved_tininess = softfloat_detectTininess;
    const auto saved_exceptions = softfloat_exceptionFlags;
    softfloat_detectTininess = softfloat_tininess_afterRounding;
    bool passed = true;
    const auto check = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c,
                           std::uint32_t rn = 0u) {
        constexpr std::array<std::uint_fast8_t, 4> rounding_modes{
            softfloat_round_near_even, softfloat_round_minMag,
            softfloat_round_max, softfloat_round_min};
        const auto left = mode ? reference_flush_subnormal(a) : a;
        const auto right = mode ? reference_flush_subnormal(b) : b;
        const auto addend = mode ? reference_flush_subnormal(c) : c;
        for (const auto operation : {
                 galaxy::PpcFloatTernaryOperation::MultiplyAdd,
                 galaxy::PpcFloatTernaryOperation::MultiplySubtract,
                 galaxy::PpcFloatTernaryOperation::NegativeMultiplyAdd,
                 galaxy::PpcFloatTernaryOperation::NegativeMultiplySubtract}) {
            const bool subtract =
                operation == galaxy::PpcFloatTernaryOperation::MultiplySubtract ||
                operation == galaxy::PpcFloatTernaryOperation::NegativeMultiplySubtract;
            const bool negate =
                operation == galaxy::PpcFloatTernaryOperation::NegativeMultiplyAdd ||
                operation == galaxy::PpcFloatTernaryOperation::NegativeMultiplySubtract;
            const std::uint32_t adjusted_c = addend ^ (subtract ? 0x80000000u : 0u);
            softfloat_roundingMode = rounding_modes[rn];
            softfloat_exceptionFlags = 0;
            const auto primary = f32_mulAdd(
                float32_t{left}, float32_t{right}, float32_t{adjusted_c});
            const auto flags = softfloat_exceptionFlags;
            softfloat_roundingMode = softfloat_round_minMag;
            softfloat_exceptionFlags = 0;
            const auto truncated = f32_mulAdd(
                float32_t{left}, float32_t{right}, float32_t{adjusted_c});
            const bool inexact = (flags & softfloat_flag_inexact) != 0;
            const bool rounded_up = inexact && primary.v != truncated.v;
            const auto flushed = mode ? reference_flush_subnormal(primary.v) : primary.v;
            const auto expected_bits = flushed ^ (negate ? 0x80000000u : 0u);
            const std::uint32_t expected_exceptions =
                ((flags & softfloat_flag_inexact) != 0 ? 0x02000000u : 0u) |
                ((flags & softfloat_flag_underflow) != 0 ? 0x08000000u : 0u) |
                ((flags & softfloat_flag_overflow) != 0 ? 0x10000000u : 0u);
            softfloat_roundingMode = softfloat_round_max;
            softfloat_detectTininess = softfloat_tininess_beforeRounding;
            softfloat_exceptionFlags = softfloat_flag_invalid | softfloat_flag_inexact;
            const auto actual = galaxy::ppc_f32_ternary(operation, a, b, c, mode | rn);
            if (!expect(softfloat_roundingMode == softfloat_round_max &&
                            softfloat_detectTininess == softfloat_tininess_beforeRounding &&
                            softfloat_exceptionFlags ==
                                (softfloat_flag_invalid | softfloat_flag_inexact),
                        "fused binary32 arithmetic preserves caller SoftFloat globals")) {
                return false;
            }
            softfloat_detectTininess = softfloat_tininess_afterRounding;
            if (actual.bits != galaxy::widen_f32_bits(expected_bits) ||
                actual.exception_bits != expected_exceptions ||
                actual.inexact != inexact || actual.rounded_up != rounded_up ||
                actual.fprf != galaxy::ppc_f32_passthrough(expected_bits).fprf) {
                std::cerr << "FAILED: fused binary32 reference a=0x" << std::hex
                          << a << " b=0x" << b << " c=0x" << c
                          << " mode=" << (mode | rn)
                          << " operation=" << static_cast<unsigned>(operation)
                          << " expected=0x" << expected_bits
                          << " actual-wide=0x" << actual.bits
                          << " expected-status=" << expected_exceptions
                          << " actual-status=" << actual.exception_bits
                          << " expected-FI/FR=" << inexact << '/' << rounded_up
                          << " actual-FI/FR=" << actual.inexact << '/'
                          << actual.rounded_up << std::dec << '\n';
                return false;
            }
        }
        return true;
    };
    // Stress the exactness guard's two boundaries, carry propagation,
    // cancellation, and signs using products with full 48-bit significands.
    constexpr std::array<std::uint32_t, 5> fractions{
        0u, 1u, 0x003FFFFFu, 0x007FFFFEu, 0x007FFFFFu};
    for (std::int32_t distance : {-6, -5, -4, 26, 27, 28, 29, 30}) {
        for (auto fraction : fractions) {
            for (auto sign : {0u, 0x80000000u}) {
                passed &= check(0x3F800001u | fraction, 0x3FFFFFFFu,
                    sign | (static_cast<std::uint32_t>(127 - distance) << 23u) |
                        fraction);
            }
        }
    }
    passed &= check(0u, 0xBF800000u, 0x80000000u);
    passed &= check(0x80000000u, 0x3F800000u, 0u);
    passed &= check(0x3F800000u, 0x3F800000u, 0xBF800000u);
    // Exercise the fused proof interval at different product binades and
    // far outside both boundaries. The independent SoftFloat oracle above
    // checks result bits, exceptions, FI/FR/FPRF and caller state for every
    // fused form and rounding mode; this also covers the software fallback.
    for (std::int32_t ea : {1, 32, 64, 96, 127, 160, 192, 224, 254}) {
        for (std::int32_t eb : {1, 64, 127, 192, 254}) {
            for (std::int32_t distance : {-128, -6, -5, -4, 0, 26, 27, 28, 128}) {
                const std::int32_t ec = ea + eb - 127 - distance;
                if (ec < 1 || ec > 254) {
                    continue;
                }
                for (auto fraction : {1u, 0x007FFFFFu}) {
                    for (auto sign_a : {0u, 0x80000000u}) {
                        for (auto sign_c : {0u, 0x80000000u}) {
                            for (std::uint32_t rn = 0u; rn < 4u; ++rn) {
                                passed &= check(
                                    (static_cast<std::uint32_t>(ea) << 23u) |
                                        fraction | sign_a,
                                    (static_cast<std::uint32_t>(eb) << 23u) |
                                        0x007FFFFFu,
                                    (static_cast<std::uint32_t>(ec) << 23u) |
                                        fraction | sign_c,
                                    rn);
                            }
                        }
                    }
                }
            }
        }
    }
    // Independently derive the fused result/status for NI input flushing in
    // each operand position, both signs, exact cancellation and signed zero.
    // Include every rounding mode: directed modes retain the software path.
    constexpr std::array<std::uint32_t, 8> input_flush_cases{
        0u, 0x80000000u, 1u, 0x80000001u, 0x007FFFFFu, 0x807FFFFFu,
        0x3F800000u, 0xBF800000u};
    for (auto a : input_flush_cases) for (auto b : input_flush_cases) {
        for (auto c : input_flush_cases) {
            for (std::uint32_t rn = 0u; rn < 4u; ++rn) passed &= check(a, b, c, rn);
        }
    }
    // The same underflow carry must preserve status for every fused form,
    // including negated results and either sign of zero addend.
    for (auto magnitude : {0x00FFFFFEu, 0x00FFFFFFu, 0x01000000u, 0x01000001u}) {
        for (auto sign : {0u, 0x80000000u}) {
            for (auto zero : {0u, 0x80000000u}) {
                for (std::uint32_t rn = 0u; rn < 4u; ++rn) {
                    passed &= check(magnitude | sign, 0x3F000000u, zero, rn);
                    passed &= check(0x3F000000u, magnitude | sign, zero, rn);
                }
            }
        }
    }
    std::uint32_t random = 0x47414C58u;
    const auto next_finite = [&]() {
        random ^= random << 13u;
        random ^= random >> 17u;
        random ^= random << 5u;
        return (random & 0x7F800000u) == 0x7F800000u
            ? random & ~0x00800000u : random;
    };
    for (std::uint32_t i = 0u; passed && i < 32768u; ++i) {
        const auto a = next_finite();
        const auto b = next_finite();
        const auto c = next_finite();
        passed &= check(a, b, c);
    }
    softfloat_roundingMode = saved_rounding;
    softfloat_detectTininess = saved_tininess;
    softfloat_exceptionFlags = saved_exceptions;
    return passed;
}

bool exceptional_arithmetic_preserves_details() {
    using Binary = galaxy::PpcFloatBinaryOperation;
    struct BinaryCase {
        Binary operation;
        std::uint32_t left, right, bits, exceptions;
    };
    constexpr std::array<BinaryCase, 6> cases{{
        {Binary::Add, 0x7F800001u, 0x3F800000u, 0x7FC00001u, 0x01000000u},
        {Binary::Multiply, 0xFFC00003u, 0x7F800001u, 0xFFC00003u, 0x01000000u},
        {Binary::Add, 0x7F800000u, 0xFF800000u, 0x7FC00000u, 0x00800000u},
        {Binary::Subtract, 0x7F800000u, 0x7F800000u, 0x7FC00000u, 0x00800000u},
        {Binary::Multiply, 0x7F800000u, 0u, 0x7FC00000u, 0x00100000u},
        {Binary::Divide, 0u, 0u, 0x7FC00000u, 0x00200000u},
    }};
    for (std::uint32_t mode = 0u; mode < 8u; ++mode) {
        // Existing sticky bits and enables do not change an arithmetic
        // helper's result; enabled-exception delivery belongs to commit.
        const std::uint32_t fpscr = 0xFFF807F8u | mode;
        for (const auto& test : cases) {
            for (bool wide : {false, true}) {
                const auto result = wide
                    ? galaxy::ppc_f64_binary(test.operation,
                          galaxy::widen_f32_bits(test.left),
                          galaxy::widen_f32_bits(test.right), fpscr)
                    : galaxy::ppc_f32_binary(test.operation, test.left, test.right, fpscr);
                if (!expect(result.bits == galaxy::widen_f32_bits(test.bits) &&
                                result.exception_bits == test.exceptions &&
                                !result.inexact && !result.rounded_up,
                            "exceptional binary operands retain payload and invalid detail")) {
                    return false;
                }
            }
        }
        for (std::uint32_t variant = 0u; variant < 4u; ++variant) {
            const auto operation = static_cast<galaxy::PpcFloatTernaryOperation>(variant);
            const bool subtract = (variant & 1u) != 0u;
            for (bool wide : {false, true}) {
                const auto fused = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c) {
                    return wide
                        ? galaxy::ppc_f64_ternary(operation, galaxy::widen_f32_bits(a),
                              galaxy::widen_f32_bits(b), galaxy::widen_f32_bits(c), fpscr)
                        : galaxy::ppc_f32_ternary(operation, a, b, c, fpscr);
                };
                const auto nan = fused(0xFFC00003u, 0x7F800001u, 0u);
                const auto zero_product = fused(0x7F800000u, 0u, 0x3F800000u);
                const auto opposing_infinities = fused(0x7F800000u, 0x3F800000u,
                    subtract ? 0x7F800000u : 0xFF800000u);
                if (!expect(nan.bits == galaxy::widen_f32_bits(0xFFC00003u) &&
                                nan.exception_bits == 0x01000000u,
                            "fused NaN propagation retains source priority and signaling detail") ||
                    !expect(zero_product.bits == galaxy::widen_f32_bits(0x7FC00000u) &&
                                zero_product.exception_bits == 0x00100000u,
                            "fused infinity times zero retains invalid detail") ||
                    !expect(opposing_infinities.bits == galaxy::widen_f32_bits(0x7FC00000u) &&
                                opposing_infinities.exception_bits == 0x00800000u,
                            "fused opposing infinities retain invalid detail")) {
                    return false;
                }
            }
        }
    }
    return true;
}

bool result_commit_preserves_status_and_fault_order() {
    // A truth table of exception/enable pairs independently checks the
    // optimized bit alignment, including deliberately inconsistent input
    // summaries, sticky exceptions, FI/FR, and scalar/paired commit order.
    constexpr std::array<std::array<std::uint32_t, 2>, 5> enabled_pairs{{
        {0x20000000u, 0x80u}, {0x10000000u, 0x40u}, {0x08000000u, 0x20u},
        {0x04000000u, 0x10u}, {0x02000000u, 0x08u},
    }};
    constexpr std::array<std::uint32_t, 12> sticky_inputs{
        0u, 0xFFFFFFFFu, 0x80000000u, 0x40000000u, 0x20000000u, 0x20000080u,
        0x10000040u, 0x08000020u, 0x04000010u, 0x02000008u, 0x01000080u, 0x3E0000F8u};
    const auto reference_status = [&](std::uint32_t initial,
                                      const galaxy::PpcFloatResult& result) {
        std::uint32_t value = initial | result.exception_bits;
        if ((result.exception_bits & ~initial) != 0u) value |= 0x80000000u;
        value &= ~0x60000000u;
        if ((value & 0x01F80700u) != 0u) value |= 0x20000000u;
        for (const auto& pair : enabled_pairs) {
            if ((value & pair[0]) != 0u && (value & pair[1]) != 0u) {
                value |= 0x40000000u;
            }
        }
        value &= ~0x0007F000u;
        value |= result.fprf;
        if (result.inexact) value |= 0x00020000u;
        if (result.rounded_up) value |= 0x00040000u;
        return value;
    };
    struct CommitFault { std::uint32_t pc; };
    galaxy::NativeServicesV1 services{};
    services.fatal = [](void*, std::uint32_t pc, const char*) { throw CommitFault{pc}; };
    std::uint32_t random = 0x46505343u;
    const auto next = [&]() {
        random ^= random << 13u;
        random ^= random >> 17u;
        random ^= random << 5u;
        return random;
    };
    for (std::uint32_t i = 0u; i < 4096u; ++i) {
        const std::uint32_t initial = i < sticky_inputs.size() ? sticky_inputs[i]
            : i < sticky_inputs.size() + 32u ? 1u << (i - sticky_inputs.size()) : next();
        const std::uint32_t cr = next();
        const auto lane = [&]() {
            const std::uint64_t bits = 0x3FF0000000000000ull | next();
            // A previously enabled sticky exception may keep FEX set, but
            // an operation with no newly raised exception must still commit.
            const std::uint32_t exceptions =
                i < sticky_inputs.size() || (i & 7u) == 0u ? 0u : next() & 0x1FF80700u;
            const std::uint32_t fprf = next() & 0x0001F000u;
            return galaxy::PpcFloatResult{bits, exceptions, fprf,
                (next() & 1u) != 0u, (next() & 1u) != 0u};
        };
        const auto lane0 = lane();
        const auto lane1 = lane();
        const bool record = (i & 1u) != 0u;
        const bool source_lane1 = (i & 2u) != 0u;
        const bool duplicate = (i & 4u) != 0u;
        const bool paired_mode = (i & 8u) != 0u;
        for (bool paired : {false, true}) {
            auto combined = paired && source_lane1 ? lane1 : lane0;
            if (paired) {
                combined.exception_bits = lane0.exception_bits | lane1.exception_bits;
                combined.inexact = lane0.inexact || lane1.inexact;
                combined.rounded_up = lane0.rounded_up || lane1.rounded_up;
            }
            const auto expected_status = reference_status(initial, combined);
            const auto raised = combined.exception_bits |
                ((combined.exception_bits & 0x01F80700u) != 0u ? 0x20000000u : 0u);
            bool expected_fault = false;
            for (const auto& pair : enabled_pairs) {
                expected_fault |= (raised & pair[0]) != 0u && (initial & pair[1]) != 0u;
            }
            galaxy::PpcContext context{};
            context.fpscr = initial;
            context.cr = cr;
            context.hid2 = paired_mode ? 0x20000000u : 0u;
            context.fpr_bits[3] = 0x1111222233334444ull;
            context.ps1_bits[3] = 0xAAAABBBBCCCCDDDDull;
            bool faulted = false;
            try {
                if (paired) {
                    galaxy::ppc_commit_paired_result(&context, 3u, lane0, lane1,
                        source_lane1, record, &services, 0x80000004u);
                } else {
                    galaxy::ppc_commit_scalar_result(&context, 3u, lane0,
                        duplicate, record, &services, 0x80000004u);
                }
            } catch (const CommitFault& fault) {
                if (fault.pc != 0x80000004u) return false;
                faulted = true;
            }
            const auto expected_cr = record && !expected_fault
                ? (cr & ~0x0F000000u) | ((expected_status >> 4u) & 0x0F000000u) : cr;
            const auto expected_lane0 = expected_fault ? 0x1111222233334444ull : lane0.bits;
            const auto expected_lane1 = expected_fault ? 0xAAAABBBBCCCCDDDDull
                : paired ? lane1.bits : duplicate && paired_mode ? lane0.bits : 0xAAAABBBBCCCCDDDDull;
            if (!expect(context.fpscr == expected_status && faulted == expected_fault &&
                            context.cr == expected_cr && context.fpr_bits[3] == expected_lane0 &&
                            context.ps1_bits[3] == expected_lane1,
                        "result commit preserves status, enabled faults, register writes, and CR1")) {
                return false;
            }
        }
    }
    return true;
}

bool paired_binary_boundary_matches_lane_helpers() {
    // Differentially exercise the new packed arithmetic/commit boundary
    // against the established two arithmetic results plus paired commit.
    // The existing arithmetic tests independently check those results against
    // SoftFloat; this test covers lane coupling, status selection, trapping,
    // and captured operands when the destination aliases either source.
    constexpr std::array<std::uint32_t, 8> enables{
        0u, 0x08u, 0x10u, 0x20u, 0x40u, 0x80u, 0xF8u, 0u};
    constexpr std::array<std::uint32_t, 8> sticky_inputs{
        0u, 0xFFFFFFFFu, 0x80000000u, 0x40000000u,
        0x01000000u, 0x1E000000u, 0x0007F000u, 0x01F80700u};
    constexpr std::array<std::uint32_t, 3> targets{3u, 9u, 17u};
    struct CommitFault {
        std::uint32_t pc;
        const char* message;
    };
    galaxy::NativeServicesV1 services{};
    services.fatal = [](void*, std::uint32_t pc, const char* message) {
        throw CommitFault{pc, message};
    };
    std::uint32_t random = 0x50534243u;
    const auto next = [&]() {
        random ^= random << 13u;
        random ^= random >> 17u;
        random ^= random << 5u;
        return random;
    };
    const auto saved_rounding = softfloat_roundingMode;
    const auto saved_tininess = softfloat_detectTininess;
    const auto saved_exceptions = softfloat_exceptionFlags;
    const auto check = [&](std::uint32_t left0, std::uint32_t left1,
                           std::uint32_t right0, std::uint32_t right1,
                           std::uint32_t case_index) {
        for (const auto operation : {
                 galaxy::PpcFloatBinaryOperation::Add,
                 galaxy::PpcFloatBinaryOperation::Subtract,
                 galaxy::PpcFloatBinaryOperation::Multiply,
                 galaxy::PpcFloatBinaryOperation::Divide}) {
            const auto op = static_cast<std::uint32_t>(operation);
            for (std::uint32_t mode = 0u; mode < 8u; ++mode) {
                galaxy::PpcContext initial{};
                for (std::uint32_t reg = 0u; reg < 32u; ++reg) {
                    initial.gpr[reg] = next();
                    const auto lane0_high = static_cast<std::uint64_t>(next());
                    const auto lane0_low = next();
                    const auto lane1_high = static_cast<std::uint64_t>(next());
                    const auto lane1_low = next();
                    initial.fpr_bits[reg] = (lane0_high << 32u) | lane0_low;
                    initial.ps1_bits[reg] = (lane1_high << 32u) | lane1_low;
                }
                initial.fpr_bits[3] = galaxy::widen_f32_bits(left0);
                initial.ps1_bits[3] = galaxy::widen_f32_bits(left1);
                initial.fpr_bits[9] = galaxy::widen_f32_bits(right0);
                initial.ps1_bits[9] = galaxy::widen_f32_bits(right1);
                const auto sticky = case_index < sticky_inputs.size()
                    ? sticky_inputs[case_index] : next();
                initial.fpscr = (sticky & ~0xFFu) | mode |
                    enables[(case_index + mode + op) % enables.size()];
                initial.cr = next();
                initial.msr = galaxy::kMsrFloatingPointAvailable;
                initial.hid2 = 0x20000000u;
                const auto target = targets[(case_index + mode + op) % targets.size()];
                const bool record = ((case_index + mode + op) & 1u) != 0u;
                const auto lane0 = galaxy::ppc_f32_binary(
                    operation, left0, right0, initial.fpscr);
                const auto lane1 = galaxy::ppc_f32_binary(
                    operation, left1, right1, initial.fpscr);
                auto expected = initial;
                auto actual = initial;
                bool expected_fault = false;
                bool actual_fault = false;
                std::uint32_t expected_pc = 0u;
                std::uint32_t actual_pc = 0u;
                std::string_view expected_message;
                std::string_view actual_message;
                constexpr std::uint32_t guest_pc = 0x80012340u;
                try {
                    galaxy::ppc_commit_paired_result(&expected, target, lane0,
                        lane1, false, record, &services, guest_pc);
                } catch (const CommitFault& fault) {
                    expected_fault = true;
                    expected_pc = fault.pc;
                    expected_message = fault.message;
                }
                try {
                    galaxy::ppc_commit_paired_binary_result(&actual, target,
                        operation, left0, left1, right0, right1, record,
                        &services, guest_pc);
                } catch (const CommitFault& fault) {
                    actual_fault = true;
                    actual_pc = fault.pc;
                    actual_message = fault.message;
                }
                if (expected_fault != actual_fault || expected_pc != actual_pc ||
                    expected_message != actual_message ||
                    expected.fpscr != actual.fpscr || expected.cr != actual.cr ||
                    expected.msr != actual.msr || expected.hid2 != actual.hid2 ||
                    std::memcmp(expected.fpr_bits, actual.fpr_bits,
                        sizeof(expected.fpr_bits)) != 0 ||
                    std::memcmp(expected.ps1_bits, actual.ps1_bits,
                        sizeof(expected.ps1_bits)) != 0 ||
                    std::memcmp(expected.gpr, actual.gpr, sizeof(expected.gpr)) != 0 ||
                    softfloat_roundingMode != saved_rounding ||
                    softfloat_detectTininess != saved_tininess ||
                    softfloat_exceptionFlags != saved_exceptions) {
                    std::cerr << "FAILED: paired binary boundary left=0x"
                              << std::hex << left0 << ',' << left1
                              << " right=0x" << right0 << ',' << right1
                              << " mode=" << mode << " operation=" << op
                              << " target=" << target
                              << " expected-fpscr=0x" << expected.fpscr
                              << " actual-fpscr=0x" << actual.fpscr
                              << " expected-lanes=0x" << expected.fpr_bits[target]
                              << ',' << expected.ps1_bits[target]
                              << " actual-lanes=0x" << actual.fpr_bits[target]
                              << ',' << actual.ps1_bits[target]
                              << std::dec << '\n';
                    return false;
                }
            }
        }
        return true;
    };
    constexpr std::array<std::uint32_t, 36> cases{
        0u, 0x80000000u, 1u, 0x80000001u, 0x007FFFFFu, 0x807FFFFFu,
        0x00800000u, 0x80800000u, 0x00800001u, 0x80800001u,
        0x00FFFFFFu, 0x80FFFFFFu, 0x3F000000u, 0xBF000000u,
        0x3F800000u, 0xBF800000u, 0x3F800001u, 0xBF800001u,
        0x3F7FFFFFu, 0xBF7FFFFFu, 0x7F7FFFFFu, 0xFF7FFFFFu,
        0x7F800000u, 0xFF800000u, 0x7F800001u, 0xFF800001u,
        0x7FC01234u, 0xFFC01234u, 0x33800000u, 0xB3800000u,
        0x32000000u, 0xB2000000u, 0x31800000u, 0xB1800000u,
        0x31000000u, 0xB1000000u};
    std::uint32_t case_index = 0u;
    for (std::size_t a = 0u; a < cases.size(); ++a) {
        for (std::size_t b = 0u; b < cases.size(); ++b) {
            if (!check(cases[a], cases[(a + 7u) % cases.size()],
                       cases[b], cases[(b + 11u) % cases.size()], case_index++)) {
                return false;
            }
        }
    }
    for (std::uint32_t i = 0u; i < 8192u; ++i) {
        const auto sample = [&]() {
            const auto bits = next();
            // Half of the windows target the common packed path: both lanes
            // remain normal and nearby, with nonconstant signs and mantissas.
            // The other half spans arbitrary encodings and slow boundaries.
            return (i & 1u) == 0u
                ? (bits & 0x807FFFFFu) | ((110u + ((bits >> 23u) % 29u)) << 23u)
                : bits;
        };
        // Sequence independent samples explicitly; argument evaluation order
        // must not select a different deterministic differential corpus.
        const auto left0 = sample();
        const auto left1 = sample();
        const auto right0 = sample();
        const auto right1 = sample();
        if (!check(left0, left1, right0, right1, case_index++)) {
            return false;
        }
    }
    return true;
}

bool paired_ternary_boundary_matches_lane_helpers() {
    // Differentially exercise the packed fused arithmetic/commit boundary
    // against the established two arithmetic results plus paired commit.
    // The existing arithmetic tests independently check those results against
    // SoftFloat; this test covers lane coupling, status selection, trapping,
    // and captured operands when the destination aliases either source.
    constexpr std::array<std::uint32_t, 8> enables{
        0u, 0x08u, 0x10u, 0x20u, 0x40u, 0x80u, 0xF8u, 0u};
    constexpr std::array<std::uint32_t, 8> sticky_inputs{
        0u, 0xFFFFFFFFu, 0x80000000u, 0x40000000u,
        0x01000000u, 0x1E000000u, 0x0007F000u, 0x01F80700u};
    constexpr std::array<std::uint32_t, 3> targets{3u, 9u, 17u};
    struct CommitFault {
        std::uint32_t pc;
        const char* message;
    };
    galaxy::NativeServicesV1 services{};
    services.fatal = [](void*, std::uint32_t pc, const char* message) {
        throw CommitFault{pc, message};
    };
    std::uint32_t random = 0x50534243u;
    const auto next = [&]() {
        random ^= random << 13u;
        random ^= random >> 17u;
        random ^= random << 5u;
        return random;
    };
    struct RestoreGlobals {
        std::uint_fast8_t rounding, tininess, exceptions;
        ~RestoreGlobals() {
            softfloat_roundingMode = rounding;
            softfloat_detectTininess = tininess;
            softfloat_exceptionFlags = exceptions;
        }
    } restore{softfloat_roundingMode, softfloat_detectTininess,
              softfloat_exceptionFlags};
    softfloat_roundingMode = softfloat_round_max;
    softfloat_detectTininess = softfloat_tininess_beforeRounding;
    softfloat_exceptionFlags = softfloat_flag_invalid | softfloat_flag_inexact;
    const auto saved_rounding = softfloat_roundingMode;
    const auto saved_tininess = softfloat_detectTininess;
    const auto saved_exceptions = softfloat_exceptionFlags;
    const auto check = [&](std::uint32_t left0, std::uint32_t left1,
                           std::uint32_t right0, std::uint32_t right1,
                           std::uint32_t add0, std::uint32_t add1,
                           std::uint32_t case_index) {
        for (const auto operation : {
                 galaxy::PpcFloatTernaryOperation::MultiplyAdd,
                 galaxy::PpcFloatTernaryOperation::MultiplySubtract,
                 galaxy::PpcFloatTernaryOperation::NegativeMultiplyAdd,
                 galaxy::PpcFloatTernaryOperation::NegativeMultiplySubtract}) {
            const auto op = static_cast<std::uint32_t>(operation);
            for (std::uint32_t mode = 0u; mode < 8u; ++mode) {
                galaxy::PpcContext initial{};
                for (std::uint32_t reg = 0u; reg < 32u; ++reg) {
                    initial.gpr[reg] = next();
                    const auto lane0_high = static_cast<std::uint64_t>(next());
                    const auto lane0_low = next();
                    const auto lane1_high = static_cast<std::uint64_t>(next());
                    const auto lane1_low = next();
                    initial.fpr_bits[reg] = (lane0_high << 32u) | lane0_low;
                    initial.ps1_bits[reg] = (lane1_high << 32u) | lane1_low;
                }
                initial.fpr_bits[3] = galaxy::widen_f32_bits(left0);
                initial.ps1_bits[3] = galaxy::widen_f32_bits(left1);
                initial.fpr_bits[9] = galaxy::widen_f32_bits(right0);
                initial.ps1_bits[9] = galaxy::widen_f32_bits(right1);
                initial.fpr_bits[17] = galaxy::widen_f32_bits(add0);
                initial.ps1_bits[17] = galaxy::widen_f32_bits(add1);
                const auto sticky = case_index < sticky_inputs.size()
                    ? sticky_inputs[case_index] : next();
                initial.fpscr = (sticky & ~0xFFu) | mode |
                    enables[(case_index + mode + op) % enables.size()];
                initial.cr = next();
                initial.msr = galaxy::kMsrFloatingPointAvailable;
                initial.hid2 = 0x20000000u;
                const auto target = targets[(case_index + mode + op) % targets.size()];
                const bool record = ((case_index + mode + op) & 1u) != 0u;
                const auto lane0 = galaxy::ppc_f32_ternary(
                    operation, left0, right0, add0, initial.fpscr);
                const auto lane1 = galaxy::ppc_f32_ternary(
                    operation, left1, right1, add1, initial.fpscr);
                auto expected = initial;
                auto actual = initial;
                bool expected_fault = false;
                bool actual_fault = false;
                std::uint32_t expected_pc = 0u;
                std::uint32_t actual_pc = 0u;
                std::string_view expected_message;
                std::string_view actual_message;
                constexpr std::uint32_t guest_pc = 0x80012340u;
                try {
                    galaxy::ppc_commit_paired_result(&expected, target, lane0,
                        lane1, false, record, &services, guest_pc);
                } catch (const CommitFault& fault) {
                    expected_fault = true;
                    expected_pc = fault.pc;
                    expected_message = fault.message;
                }
                try {
                    galaxy::ppc_commit_paired_ternary_result(&actual, target,
                        operation, left0, left1, right0, right1, add0, add1, record,
                        &services, guest_pc);
                } catch (const CommitFault& fault) {
                    actual_fault = true;
                    actual_pc = fault.pc;
                    actual_message = fault.message;
                }
                if (expected_fault != actual_fault || expected_pc != actual_pc ||
                    expected_message != actual_message ||
                    expected.fpscr != actual.fpscr || expected.cr != actual.cr ||
                    std::memcmp(&expected, &actual, sizeof(expected)) != 0 ||
                    std::memcmp(expected.fpr_bits, actual.fpr_bits,
                        sizeof(expected.fpr_bits)) != 0 ||
                    std::memcmp(expected.ps1_bits, actual.ps1_bits,
                        sizeof(expected.ps1_bits)) != 0 ||
                    std::memcmp(expected.gpr, actual.gpr, sizeof(expected.gpr)) != 0 ||
                    softfloat_roundingMode != saved_rounding ||
                    softfloat_detectTininess != saved_tininess ||
                    softfloat_exceptionFlags != saved_exceptions) {
                    std::cerr << "FAILED: paired ternary boundary left=0x"
                              << std::hex << left0 << ',' << left1
                              << " right=0x" << right0 << ',' << right1
                              << " mode=" << mode << " operation=" << op
                              << " target=" << target
                              << " expected-fpscr=0x" << expected.fpscr
                              << " actual-fpscr=0x" << actual.fpscr
                              << " expected-lanes=0x" << expected.fpr_bits[target]
                              << ',' << expected.ps1_bits[target]
                              << " actual-lanes=0x" << actual.fpr_bits[target]
                              << ',' << actual.ps1_bits[target]
                              << std::dec << '\n';
                    return false;
                }
            }
        }
        return true;
    };
    constexpr std::array<std::uint32_t, 36> cases{
        0u, 0x80000000u, 1u, 0x80000001u, 0x007FFFFFu, 0x807FFFFFu,
        0x00800000u, 0x80800000u, 0x00800001u, 0x80800001u,
        0x00FFFFFFu, 0x80FFFFFFu, 0x3F000000u, 0xBF000000u,
        0x3F800000u, 0xBF800000u, 0x3F800001u, 0xBF800001u,
        0x3F7FFFFFu, 0xBF7FFFFFu, 0x7F7FFFFFu, 0xFF7FFFFFu,
        0x7F800000u, 0xFF800000u, 0x7F800001u, 0xFF800001u,
        0x7FC01234u, 0xFFC01234u, 0x33800000u, 0xB3800000u,
        0x32000000u, 0xB2000000u, 0x31800000u, 0xB1800000u,
        0x31000000u, 0xB1000000u};
    std::uint32_t case_index = 0u;
    for (std::size_t a = 0u; a < cases.size(); ++a) {
        for (std::size_t b = 0u; b < cases.size(); ++b) {
            if (!check(cases[a], cases[(a + 7u) % cases.size()],
                       cases[b], cases[(b + 11u) % cases.size()],
                       cases[(a + b + 3u) % cases.size()],
                       cases[(a + 2u*b + 9u) % cases.size()], case_index++)) {
                return false;
            }
        }
    }
    for (std::uint32_t i = 0u; i < 8192u; ++i) {
        const auto sample = [&]() {
            const auto bits = next();
            // Half of the windows target the common packed path: both lanes
            // remain normal and nearby, with nonconstant signs and mantissas.
            // The other half spans arbitrary encodings and slow boundaries.
            return (i & 1u) == 0u
                ? (bits & 0x807FFFFFu) | ((110u + ((bits >> 23u) % 29u)) << 23u)
                : bits;
        };
        // Sequence independent samples explicitly; argument evaluation order
        // must not select a different deterministic differential corpus.
        const auto left0 = sample();
        const auto left1 = sample();
        const auto right0 = sample();
        const auto right1 = sample();
        const auto add0 = sample();
        const auto add1 = sample();
        if (!check(left0, left1, right0, right1, add0, add1, case_index++)) {
            return false;
        }
    }
    // Product/addend exponent-distance proof boundaries, with opposite
    // signs, carry/cancellation and one eligible versus ineligible lane.
    for (const int distance : {-6, -5, -1, 0, 27, 28, 29}) {
        const auto add = static_cast<std::uint32_t>(127-distance) << 23u;
        for (std::uint32_t sign=0; sign<4; ++sign) {
            if (!check(0x3F800001u, 0xBF7FFFFFu, 0x3F800003u, 0x3F800001u,
                    add | ((sign&1u)<<31u), add | ((sign&2u)<<30u), case_index++)) {
                return false;
            }
        }
    }
    return true;
}

bool inline_scalar_single_binary_matches_current_helper() {
    // Link this test against the preserved current float library first.
    // It compares every guest context byte, not just the rounded result.
    // Existing software-reference tests independently cover the moved math.
    using Operation = galaxy::PpcFloatBinaryOperation;
    struct FloatFault { std::uint32_t pc; };
    galaxy::NativeServicesV1 services{};
    services.fatal = [](void*, std::uint32_t pc, const char*) { throw FloatFault{pc}; };
    constexpr std::uint32_t pc = 0x80004000u;
    constexpr std::array<Operation, 3> operations{
        Operation::Add, Operation::Subtract, Operation::Multiply};
    constexpr std::uint32_t modes[]{
        0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u,
        0x00000008u, 0x00000010u, 0x00000080u, 0x000000F8u,
        0x02000008u, 0x20000000u, 0x40000000u, 0x80000000u,
        0x01F80700u, 0xFFFFFFFFu};
    constexpr std::uint32_t encodings[]{
        0x00000000u, 0x80000000u, 0x00000001u, 0x80000001u,
        0x007FFFFFu, 0x00800000u, 0x80800000u, 0x00800001u,
        0x3F800000u, 0xBF800000u, 0x3F800001u, 0xBF800001u,
        0x3F7FFFFFu, 0x7F7FFFFFu, 0xFF7FFFFFu, 0x7F800000u,
        0xFF800000u, 0x7F800001u, 0xFF800001u, 0x7FC01234u,
        0xFFC01234u, 0x33800000u, 0xB3800000u, 0x31000000u,
        0x30800000u, 0x3F000000u};
    std::uint64_t cases = 0u;
    std::uint64_t accepted = 0u;
    std::uint64_t rejected = 0u;
    std::uint64_t fast_faults = 0u;
    std::uint32_t random = 0x53434C52u;
    const auto next = [&]() {
        random ^= random << 13u;
        random ^= random >> 17u;
        random ^= random << 5u;
        return random;
    };
    const auto common_widened = [](std::uint64_t bits) {
        const auto magnitude = bits & 0x7FFFFFFFFFFFFFFFull;
        const auto exponent = (bits >> 52u) & 0x7FFu;
        return magnitude == 0u ||
            (exponent >= 897u && exponent <= 1150u &&
             (bits & 0x1FFFFFFFull) == 0u);
    };
    const auto fast = [&](Operation operation, galaxy::PpcContext* context,
                          std::uint32_t target, std::uint64_t left,
                          std::uint64_t right, bool record) {
        switch (operation) {
        case Operation::Add:
            return galaxy::try_commit_widened_scalar_binary<Operation::Add>(
                context, target, left, right, record, &services, pc);
        case Operation::Subtract:
            return galaxy::try_commit_widened_scalar_binary<Operation::Subtract>(
                context, target, left, right, record, &services, pc);
        case Operation::Multiply:
            return galaxy::try_commit_widened_scalar_binary<Operation::Multiply>(
                context, target, left, right, record, &services, pc);
        case Operation::Divide:
            return false;
        }
        return false;
    };
    const auto check = [&](Operation operation, std::uint64_t left,
                           std::uint64_t right, std::uint32_t fpscr,
                           std::uint32_t target, bool record, bool paired) {
        galaxy::PpcContext initial{};
        initial.fpscr = fpscr;
        initial.cr = 0xA13579BFu;
        initial.hid2 = paired ? 0x20000000u : 0u;
        for (std::uint32_t reg = 0u; reg < 32u; ++reg) {
            initial.gpr[reg] = 0x13572468u ^ reg;
            initial.fpr_bits[reg] = 0x4051000000000000ull + reg;
            initial.ps1_bits[reg] = 0xC057000000000000ull + reg;
        }
        initial.fpr_bits[1] = left;
        initial.fpr_bits[2] = right;
        galaxy::PpcContext reference{};
        galaxy::PpcContext actual{};
        // Give both copies identical padding as well as identical fields.
        std::memcpy(&reference, &initial, sizeof(initial));
        std::memcpy(&actual, &initial, sizeof(initial));
        const auto result = galaxy::ppc_f64_binary_to_f32(operation, left, right, fpscr);
        bool reference_fault = false;
        try {
            galaxy::ppc_commit_scalar_result(
                &reference, target, result, true, record, &services, pc);
        } catch (const FloatFault& fault) {
            if (fault.pc != pc) return false;
            reference_fault = true;
        }
        bool completed_fast = false;
        bool fallback = false;
        bool actual_fault = false;
        bool fast_fault = false;
        try {
            // This is the exact emitted alias-safe capture/fallback order.
            const auto captured_left = actual.fpr_bits[1];
            const auto captured_right = actual.fpr_bits[2];
            completed_fast = fast(operation, &actual, target,
                                  captured_left, captured_right, record);
            if (!completed_fast) {
                if (std::memcmp(&actual, &initial, sizeof(actual)) != 0) {
                    std::cerr << "FAILED: rejected scalar fast guard mutated context\n";
                    return false;
                }
                fallback = true;
                const auto original = galaxy::ppc_f64_binary_to_f32(
                    operation, actual.fpr_bits[1], actual.fpr_bits[2], actual.fpscr);
                galaxy::ppc_commit_scalar_result(
                    &actual, target, original, true, record, &services, pc);
            }
        } catch (const FloatFault& fault) {
            if (fault.pc != pc) return false;
            actual_fault = true;
            fast_fault = !fallback;
        }
        // The native guard classifies its unflushed binary32 result. With NI,
        // the old fallback can flush a subnormal to signed zero; that zero
        // must not make a rejected native candidate appear eligible here.
        auto eligibility_result = result;
        if ((fpscr & 7u) == 4u && common_widened(left) && common_widened(right)) {
            eligibility_result = galaxy::ppc_f64_binary_to_f32(
                operation, left, right, fpscr & ~4u);
        }
        const auto result_magnitude = eligibility_result.bits & 0x7FFFFFFFFFFFFFFFull;
        const bool expected_eligible = (fpscr & 3u) == 0u &&
            common_widened(left) && common_widened(right) &&
            common_widened(eligibility_result.bits) &&
            result_magnitude != galaxy::widen_f32_bits(0x00800000u);
        if ((completed_fast || fast_fault) != expected_eligible ||
            reference_fault != actual_fault ||
            std::memcmp(&reference, &actual, sizeof(actual)) != 0) {
            std::cerr << "FAILED: scalar inline differential left=" << std::hex << left
                      << " right=" << right << " fpscr=" << fpscr
                      << " target=" << target << " operation="
                      << static_cast<unsigned>(operation) << " oracle-result=" << result.bits
                      << " unflushed-result=" << eligibility_result.bits
                      << " oracle-fpscr=" << reference.fpscr << " actual-fpscr=" << actual.fpscr
                      << std::dec << " expected-eligible=" << expected_eligible
                      << " actual-eligible=" << (completed_fast || fast_fault)
                      << " state-equal=" << (std::memcmp(&reference, &actual, sizeof(actual)) == 0)
                      << " oracle-fault=" << reference_fault << " actual-fault=" << actual_fault << '\n';
            return false;
        }
        // The oracle and candidate must both publish status while preserving
        // the destination, PS1 and CR on an enabled-exception hard failure.
        if (actual_fault &&
            (actual.fpr_bits[target] != initial.fpr_bits[target] ||
             actual.ps1_bits[target] != initial.ps1_bits[target] ||
             actual.cr != initial.cr)) {
            std::cerr << "FAILED: scalar enabled fault wrote destination before trap\n";
            return false;
        }
        ++cases;
        accepted += (completed_fast || fast_fault) ? 1u : 0u;
        rejected += fallback ? 1u : 0u;
        fast_faults += fast_fault ? 1u : 0u;
        return true;
    };
    for (auto left_bits : encodings) {
        const auto left = galaxy::widen_f32_bits(left_bits);
        std::uint32_t narrowed = 0u;
        const bool narrowed_fast =
            galaxy::float_native_detail::try_narrow_normal_or_zero_widened_f32(left, narrowed);
        const bool finite_normal_or_zero = (left_bits & 0x7FFFFFFFu) == 0u ||
            ((left_bits & 0x7F800000u) != 0u &&
             (left_bits & 0x7F800000u) != 0x7F800000u);
        if (narrowed_fast != finite_normal_or_zero ||
            (narrowed_fast && narrowed != left_bits)) {
            std::cerr << "FAILED: widened-single normal/zero conversion proof\n";
            return false;
        }
        for (auto right_bits : encodings) {
            const auto right = galaxy::widen_f32_bits(right_bits);
            for (auto operation : operations) {
                for (auto mode : modes) {
                    for (std::uint32_t target : {1u, 2u, 3u}) {
                        for (std::uint32_t flags = 0u; flags < 4u; ++flags) {
                            if (!check(operation, left, right, mode, target,
                                       (flags & 1u) != 0u, (flags & 2u) != 0u)) return false;
                        }
                    }
                }
            }
        }
    }
    // Arbitrary binary64 values must retain their current two-stage rounding,
    // exception and NaN behavior. Nearby exact binary32 operands cover the
    // common native path with varying signs, ties, status, aliases and Rc.
    for (std::uint32_t i = 0u; i < 8192u; ++i) {
        const auto sample = [&]() {
            const auto hi = next();
            const auto lo = next();
            if ((i & 1u) != 0u) return (static_cast<std::uint64_t>(hi) << 32u) | lo;
            const auto bits = (hi & 0x807FFFFFu) |
                ((110u + ((hi >> 23u) % 29u)) << 23u);
            return galaxy::widen_f32_bits(bits);
        };
        const auto left = sample();
        const auto right = sample();
        for (auto operation : operations) {
            if (!check(operation, left, right, modes[i % std::size(modes)],
                       1u + i % 3u, (i & 1u) != 0u, (i & 2u) != 0u)) return false;
        }
    }
    if (accepted == 0u || rejected == 0u || fast_faults == 0u) {
        std::cerr << "FAILED: scalar differential did not cover guard/fallback/fault paths\n";
        return false;
    }
    std::cout << "Scalar inline differential cases=" << cases << " accepted=" << accepted
              << " rejected=" << rejected << " native-enabled-faults=" << fast_faults << '\n';
    return true;
}

bool scalar_estimates_preserve_operand_and_result_widths() {
    bool passed = true;
    for (std::uint32_t mode = 0u; mode < 8u; ++mode) {
        const auto reciprocal = galaxy::ppc_f64_reciprocal_estimate(
            0x3FF199999999999Aull, mode);  // double 1.1, not widened float
        const double value = std::bit_cast<double>(reciprocal.bits);
        passed &= expect(std::abs(value * 1.1 - 1.0) < 1.0 / 1024.0 &&
                             reciprocal.exception_bits == 0u &&
                             reciprocal.fprf == 0x00004000u,
                         "scalar fres accepts a full double operand before estimation");
        // Rounding the operand to binary32 first would make this input 1.0.
        const auto next_one = galaxy::ppc_f64_reciprocal_estimate(
            0x3FF0000000000001ull, mode);
        passed &= expect(next_one.bits < 0x3FF0000000000000ull &&
                             next_one.bits > 0x3FEFF00000000000ull,
                         "software fres estimate retains low binary64 operand bits");

        // Exact powers of two give independent exponent/range expectations
        // for the retained software estimate policy, not a Broadway table.
        for (const int exponent : {-1022, -400, 400, 1022}) {
            const auto input = static_cast<std::uint64_t>(1023 + exponent) << 52u;
            const auto expected =
                static_cast<std::uint64_t>(1023 - exponent / 2) << 52u;
            const auto root = galaxy::ppc_f64_reciprocal_sqrt_estimate(input, mode);
            passed &= expect(root.bits == expected && root.exception_bits == 0u &&
                                 root.fprf == 0x00004000u,
                             "frsqrte keeps full double output range without single conversion flags");
        }
    }
    const auto minus_zero = galaxy::ppc_f64_reciprocal_estimate(
        0x8000000000000000ull, 0u);
    const auto minus_infinity = galaxy::ppc_f64_reciprocal_estimate(
        0xFFF0000000000000ull, 0u);
    const auto signaling_nan = galaxy::ppc_f64_reciprocal_estimate(
        0x7FF0000000000001ull, 0u);
    passed &= expect(minus_zero.bits == 0xFFF0000000000000ull &&
                         minus_zero.exception_bits == 0x04000000u &&
                         minus_infinity.bits == 0x8000000000000000ull &&
                         minus_infinity.exception_bits == 0u &&
                         signaling_nan.exception_bits == 0x01000000u &&
                         (signaling_nan.bits & 0x7FF8000000000000ull) ==
                             0x7FF8000000000000ull,
                     "double-input fres retains signed specials and signaling-NaN exception");
    const auto single_subnormal = galaxy::ppc_f64_reciprocal_estimate(
        std::uint64_t{1151u} << 52u, 0u);  // 2^128 -> 2^-128
    passed &= expect(single_subnormal.bits == (std::uint64_t{895u} << 52u) &&
                         single_subnormal.exception_bits == 0u,
                     "fres narrows the reciprocal rather than overflowing its double source");
    for (const int exponent : {-1022, -401, -1, 0, 1, 401, 1023}) {
        for (const std::uint64_t fraction :
             {0x000123456789ABCDull, 0x0008000000000000ull, 0x000FFFFFFFFFFFFFull}) {
            const auto input =
                (static_cast<std::uint64_t>(1023 + exponent) << 52u) | fraction;
            const auto result = galaxy::ppc_f64_reciprocal_sqrt_estimate(input, 0u);
            const double relative = std::bit_cast<double>(result.bits) *
                std::sqrt(std::bit_cast<double>(input));
            passed &= expect(std::abs(relative - 1.0) < 1.0 / 32.0 &&
                                 result.exception_bits == 0u &&
                                 (result.bits & ((1ull << 41u) - 1ull)) == 0u,
                             "software frsqrte stays bounded across exponent and mantissa extremes");
        }
    }
    const auto tiny = galaxy::ppc_f64_reciprocal_sqrt_estimate(1ull, 0u);
    const auto flushed = galaxy::ppc_f64_reciprocal_sqrt_estimate(1ull, 4u);
    passed &= expect(tiny.bits == (std::uint64_t{1560u} << 52u) &&
                         tiny.exception_bits == 0u &&
                         flushed.bits == 0x7FF0000000000000ull &&
                         flushed.exception_bits == 0x04000000u,
                     "frsqrte preserves tiny double inputs with NI off and existing NI flush with NI on");

    galaxy::PpcContext context{};
    context.hid2 = 0x20000000u;
    context.fpr_bits[1] = 0x3FF199999999999Aull;
    const auto result = galaxy::ppc_f64_reciprocal_estimate(context.fpr_bits[1], 0u);
    galaxy::ppc_commit_scalar_result(&context, 1u, result, true, true, nullptr, 0u);
    passed &= expect(context.fpr_bits[1] == result.bits &&
                         context.ps1_bits[1] == result.bits &&
                         ((context.cr >> 24u) & 15u) == (context.fpscr >> 28u),
                     "scalar fres alias capture commits single duplication and Rc");
    context.ps1_bits[1] = 0xDEADBEEFull;
    const auto root = galaxy::ppc_f64_reciprocal_sqrt_estimate(
        std::uint64_t{1423u} << 52u, 0u);
    galaxy::ppc_commit_scalar_result(&context, 1u, root, false, false, nullptr, 0u);
    passed &= expect(context.fpr_bits[1] == (std::uint64_t{823u} << 52u) &&
                         context.ps1_bits[1] == 0xDEADBEEFull,
                     "double frsqrte commit retains PS1 without single duplication");

    struct EstimateFault { std::uint32_t pc; };
    galaxy::NativeServicesV1 services{};
    services.fatal = [](void*, std::uint32_t pc, const char*) { throw EstimateFault{pc}; };
    context.fpscr = 0x10u;  // ZE
    context.fpr_bits[1] = 0x1122334455667788ull;
    context.ps1_bits[1] = 0x8877665544332211ull;
    context.cr = 0x12345678u;
    bool faulted = false;
    try {
        galaxy::ppc_commit_scalar_result(
            &context, 1u, minus_zero, true, true, &services, 0x80004000u);
    } catch (const EstimateFault& fault) {
        faulted = fault.pc == 0x80004000u;
    }
    passed &= expect(faulted && context.fpr_bits[1] == 0x1122334455667788ull &&
                         context.ps1_bits[1] == 0x8877665544332211ull &&
                         context.cr == 0x12345678u &&
                         (context.fpscr & 0x04000000u) != 0u,
                     "enabled estimate exception updates FPSCR before trapping without target or Rc commit");
    return passed;
}

}  // namespace

int main() {
    bool passed = exact_binary32_store_conversions();
    passed &= scalar_estimates_preserve_operand_and_result_widths();
    passed &= binary32_matches_software_reference_with_ni();
    passed &= fused_binary32_matches_software_reference(0u);
    passed &= fused_binary32_matches_software_reference(4u);
    passed &= binary64_scaling_preserves_underflow_status();
    passed &= exceptional_arithmetic_preserves_details();
    passed &= result_commit_preserves_status_and_fault_order();
    passed &= paired_binary_boundary_matches_lane_helpers();
    passed &= paired_ternary_boundary_matches_lane_helpers();
    passed &= inline_scalar_single_binary_matches_current_helper();
    const std::uint64_t positive_halfway = 0x3FF0000010000000ull;
    const std::uint64_t negative_halfway = 0xBFF0000010000000ull;

    const auto nearest = galaxy::ppc_narrow_f64_to_f32(positive_halfway, 0);
    const auto toward_zero = galaxy::ppc_narrow_f64_to_f32(positive_halfway, 1);
    const auto toward_positive = galaxy::ppc_narrow_f64_to_f32(positive_halfway, 2);
    const auto toward_negative = galaxy::ppc_narrow_f64_to_f32(positive_halfway, 3);
    passed &= expect(nearest.bits == 0x3F800000u, "nearest-even positive halfway");
    passed &= expect(toward_zero.bits == 0x3F800000u, "toward-zero positive halfway");
    passed &= expect(toward_positive.bits == 0x3F800001u, "toward-positive halfway");
    passed &= expect(toward_negative.bits == 0x3F800000u, "toward-negative positive halfway");
    passed &= expect((nearest.exceptions & galaxy::kPpcFloatExceptionInexact) != 0,
                     "halfway conversion reports inexact");

    const auto negative_up = galaxy::ppc_narrow_f64_to_f32(negative_halfway, 2);
    const auto negative_down = galaxy::ppc_narrow_f64_to_f32(negative_halfway, 3);
    passed &= expect(negative_up.bits == 0xBF800000u, "negative toward-positive");
    passed &= expect(negative_down.bits == 0xBF800001u, "negative toward-negative");

    const auto exact = galaxy::ppc_narrow_f64_to_f32(0x3FF8000000000000ull, 0);
    passed &= expect(exact.bits == 0x3FC00000u, "exact conversion");
    passed &= expect(exact.exceptions == 0, "exact conversion has no exception flags");

    const auto rounded_single =
        galaxy::ppc_round_f64_to_f32(positive_halfway, 2);
    passed &= expect(rounded_single.bits == galaxy::widen_f32_bits(0x3F800001u),
                     "frsp honors the FPSCR rounding mode");
    passed &= expect(rounded_single.inexact && rounded_single.rounded_up,
                     "frsp reports FI and FR inputs");
    const auto signaling_single =
        galaxy::ppc_round_f64_to_f32(0x7FF0000020000000ull, 0);
    passed &= expect((signaling_single.exception_bits & 0x01000000u) != 0,
                     "frsp reports VXSNAN");
    passed &= expect((signaling_single.bits & 0x0008000000000000ull) != 0,
                     "frsp quiets a signaling NaN");

    const auto integer = galaxy::ppc_f64_to_i32_round_zero(
        std::bit_cast<std::uint64_t>(3.75), 0);
    passed &= expect(integer.bits == 0xFFF8000000000003ull,
                     "fctiwz emits the Gekko integer-in-FPR encoding");
    passed &= expect(integer.inexact && !integer.rounded_up,
                     "fctiwz truncation reports inexact without incrementing");
    const auto positive_integer_overflow = galaxy::ppc_f64_to_i32_round_zero(
        std::bit_cast<std::uint64_t>(2147483648.0), 0);
    passed &= expect(positive_integer_overflow.bits == 0xFFF800007FFFFFFFull,
                     "fctiwz saturates positive overflow");
    passed &= expect(
        (positive_integer_overflow.exception_bits & 0x00000100u) != 0,
        "fctiwz positive overflow reports VXCVI");
    const auto negative_integer_overflow = galaxy::ppc_f64_to_i32_round_zero(
        std::bit_cast<std::uint64_t>(-2147483649.0), 0);
    passed &= expect(negative_integer_overflow.bits == 0xFFF8000080000000ull,
                     "fctiwz saturates negative overflow");
    const auto nan_integer =
        galaxy::ppc_f64_to_i32_round_zero(0x7FF0000000000001ull, 0);
    passed &= expect(nan_integer.bits == 0xFFF8000080000000ull,
                     "fctiwz maps NaN to the signed minimum encoding");
    passed &= expect((nan_integer.exception_bits & 0x01000100u) == 0x01000100u,
                     "fctiwz signaling NaN reports VXSNAN and VXCVI");

    const auto single_sum = galaxy::ppc_f32_binary(
        galaxy::PpcFloatBinaryOperation::Add, 0x3FC00000u, 0x40100000u, 0);
    passed &= expect(single_sum.bits == std::bit_cast<std::uint64_t>(3.75),
                     "single add returns an exact widened result");
    passed &= expect(single_sum.exception_bits == 0, "exact single add has no exception");
    passed &= expect(single_sum.fprf == 0x00004000u,
                     "positive normal single result classification");

    const auto double_product = galaxy::ppc_f64_binary(
        galaxy::PpcFloatBinaryOperation::Multiply,
        std::bit_cast<std::uint64_t>(1.5),
        std::bit_cast<std::uint64_t>(-4.0),
        0);
    passed &= expect(double_product.bits == std::bit_cast<std::uint64_t>(-6.0),
                     "double multiply result");
    passed &= expect(double_product.fprf == 0x00008000u,
                     "negative normal double result classification");

    const auto invalid_add = galaxy::ppc_f64_binary(
        galaxy::PpcFloatBinaryOperation::Add,
        0x7FF0000000000000ull,
        0xFFF0000000000000ull,
        0);
    passed &= expect(invalid_add.bits == 0x7FF8000000000000ull,
                     "opposite infinities produce the canonical quiet NaN");
    passed &= expect((invalid_add.exception_bits & 0x00800000u) != 0,
                     "opposite infinities report VXISI");

    const auto signaling = galaxy::ppc_f32_binary(
        galaxy::PpcFloatBinaryOperation::Add, 0x7F800001u, 0x3F800000u, 0);
    passed &= expect(signaling.bits == 0x7FF8000020000000ull,
                     "single signaling NaN is quieted while preserving payload");
    passed &= expect((signaling.exception_bits & 0x01000000u) != 0,
                     "single signaling NaN reports VXSNAN");

    const auto non_ieee = galaxy::ppc_f32_binary(
        galaxy::PpcFloatBinaryOperation::Add, 0x00000001u, 0x00000000u, 4);
    passed &= expect(non_ieee.bits == 0,
                     "non-IEEE mode treats a single denormal operand as zero");

    const auto nearest_sum = galaxy::ppc_f32_binary(
        galaxy::PpcFloatBinaryOperation::Add, 0x3F800000u, 0x33800000u, 0);
    const auto upward_sum = galaxy::ppc_f32_binary(
        galaxy::PpcFloatBinaryOperation::Add, 0x3F800000u, 0x33800000u, 2);
    passed &= expect(nearest_sum.bits == std::bit_cast<std::uint64_t>(1.0),
                     "nearest-even single tie rounds down to even");
    passed &= expect(nearest_sum.inexact && !nearest_sum.rounded_up,
                     "nearest-even tie records inexact without increment");
    passed &= expect(upward_sum.bits == galaxy::widen_f32_bits(0x3F800001u),
                     "round-toward-positive single tie increments");
    passed &= expect(upward_sum.inexact && upward_sum.rounded_up,
                     "round-toward-positive records fraction increment");

    const auto gx_integer_conversion = galaxy::ppc_f64_binary_to_f32(
        galaxy::PpcFloatBinaryOperation::Subtract,
        0x4330000000000280ull,
        0x4330000000000000ull,
        0);
    passed &= expect(
        gx_integer_conversion.bits == std::bit_cast<std::uint64_t>(640.0),
        "single-result subtract accepts full-width GX integer conversion operands");
    passed &= expect(
        gx_integer_conversion.exception_bits == 0,
        "exact GX integer conversion has no floating-point exception");

    const auto fused = galaxy::ppc_f32_ternary(
        galaxy::PpcFloatTernaryOperation::MultiplyAdd,
        0x3FC00000u,
        0x40000000u,
        0x3F000000u,
        0);
    passed &= expect(fused.bits == std::bit_cast<std::uint64_t>(3.5),
                     "single fused multiply-add result");
    const auto fused_round_down = galaxy::ppc_f32_ternary(
        galaxy::PpcFloatTernaryOperation::MultiplyAdd,
        0x3F800000u,
        0x3F800000u,
        0x33000000u,
        0);
    passed &= expect(
        fused_round_down.bits == std::bit_cast<std::uint64_t>(1.0),
        "single fused multiply-add rounds a sub-half-ulp addend down");
    passed &= expect(
        fused_round_down.inexact && !fused_round_down.rounded_up,
        "single fused multiply-add records down-rounded inexact status");
    const auto fused_round_up = galaxy::ppc_f32_ternary(
        galaxy::PpcFloatTernaryOperation::MultiplyAdd,
        0x3F800000u,
        0x3F800000u,
        0x33C00000u,
        0);
    passed &= expect(
        fused_round_up.bits == galaxy::widen_f32_bits(0x3F800001u),
        "single fused multiply-add rounds a three-quarter-ulp addend up");
    passed &= expect(
        fused_round_up.inexact && fused_round_up.rounded_up,
        "single fused multiply-add records up-rounded inexact status");
    const auto multiply_invalid = galaxy::ppc_f32_ternary(
        galaxy::PpcFloatTernaryOperation::MultiplyAdd,
        0,
        0x7F800000u,
        0x3F800000u,
        0);
    passed &= expect((multiply_invalid.exception_bits & 0x00100000u) != 0,
                     "fused zero times infinity reports VXIMZ");
    const auto add_invalid = galaxy::ppc_f64_ternary(
        galaxy::PpcFloatTernaryOperation::MultiplyAdd,
        0x7FF0000000000000ull,
        0x3FF0000000000000ull,
        0xFFF0000000000000ull,
        0);
    passed &= expect((add_invalid.exception_bits & 0x00800000u) != 0,
                     "fused opposite infinities report VXISI");
    const auto negative_nan = galaxy::ppc_f32_ternary(
        galaxy::PpcFloatTernaryOperation::NegativeMultiplyAdd,
        0xFFC00001u,
        0x3F800000u,
        0x3F800000u,
        0);
    passed &= expect((negative_nan.bits >> 63) != 0,
                     "negative fused operations preserve a propagated NaN sign");

    const auto reciprocal =
        galaxy::ppc_f32_reciprocal_estimate(0x40000000u, 0);
    passed &= expect(reciprocal.bits == std::bit_cast<std::uint64_t>(0.5),
                     "fres estimates the reciprocal of two");
    passed &= expect(
        (galaxy::narrow_f64_to_f32_bits(reciprocal.bits, nullptr, 0) &
         0x00000FFFu) == 0,
        "fres leaves only twelve significand bits");
    const auto negative_zero_reciprocal =
        galaxy::ppc_f32_reciprocal_estimate(0x80000000u, 0);
    passed &= expect(
        negative_zero_reciprocal.bits == 0xFFF0000000000000ull,
        "fres preserves the sign of zero in its infinity result");
    passed &= expect(
        (negative_zero_reciprocal.exception_bits & 0x04000000u) != 0,
        "fres zero reports ZX");

    const auto reciprocal_root =
        galaxy::ppc_f64_reciprocal_sqrt_estimate(
            std::bit_cast<std::uint64_t>(4.0), 0);
    passed &= expect(
        reciprocal_root.bits == std::bit_cast<std::uint64_t>(0.5),
        "frsqrte estimates the reciprocal square root of four");
    const auto invalid_root =
        galaxy::ppc_f64_reciprocal_sqrt_estimate(
            std::bit_cast<std::uint64_t>(-1.0), 0);
    passed &= expect((invalid_root.exception_bits & 0x00000200u) != 0,
                     "negative frsqrte input reports VXSQRT");
    const auto paired_root =
        galaxy::ppc_f32_reciprocal_sqrt_estimate(0x40800000u, 0);
    passed &= expect(paired_root.bits == std::bit_cast<std::uint64_t>(0.5),
                     "paired reciprocal square root estimate");

    galaxy::PpcContext context{};
    galaxy::ppc_commit_scalar_result(
        &context, 3, upward_sum, true, true, nullptr, 0);
    passed &= expect(context.fpr_bits[3] == upward_sum.bits,
                     "scalar result commit writes lane zero");
    passed &= expect((context.fpscr & 0x02060000u) == 0x02060000u,
                     "scalar result commit records XX, FR, and FI");
    passed &= expect(((context.cr >> 24) & 0xFu) == ((context.fpscr >> 28) & 0xFu),
                     "record form copies FPSCR summary to CR1");

    context = {};
    context.hid2 = 0x20000000u;
    galaxy::ppc_commit_scalar_result(
        &context, 4, single_sum, true, false, nullptr, 0);
    passed &= expect(context.fpr_bits[4] == context.ps1_bits[4],
                     "single arithmetic duplicates its result in paired mode");

    context = {};
    const auto negative_lane = galaxy::ppc_f32_passthrough(0xBF800000u);
    const auto positive_lane = galaxy::ppc_f32_passthrough(0x3F800000u);
    galaxy::ppc_commit_paired_result(
        &context,
        5,
        negative_lane,
        positive_lane,
        true,
        false,
        nullptr,
        0);
    passed &= expect((context.fpscr & 0x0001F000u) == 0x00004000u,
                     "paired commit can source FPRF from lane one");

    if (!passed) {
        return 1;
    }
    std::cout << "PowerPC floating-point wrapper tests passed\n";
    return 0;
}
