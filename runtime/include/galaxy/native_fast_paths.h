// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Native fast paths consumed by the generated RMGE01 module.
//
// Every helper here has the same observable guest result as the checked helper
// it shadows. Each one proves its own admission in a handful of integer tests
// and falls back to the complete checked/exact helper for everything it cannot
// prove: device addresses, unmapped spans, non-finite or subnormal operands,
// results that sit on a binary32 rounding midpoint, non-default rounding modes,
// and every FPSCR status consumer. The only deliberate difference is that the
// native arithmetic paths do not maintain the FPSCR status bits (FPRF, FI, FR
// and the sticky exception bits) on their fast path; those remain exact on the
// fallback path and at every instruction that consumes them.
//
// Fast float (GALAXY_FAST_FLOAT=1, the release module): the float helpers run
// native IEEE arithmetic without the exact fallback. See the "Fast float"
// section below for what that does and does not reproduce.
//
// This header is included at the end of galaxy/native_api.h after every guest
// helper it relies on has been declared.

#include <cmath>

#ifndef GALAXY_FAST_FLOAT
#define GALAXY_FAST_FLOAT 0
#endif

namespace galaxy {
namespace fast {

inline constexpr bool kFastFloat = GALAXY_FAST_FLOAT != 0;

// ---------------------------------------------------------------------------
// Flat RAM access
// ---------------------------------------------------------------------------

// Byte size of the RAM alias selected by the top address nibble. Zero for every
// non-RAM nibble, so an unmapped or device address can never pass the span
// test. MEM1 aliases: 0x0, 0x8, 0xC. MEM2 aliases: 0x1, 0x9, 0xD.
inline constexpr std::uint32_t kFlatAliasBytes[16] = {
    0x01800000u, 0x04000000u, 0u, 0u, 0u, 0u, 0u, 0u,
    0x01800000u, 0x04000000u, 0u, 0u, 0x01800000u, 0x04000000u, 0u, 0u,
};

GALAXY_ALWAYS_INLINE bool flat_span(std::uint32_t address, std::uint32_t width) noexcept {
    // offset < 2^28 and width <= 8, so the sum cannot wrap.
    return (address & 0x0FFFFFFFu) + width <= kFlatAliasBytes[address >> 28u];
}

GALAXY_ALWAYS_INLINE std::byte* flat_base(const GuestMemoryV1* memory) noexcept {
    return memory != nullptr && memory->struct_size >= sizeof(GuestMemoryV1)
        ? memory->flat_guest_base
        : nullptr;
}

GALAXY_ALWAYS_INLINE bool wgpipe_address(std::uint32_t address) noexcept {
    return (address & 0xFFFFF000u) == 0xCC008000u ||
           (address & 0xFFFFF000u) == 0x0C008000u;
}

// Appends one guest store's big-endian bytes to the host's 32-byte gather
// buffer without a device-dispatch call. The host publishes the buffer only
// when no FIFO diagnostic is active; a burst boundary is flushed through the
// host callback exactly as the device path does.
template <std::size_t Width>
GALAXY_ALWAYS_INLINE bool wgpipe_append(
    GuestMemoryV1* memory,
    const std::byte* big_endian_bytes) noexcept {
    std::byte* const gather = memory->wgpipe_gather_fast;
    std::uint32_t* const count = memory->wgpipe_gather_count_fast;
    if (gather == nullptr || count == nullptr || memory->wgpipe_flush == nullptr) {
        return false;
    }
    const std::uint32_t used = *count;
    if (used + Width > 32u) {
        return false;
    }
    std::memcpy(gather + used, big_endian_bytes, Width);
    const std::uint32_t next = used + static_cast<std::uint32_t>(Width);
    *count = next;
    if (next == 32u) {
        return memory->wgpipe_flush(memory->user);
    }
    return true;
}

template <typename T>
GALAXY_ALWAYS_INLINE T swap_to_big_endian(T value) noexcept {
    if constexpr (sizeof(T) == 2u) {
        return byte_swap_u16(value);
    } else if constexpr (sizeof(T) == 4u) {
        return byte_swap_u32(value);
    } else if constexpr (sizeof(T) == 8u) {
        return byte_swap_u64(value);
    } else {
        return value;
    }
}

template <typename T>
GALAXY_ALWAYS_INLINE void checked_store(
    GuestMemoryV1* memory,
    std::uint32_t address,
    T value,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    if constexpr (sizeof(T) == 1u) {
        guest_store_u8(memory, address, value, services, guest_pc);
    } else if constexpr (sizeof(T) == 2u) {
        guest_store_u16(memory, address, value, services, guest_pc);
    } else if constexpr (sizeof(T) == 4u) {
        guest_store_u32(memory, address, value, services, guest_pc);
    } else {
        guest_store_u64(memory, address, value, services, guest_pc);
    }
}

// Flat store: RAM spans write through the shared guest view with no
// publication (ABI 25, flush-based coherency). On the Wii a CPU store lands in
// the data cache; the GP and DSP observe it only after dcbf/dcbst (DCFlushRange,
// DCStoreRange), DMA, or the write-gather pipe. The translator therefore
// publishes dirty lines from the cache instructions (dcache_flush_line below)
// and the host publishes its own DMA/device writes. Gather-pipe stores append
// in place. Everything else keeps the checked device/fault path.
template <typename T>
GALAXY_ALWAYS_INLINE void store(
    std::byte* base,
    GuestMemoryV1* memory,
    std::uint32_t address,
    T value,
    const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    static_assert(std::is_integral_v<T> && std::is_unsigned_v<T>);
    const T swapped = swap_to_big_endian<T>(value);
    if (base != nullptr && flat_span(address, sizeof(T))) GALAXY_LIKELY {
        std::memcpy(base + address, &swapped, sizeof(T));
        return;
    }
    if (wgpipe_address(address) && memory != nullptr) {
        if (wgpipe_append<sizeof(T)>(
                memory, reinterpret_cast<const std::byte*>(&swapped))) {
            return;
        }
    }
    checked_store<T>(memory, address, value, services, guest_pc);
}

// dcbf / dcbst: the 32-byte data cache line containing `address` reaches RAM.
// Publishes that line to the renderer and CPU dirty trackers exactly as a
// 32-byte guest write would; this is the only publication translated stores
// perform (see `store` above).
GALAXY_ALWAYS_INLINE void dcache_flush_line(
    GuestMemoryV1* memory,
    std::uint32_t address) {
    guest_notify_write(memory, address & 0xFFFFFFE0u, 32u);
}

GALAXY_ALWAYS_INLINE std::uint8_t load_u8(
    const std::byte* base, GuestMemoryV1* memory, std::uint32_t address,
    const NativeServicesV1* services, std::uint32_t guest_pc) {
    if (base != nullptr && flat_span(address, 1u)) GALAXY_LIKELY {
        return static_cast<std::uint8_t>(base[address]);
    }
    return guest_load_u8(memory, address, services, guest_pc);
}

GALAXY_ALWAYS_INLINE std::uint16_t load_u16(
    const std::byte* base, GuestMemoryV1* memory, std::uint32_t address,
    const NativeServicesV1* services, std::uint32_t guest_pc) {
    if (base != nullptr && flat_span(address, 2u)) GALAXY_LIKELY {
        return guest_flat_load_bytes<std::uint16_t>(base, address);
    }
    return guest_load_u16(memory, address, services, guest_pc);
}

GALAXY_ALWAYS_INLINE std::uint32_t load_u32(
    const std::byte* base, GuestMemoryV1* memory, std::uint32_t address,
    const NativeServicesV1* services, std::uint32_t guest_pc) {
    if (base != nullptr && flat_span(address, 4u)) GALAXY_LIKELY {
        return guest_flat_load_bytes<std::uint32_t>(base, address);
    }
    return guest_load_u32(memory, address, services, guest_pc);
}

GALAXY_ALWAYS_INLINE std::uint64_t load_u64(
    const std::byte* base, GuestMemoryV1* memory, std::uint32_t address,
    const NativeServicesV1* services, std::uint32_t guest_pc) {
    if (base != nullptr && flat_span(address, 8u)) GALAXY_LIKELY {
        return guest_flat_load_bytes<std::uint64_t>(base, address);
    }
    return guest_load_u64(memory, address, services, guest_pc);
}

GALAXY_ALWAYS_INLINE void load_fpr_single(
    const std::byte* base, PpcContext* context, std::uint32_t index,
    GuestMemoryV1* memory, std::uint32_t address,
    const NativeServicesV1* services, std::uint32_t guest_pc) {
    const std::uint64_t widened =
        widen_f32_bits(load_u32(base, memory, address, services, guest_pc));
    context->fpr_bits[index] = widened;
    if (paired_singles_enabled(context)) {
        context->ps1_bits[index] = widened;
    }
}

GALAXY_ALWAYS_INLINE void store_fpr_single(
    std::byte* base, const PpcContext* context, std::uint32_t index,
    GuestMemoryV1* memory, std::uint32_t address,
    const NativeServicesV1* services, std::uint32_t guest_pc) {
    store<std::uint32_t>(
        base, memory, address,
        native_stored_single_bits(context->fpr_bits[index], context->fpscr),
        services, guest_pc);
}

// Quantized paired loads. Type 0 (binary32) is the overwhelmingly common
// configuration; the integer types reuse the exact dequantization helpers.
GALAXY_ALWAYS_INLINE void psq_load(
    const std::byte* base, PpcContext* context, std::uint32_t target,
    GuestMemoryV1* memory, std::uint32_t address, std::uint32_t gqr_index,
    bool one_element, bool d_form, const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    const std::uint32_t gqr = context->gqr[gqr_index];
    const std::uint32_t type = psq_load_type(gqr);
    constexpr std::uint32_t kHid2Lsqe = 0x80000000u;
    constexpr std::uint32_t kHid2Pse = 0x20000000u;
    const std::uint32_t required = kHid2Pse | (d_form ? kHid2Lsqe : 0u);
    if (base != nullptr && (context->hid2 & required) == required) GALAXY_LIKELY {
        if (type == 0u) GALAXY_LIKELY {
            if (flat_span(address, one_element ? 4u : 8u)) GALAXY_LIKELY {
                context->fpr_bits[target] = widen_f32_bits(
                    guest_flat_load_bytes<std::uint32_t>(base, address));
                context->ps1_bits[target] = one_element
                    ? 0x3FF0000000000000ull
                    : widen_f32_bits(
                          guest_flat_load_bytes<std::uint32_t>(base, address + 4u));
                return;
            }
        } else if (type >= 4u && type <= 7u) {
            const std::uint32_t element = (type & 1u) != 0u ? 2u : 1u;
            if (flat_span(address, one_element ? element : element * 2u)) GALAXY_LIKELY {
                const std::int32_t scale = psq_load_scale(gqr);
                const auto load_lane = [&](std::uint32_t lane_address) -> std::uint64_t {
                    switch (type) {
                    case 4u:
                        return psq_load_integer_bits(
                            static_cast<std::int32_t>(
                                static_cast<std::uint8_t>(base[lane_address])),
                            scale);
                    case 5u:
                        return psq_load_integer_bits(
                            static_cast<std::int32_t>(
                                guest_flat_load_bytes<std::uint16_t>(base, lane_address)),
                            scale);
                    case 6u:
                        return psq_load_integer_bits(
                            static_cast<std::int32_t>(
                                static_cast<std::int8_t>(base[lane_address])),
                            scale);
                    default:
                        return psq_load_integer_bits(
                            static_cast<std::int32_t>(static_cast<std::int16_t>(
                                guest_flat_load_bytes<std::uint16_t>(base, lane_address))),
                            scale);
                    }
                };
                context->fpr_bits[target] = load_lane(address);
                context->ps1_bits[target] = one_element
                    ? 0x3FF0000000000000ull
                    : load_lane(address + element);
                return;
            }
        }
    }
    galaxy::psq_load(
        context, target, memory, address, gqr_index, one_element, d_form,
        services, guest_pc);
}

GALAXY_ALWAYS_INLINE void psq_store(
    std::byte* base, const PpcContext* context, std::uint32_t source,
    GuestMemoryV1* memory, std::uint32_t address, std::uint32_t gqr_index,
    bool one_element, bool d_form, const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    const std::uint32_t gqr = context->gqr[gqr_index];
    const std::uint32_t type = psq_store_type(gqr);
    constexpr std::uint32_t kHid2Lsqe = 0x80000000u;
    constexpr std::uint32_t kHid2Pse = 0x20000000u;
    const std::uint32_t required = kHid2Pse | (d_form ? kHid2Lsqe : 0u);
    if (base != nullptr && type == 0u &&
        (context->hid2 & required) == required) GALAXY_LIKELY {
        if (one_element) {
            if (flat_span(address, 4u)) GALAXY_LIKELY {
                const std::uint32_t first = byte_swap_u32(
                    narrow_paired_single_ftz(context->fpr_bits[source]));
                std::memcpy(base + address, &first, sizeof(first));
                return;
            }
        } else if (flat_span(address, 8u)) GALAXY_LIKELY {
            const std::uint32_t first = byte_swap_u32(
                narrow_paired_single_ftz(context->fpr_bits[source]));
            const std::uint32_t second = byte_swap_u32(
                narrow_paired_single_ftz(context->ps1_bits[source]));
            std::memcpy(base + address, &first, sizeof(first));
            std::memcpy(base + address + 4u, &second, sizeof(second));
            return;
        }
    }
    galaxy::psq_store(
        context, source, memory, address, gqr_index, one_element, d_form,
        services, guest_pc);
}

// ---------------------------------------------------------------------------
// Native floating point
// ---------------------------------------------------------------------------

// Finite and either zero or normal: the operand class for which native IEEE
// binary64 arithmetic under nearest-even rounding reproduces the Gekko result
// bit for bit (NI-mode input flushing cannot change an ordinary operand).
GALAXY_ALWAYS_INLINE bool f64_ordinary(std::uint64_t bits) noexcept {
    const std::uint32_t exponent = static_cast<std::uint32_t>(bits >> 52u) & 0x7FFu;
    return (exponent - 1u) < 0x7FEu || (bits & 0x7FFFFFFFFFFFFFFFull) == 0u;
}

GALAXY_ALWAYS_INLINE bool f32_ordinary(std::uint32_t bits) noexcept {
    const std::uint32_t exponent = (bits >> 23u) & 0xFFu;
    return (exponent - 1u) < 0xFEu || (bits & 0x7FFFFFFFu) == 0u;
}

// A binary64 value exactly halfway between two adjacent binary32 values. Only
// there can rounding the double result to single disagree with rounding the
// exact result to single, so these take the exact path.
GALAXY_ALWAYS_INLINE bool f64_is_single_midpoint(std::uint64_t bits) noexcept {
    return (bits & 0x1FFFFFFFull) == 0x10000000ull;
}

// Gekko rounds the multiplier of every single-precision multiply to 25
// significant bits (round half up at fraction bit 27) before multiplying.
GALAXY_ALWAYS_INLINE std::uint64_t round_multiplier_25(std::uint64_t bits) noexcept {
    return (bits + 0x0000000008000000ull) & 0xFFFFFFFFF0000000ull;
}

GALAXY_ALWAYS_INLINE bool round_nearest(const PpcContext* context) noexcept {
    return (context->fpscr & 0x3u) == 0u;
}

GALAXY_ALWAYS_INLINE void commit_f64(
    PpcContext* context, std::uint32_t target, std::uint64_t bits, bool record) {
    context->fpr_bits[target] = bits;
    if (record) {
        update_cr1_from_fpscr(context);
    }
}

GALAXY_ALWAYS_INLINE void commit_single(
    PpcContext* context, std::uint32_t target, std::uint32_t f32_bits,
    bool duplicate, bool record) {
    const std::uint64_t widened = widen_f32_bits(f32_bits);
    context->fpr_bits[target] = widened;
    if (duplicate && paired_singles_enabled(context)) {
        context->ps1_bits[target] = widened;
    }
    if (record) {
        update_cr1_from_fpscr(context);
    }
}

// Rounds an ordinary binary64 result to binary32 when that rounding is
// provably the same as rounding the exact value. Returns false on a midpoint,
// overflow or a subnormal single.
GALAXY_ALWAYS_INLINE bool narrow_result(double value, std::uint32_t& f32_bits) noexcept {
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
    if (!f64_ordinary(bits) || f64_is_single_midpoint(bits)) {
        return false;
    }
    const float narrowed = static_cast<float>(value);
    const std::uint32_t narrowed_bits = std::bit_cast<std::uint32_t>(narrowed);
    if (!f32_ordinary(narrowed_bits)) {
        return false;
    }
    f32_bits = narrowed_bits;
    return true;
}

template <PpcFloatBinaryOperation Operation>
GALAXY_ALWAYS_INLINE double binary_op(double left, double right) noexcept {
    if constexpr (Operation == PpcFloatBinaryOperation::Add) {
        return left + right;
    } else if constexpr (Operation == PpcFloatBinaryOperation::Subtract) {
        return left - right;
    } else if constexpr (Operation == PpcFloatBinaryOperation::Multiply) {
        return left * right;
    } else {
        return left / right;
    }
}

template <PpcFloatBinaryOperation Operation>
GALAXY_ALWAYS_INLINE float binary_op_f32(float left, float right) noexcept {
    if constexpr (Operation == PpcFloatBinaryOperation::Add) {
        return left + right;
    } else if constexpr (Operation == PpcFloatBinaryOperation::Subtract) {
        return left - right;
    } else if constexpr (Operation == PpcFloatBinaryOperation::Multiply) {
        return left * right;
    } else {
        return left / right;
    }
}

template <PpcFloatTernaryOperation Operation>
GALAXY_ALWAYS_INLINE double ternary_op(double a, double c, double b) noexcept {
    if constexpr (Operation == PpcFloatTernaryOperation::MultiplyAdd) {
        return std::fma(a, c, b);
    } else if constexpr (Operation == PpcFloatTernaryOperation::MultiplySubtract) {
        return std::fma(a, c, -b);
    } else if constexpr (Operation == PpcFloatTernaryOperation::NegativeMultiplyAdd) {
        return -std::fma(a, c, b);
    } else {
        return -std::fma(a, c, -b);
    }
}

template <PpcFloatTernaryOperation Operation>
GALAXY_ALWAYS_INLINE float ternary_op_f32(float a, float c, float b) noexcept {
    if constexpr (Operation == PpcFloatTernaryOperation::MultiplyAdd) {
        return std::fmaf(a, c, b);
    } else if constexpr (Operation == PpcFloatTernaryOperation::MultiplySubtract) {
        return std::fmaf(a, c, -b);
    } else if constexpr (Operation == PpcFloatTernaryOperation::NegativeMultiplyAdd) {
        return -std::fmaf(a, c, b);
    } else {
        return -std::fmaf(a, c, -b);
    }
}

// Out-of-line exact tails. One call per site keeps the inline fast path small;
// these run only for the operands the fast path declines.
template <PpcFloatBinaryOperation Operation, bool Single>
GALAXY_NOINLINE inline void scalar_binary_exact(
    PpcContext* context, std::uint32_t target, std::uint32_t left_register,
    std::uint32_t right_register, bool record, const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    if constexpr (Single) {
        const PpcFloatResult exact = ppc_f64_binary_to_f32(
            Operation, context->fpr_bits[left_register],
            context->fpr_bits[right_register], context->fpscr);
        ppc_commit_scalar_result(context, target, exact, true, record, services, guest_pc);
    } else {
        const PpcFloatResult exact = ppc_f64_binary(
            Operation, context->fpr_bits[left_register],
            context->fpr_bits[right_register], context->fpscr);
        ppc_commit_scalar_result(context, target, exact, false, record, services, guest_pc);
    }
}

template <PpcFloatTernaryOperation Operation, bool Single>
GALAXY_NOINLINE inline void scalar_ternary_exact(
    PpcContext* context, std::uint32_t target, std::uint32_t multiplicand_register,
    std::uint32_t multiplier_register, std::uint32_t addend_register, bool record,
    const NativeServicesV1* services, std::uint32_t guest_pc) {
    if constexpr (Single) {
        ppc_commit_scalar_single_ternary(
            context, target, Operation, multiplicand_register, multiplier_register,
            addend_register, record, services, guest_pc);
    } else {
        const PpcFloatResult exact = ppc_f64_ternary(
            Operation, context->fpr_bits[multiplicand_register],
            context->fpr_bits[multiplier_register],
            context->fpr_bits[addend_register], context->fpscr);
        ppc_commit_scalar_result(context, target, exact, false, record, services, guest_pc);
    }
}

GALAXY_NOINLINE inline void scalar_frsp_exact(
    PpcContext* context, std::uint32_t target, std::uint32_t source_register,
    bool record, const NativeServicesV1* services, std::uint32_t guest_pc) {
    const PpcFloatResult exact =
        ppc_round_f64_to_f32(context->fpr_bits[source_register], context->fpscr);
    ppc_commit_scalar_result(context, target, exact, false, record, services, guest_pc);
}

// ---------------------------------------------------------------------------
// Fast float
// ---------------------------------------------------------------------------
//
// GALAXY_FAST_FLOAT=1 selects native IEEE arithmetic for every Gekko float
// instruction the helpers below cover, with no exact fallback:
//   - binary64 operations run as written; the single-precision forms compute
//     in binary64 and round once to binary32 (held widened, as the register
//     file always is);
//   - fmuls/fmadds/ps_mul*/ps_madd* round the multiplier to 25 significant
//     bits first, as the hardware does;
//   - the fmadd family is a fused multiply-add; paired singles compute both
//     lanes in one binary64 vector;
//   - fctiwz saturates like the hardware and reports no exception.
// Not reproduced: FPSCR status bits (FPRF, FI, FR, sticky exceptions), NaN
// payload and sign propagation, non-nearest rounding modes, and binary32
// double-rounding corners. Denormals follow the host MXCSR; the runtime sets
// flush-to-zero and denormals-are-zero on the simulation thread for this mode
// (the Gekko's non-IEEE mode). This is the accuracy class of Dolphin's JIT and
// WiiCompiled, not the bit-exact model of the exact paths, and it exists
// because the exact fallback calls kept the host compiler from holding guest
// floats in registers.
namespace fast_float_detail {

GALAXY_ALWAYS_INLINE double as_f64(std::uint64_t bits) noexcept {
    return std::bit_cast<double>(bits);
}

GALAXY_ALWAYS_INLINE std::uint64_t as_bits(double value) noexcept {
    return std::bit_cast<std::uint64_t>(value);
}

// A binary32 result as the binary64 bits the register file holds.
GALAXY_ALWAYS_INLINE std::uint64_t single_result_bits(double value) noexcept {
    return as_bits(static_cast<double>(static_cast<float>(value)));
}

GALAXY_ALWAYS_INLINE void write_single(
    const PpcContext* context, std::uint64_t* fpr, std::uint64_t* ps1,
    std::uint32_t target, std::uint64_t bits, bool duplicate) noexcept {
    fpr[target] = bits;
    if (duplicate && paired_singles_enabled(context)) {
        ps1[target] = bits;
    }
}

template <PpcFloatBinaryOperation Operation, bool Single>
GALAXY_ALWAYS_INLINE void scalar_binary_fast(
    PpcContext* context, std::uint64_t* fpr, std::uint64_t* ps1, std::uint32_t target,
    std::uint32_t left_register, std::uint32_t right_register, bool record) noexcept {
    const double left = as_f64(fpr[left_register]);
    std::uint64_t right_bits = fpr[right_register];
    if constexpr (Single && Operation == PpcFloatBinaryOperation::Multiply) {
        right_bits = round_multiplier_25(right_bits);
    }
    const double result = binary_op<Operation>(left, as_f64(right_bits));
    if constexpr (Single) {
        write_single(context, fpr, ps1, target, single_result_bits(result), true);
    } else {
        fpr[target] = as_bits(result);
    }
    if (record) {
        update_cr1_from_fpscr(context);
    }
}

template <PpcFloatTernaryOperation Operation, bool Single>
GALAXY_ALWAYS_INLINE void scalar_ternary_fast(
    PpcContext* context, std::uint64_t* fpr, std::uint64_t* ps1, std::uint32_t target,
    std::uint32_t multiplicand_register, std::uint32_t multiplier_register,
    std::uint32_t addend_register, bool record) noexcept {
    const double a = as_f64(fpr[multiplicand_register]);
    std::uint64_t c_bits = fpr[multiplier_register];
    if constexpr (Single) {
        c_bits = round_multiplier_25(c_bits);
    }
    const double b = as_f64(fpr[addend_register]);
    const double result = ternary_op<Operation>(a, as_f64(c_bits), b);
    if constexpr (Single) {
        write_single(context, fpr, ps1, target, single_result_bits(result), true);
    } else {
        fpr[target] = as_bits(result);
    }
    if (record) {
        update_cr1_from_fpscr(context);
    }
}

GALAXY_ALWAYS_INLINE void scalar_frsp_fast(
    PpcContext* context, std::uint64_t* fpr, std::uint64_t* ps1, std::uint32_t target,
    std::uint32_t source_register, bool record) noexcept {
    write_single(
        context, fpr, ps1, target, single_result_bits(as_f64(fpr[source_register])), false);
    if (record) {
        update_cr1_from_fpscr(context);
    }
}

// fctiwz: truncating binary64 to int32. cvttsd2si yields INT32_MIN for NaN
// and for both overflow directions; the hardware saturates positive overflow
// to INT32_MAX. The high word matches the exact helper.
GALAXY_ALWAYS_INLINE std::uint64_t fctiwz_bits(std::uint64_t source) noexcept {
    const double value = as_f64(source);
    std::int32_t integer = _mm_cvttsd_si32(_mm_set_sd(value));
    if (integer == static_cast<std::int32_t>(0x80000000u) && value > 0.0) {
        integer = 0x7FFFFFFF;
    }
    return 0xFFF8000000000000ull | static_cast<std::uint32_t>(integer);
}

}  // namespace fast_float_detail

// Scalar fadd/fsub/fmul/fdiv and their single-precision forms.
template <PpcFloatBinaryOperation Operation, bool Single>
GALAXY_ALWAYS_INLINE void scalar_binary(
    PpcContext* context, std::uint32_t target, std::uint32_t left_register,
    std::uint32_t right_register, bool record, const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    if constexpr (kFastFloat) {
        fast_float_detail::scalar_binary_fast<Operation, Single>(context, context->fpr_bits, context->ps1_bits, target, left_register, right_register, record);
    } else {
        const std::uint64_t left = context->fpr_bits[left_register];
        std::uint64_t right = context->fpr_bits[right_register];
        if (round_nearest(context) && f64_ordinary(left) && f64_ordinary(right)) GALAXY_LIKELY {
            if constexpr (Single && Operation == PpcFloatBinaryOperation::Multiply) {
                right = round_multiplier_25(right);
            }
            const double result = binary_op<Operation>(
                std::bit_cast<double>(left), std::bit_cast<double>(right));
            if constexpr (Single) {
                std::uint32_t narrowed = 0u;
                if (narrow_result(result, narrowed)) GALAXY_LIKELY {
                    commit_single(context, target, narrowed, true, record);
                    return;
                }
            } else {
                const std::uint64_t bits = std::bit_cast<std::uint64_t>(result);
                if (f64_ordinary(bits)) GALAXY_LIKELY {
                    commit_f64(context, target, bits, record);
                    return;
                }
            }
        }
        scalar_binary_exact<Operation, Single>(
            context, target, left_register, right_register, record, services, guest_pc);
    }
}

// Scalar fmadd family. `multiplicand` is frA, `multiplier` frC, `addend` frB.
template <PpcFloatTernaryOperation Operation, bool Single>
GALAXY_ALWAYS_INLINE void scalar_ternary(
    PpcContext* context, std::uint32_t target, std::uint32_t multiplicand_register,
    std::uint32_t multiplier_register, std::uint32_t addend_register, bool record,
    const NativeServicesV1* services, std::uint32_t guest_pc) {
    if constexpr (kFastFloat) {
        fast_float_detail::scalar_ternary_fast<Operation, Single>(context, context->fpr_bits, context->ps1_bits, target, multiplicand_register, multiplier_register, addend_register, record);
    } else {
        const std::uint64_t a = context->fpr_bits[multiplicand_register];
        std::uint64_t c = context->fpr_bits[multiplier_register];
        const std::uint64_t b = context->fpr_bits[addend_register];
        if (round_nearest(context) && f64_ordinary(a) && f64_ordinary(c) &&
            f64_ordinary(b)) GALAXY_LIKELY {
            if constexpr (Single) {
                c = round_multiplier_25(c);
            }
            const double result = ternary_op<Operation>(
                std::bit_cast<double>(a), std::bit_cast<double>(c),
                std::bit_cast<double>(b));
            if constexpr (Single) {
                std::uint32_t narrowed = 0u;
                if (narrow_result(result, narrowed)) GALAXY_LIKELY {
                    commit_single(context, target, narrowed, true, record);
                    return;
                }
            } else {
                const std::uint64_t bits = std::bit_cast<std::uint64_t>(result);
                if (f64_ordinary(bits)) GALAXY_LIKELY {
                    commit_f64(context, target, bits, record);
                    return;
                }
            }
        }
        scalar_ternary_exact<Operation, Single>(
            context, target, multiplicand_register, multiplier_register, addend_register,
            record, services, guest_pc);
    }
}

GALAXY_ALWAYS_INLINE void scalar_frsp(
    PpcContext* context, std::uint32_t target, std::uint32_t source_register,
    bool record, const NativeServicesV1* services, std::uint32_t guest_pc) {
    if constexpr (kFastFloat) {
        fast_float_detail::scalar_frsp_fast(context, context->fpr_bits, context->ps1_bits, target, source_register, record);
    } else {
        const std::uint64_t source = context->fpr_bits[source_register];
        if (round_nearest(context) && f64_ordinary(source)) GALAXY_LIKELY {
            std::uint32_t narrowed = 0u;
            if (narrow_result(std::bit_cast<double>(source), narrowed)) GALAXY_LIKELY {
                commit_single(context, target, narrowed, false, record);
                return;
            }
        }
        scalar_frsp_exact(context, target, source_register, record, services, guest_pc);
    }
}

// Lane selection for the paired multiplier/right operand.
enum class PairedLane : std::uint8_t {
    Each,   // ps0 with ps0, ps1 with ps1
    Lane0,  // both lanes use the operand's ps0
    Lane1,  // both lanes use the operand's ps1
};

GALAXY_ALWAYS_INLINE bool paired_mode(const PpcContext* context) noexcept {
    constexpr std::uint32_t kHid2Pse = 0x20000000u;
    return (context->hid2 & kHid2Pse) != 0u;
}

GALAXY_ALWAYS_INLINE void commit_paired(
    PpcContext* context, std::uint32_t target, std::uint32_t lane0,
    std::uint32_t lane1, bool record) {
    context->fpr_bits[target] = widen_f32_bits(lane0);
    context->ps1_bits[target] = widen_f32_bits(lane1);
    if (record) {
        update_cr1_from_fpscr(context);
    }
}

namespace fast_float_detail {

// Both lanes of a paired-single register as one binary64 vector: lane 0 is
// ps0, lane 1 is ps1.
#if defined(_M_X64) || defined(__x86_64__)
using Pair = __m128d;

GALAXY_ALWAYS_INLINE Pair make_pair(std::uint64_t ps0, std::uint64_t ps1) noexcept {
    return _mm_castsi128_pd(
        _mm_set_epi64x(static_cast<long long>(ps1), static_cast<long long>(ps0)));
}

template <PairedLane Lane>
GALAXY_ALWAYS_INLINE Pair select_lanes(std::uint64_t ps0, std::uint64_t ps1) noexcept {
    if constexpr (Lane == PairedLane::Lane0) {
        return _mm_set1_pd(as_f64(ps0));
    } else if constexpr (Lane == PairedLane::Lane1) {
        return _mm_set1_pd(as_f64(ps1));
    } else {
        return make_pair(ps0, ps1);
    }
}

GALAXY_ALWAYS_INLINE std::uint64_t lane0_bits(Pair value) noexcept {
    return as_bits(_mm_cvtsd_f64(value));
}

GALAXY_ALWAYS_INLINE std::uint64_t lane1_bits(Pair value) noexcept {
    return as_bits(_mm_cvtsd_f64(_mm_unpackhi_pd(value, value)));
}

// Both lanes rounded once to binary32 and widened again.
GALAXY_ALWAYS_INLINE Pair round_to_single(Pair value) noexcept {
    return _mm_cvtps_pd(_mm_cvtpd_ps(value));
}

GALAXY_ALWAYS_INLINE Pair round_multiplier_25(Pair value) noexcept {
    const __m128i bits = _mm_castpd_si128(value);
    return _mm_castsi128_pd(_mm_and_si128(
        _mm_add_epi64(bits, _mm_set1_epi64x(0x0000000008000000ll)),
        _mm_set1_epi64x(static_cast<long long>(0xFFFFFFFFF0000000ull))));
}

template <PpcFloatBinaryOperation Operation>
GALAXY_ALWAYS_INLINE Pair binary_pair(Pair left, Pair right) noexcept {
    if constexpr (Operation == PpcFloatBinaryOperation::Add) {
        return _mm_add_pd(left, right);
    } else if constexpr (Operation == PpcFloatBinaryOperation::Subtract) {
        return _mm_sub_pd(left, right);
    } else if constexpr (Operation == PpcFloatBinaryOperation::Multiply) {
        return _mm_mul_pd(left, right);
    } else {
        return _mm_div_pd(left, right);
    }
}

// a*c+b, a*c-b, -(a*c+b), -(a*c-b): the same results as ternary_op per lane.
template <PpcFloatTernaryOperation Operation>
GALAXY_ALWAYS_INLINE Pair ternary_pair(Pair a, Pair c, Pair b) noexcept {
#if defined(__FMA__) || defined(__AVX2__)
    if constexpr (Operation == PpcFloatTernaryOperation::MultiplyAdd) {
        return _mm_fmadd_pd(a, c, b);
    } else if constexpr (Operation == PpcFloatTernaryOperation::MultiplySubtract) {
        return _mm_fmsub_pd(a, c, b);
    } else if constexpr (Operation == PpcFloatTernaryOperation::NegativeMultiplyAdd) {
        return _mm_fnmsub_pd(a, c, b);
    } else {
        return _mm_fnmadd_pd(a, c, b);
    }
#else
    const double lane0 = ternary_op<Operation>(
        _mm_cvtsd_f64(a), _mm_cvtsd_f64(c), _mm_cvtsd_f64(b));
    const double lane1 = ternary_op<Operation>(
        _mm_cvtsd_f64(_mm_unpackhi_pd(a, a)), _mm_cvtsd_f64(_mm_unpackhi_pd(c, c)),
        _mm_cvtsd_f64(_mm_unpackhi_pd(b, b)));
    return _mm_set_pd(lane1, lane0);
#endif
}
#else
#error "fast float paired singles require x86-64"
#endif

GALAXY_ALWAYS_INLINE void write_pair(
    PpcContext* context, std::uint64_t* fpr, std::uint64_t* ps1, std::uint32_t target,
    Pair value, bool record) noexcept {
    fpr[target] = lane0_bits(value);
    ps1[target] = lane1_bits(value);
    if (record) {
        update_cr1_from_fpscr(context);
    }
}

template <PpcFloatBinaryOperation Operation, PairedLane Lane>
GALAXY_ALWAYS_INLINE void paired_binary_fast(
    PpcContext* context, std::uint64_t* fpr, std::uint64_t* ps1, std::uint32_t target,
    std::uint32_t left_register, std::uint32_t right_register, bool record) noexcept {
    const Pair left = make_pair(fpr[left_register], ps1[left_register]);
    Pair right = select_lanes<Lane>(fpr[right_register], ps1[right_register]);
    if constexpr (Operation == PpcFloatBinaryOperation::Multiply) {
        right = round_multiplier_25(right);
    }
    write_pair(
        context, fpr, ps1, target, round_to_single(binary_pair<Operation>(left, right)),
        record);
}

template <PpcFloatTernaryOperation Operation, PairedLane Lane>
GALAXY_ALWAYS_INLINE void paired_ternary_fast(
    PpcContext* context, std::uint64_t* fpr, std::uint64_t* ps1, std::uint32_t target,
    std::uint32_t multiplicand_register, std::uint32_t multiplier_register,
    std::uint32_t addend_register, bool record) noexcept {
    const Pair a = make_pair(fpr[multiplicand_register], ps1[multiplicand_register]);
    const Pair c = round_multiplier_25(
        select_lanes<Lane>(fpr[multiplier_register], ps1[multiplier_register]));
    const Pair b = make_pair(fpr[addend_register], ps1[addend_register]);
    write_pair(
        context, fpr, ps1, target, round_to_single(ternary_pair<Operation>(a, c, b)),
        record);
}

template <bool SumInLane1>
GALAXY_ALWAYS_INLINE void paired_sum_fast(
    PpcContext* context, std::uint64_t* fpr, std::uint64_t* ps1, std::uint32_t target,
    std::uint32_t left_register, std::uint32_t right_register, std::uint32_t copy_register,
    bool record) noexcept {
    const std::uint64_t sum =
        single_result_bits(as_f64(fpr[left_register]) + as_f64(ps1[right_register]));
    if constexpr (SumInLane1) {
        const std::uint64_t copy = fpr[copy_register];
        fpr[target] = copy;
        ps1[target] = sum;
    } else {
        const std::uint64_t copy = ps1[copy_register];
        fpr[target] = sum;
        ps1[target] = copy;
    }
    if (record) {
        update_cr1_from_fpscr(context);
    }
}

}  // namespace fast_float_detail

GALAXY_NOINLINE inline void paired_binary_exact(
    PpcContext* context, std::uint32_t target, PpcFloatBinaryOperation operation,
    std::uint32_t left_register, std::uint32_t right_register, PairedLane lane,
    bool record, const NativeServicesV1* services, std::uint32_t guest_pc) {
    require_paired_single_mode(context, false, services, guest_pc);
    const std::uint32_t left_ps0 =
        require_single_precision_bits(context->fpr_bits[left_register], services, guest_pc);
    const std::uint32_t left_ps1 =
        require_single_precision_bits(context->ps1_bits[left_register], services, guest_pc);
    const std::uint64_t right0_bits = lane == PairedLane::Lane1
        ? context->ps1_bits[right_register] : context->fpr_bits[right_register];
    const std::uint64_t right1_bits = lane == PairedLane::Lane0
        ? context->fpr_bits[right_register] : context->ps1_bits[right_register];
    const std::uint32_t right_ps0 =
        require_single_precision_bits(right0_bits, services, guest_pc);
    const std::uint32_t right_ps1 =
        require_single_precision_bits(right1_bits, services, guest_pc);
    const PpcFloatResult result_ps0 =
        ppc_f32_binary(operation, left_ps0, right_ps0, context->fpscr);
    const PpcFloatResult result_ps1 =
        ppc_f32_binary(operation, left_ps1, right_ps1, context->fpscr);
    ppc_commit_paired_result(
        context, target, result_ps0, result_ps1, false, record, services, guest_pc);
}

template <PpcFloatBinaryOperation Operation, PairedLane Lane>
GALAXY_ALWAYS_INLINE void paired_binary(
    PpcContext* context, std::uint32_t target, std::uint32_t left_register,
    std::uint32_t right_register, bool record, const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    if constexpr (kFastFloat) {
        fast_float_detail::paired_binary_fast<Operation, Lane>(context, context->fpr_bits, context->ps1_bits, target, left_register, right_register, record);
    } else {
        std::uint32_t l0 = 0u, l1 = 0u, r0 = 0u, r1 = 0u;
        const std::uint64_t right0_bits = Lane == PairedLane::Lane1
            ? context->ps1_bits[right_register] : context->fpr_bits[right_register];
        const std::uint64_t right1_bits = Lane == PairedLane::Lane0
            ? context->fpr_bits[right_register] : context->ps1_bits[right_register];
        using float_native_detail::try_narrow_normal_or_zero_widened_f32;
        if (paired_mode(context) && round_nearest(context) &&
            try_narrow_normal_or_zero_widened_f32(context->fpr_bits[left_register], l0) &&
            try_narrow_normal_or_zero_widened_f32(context->ps1_bits[left_register], l1) &&
            try_narrow_normal_or_zero_widened_f32(right0_bits, r0) &&
            try_narrow_normal_or_zero_widened_f32(right1_bits, r1)) GALAXY_LIKELY {
            const float result0 = binary_op_f32<Operation>(
                std::bit_cast<float>(l0), std::bit_cast<float>(r0));
            const float result1 = binary_op_f32<Operation>(
                std::bit_cast<float>(l1), std::bit_cast<float>(r1));
            const std::uint32_t bits0 = std::bit_cast<std::uint32_t>(result0);
            const std::uint32_t bits1 = std::bit_cast<std::uint32_t>(result1);
            if (f32_ordinary(bits0) && f32_ordinary(bits1)) GALAXY_LIKELY {
                commit_paired(context, target, bits0, bits1, record);
                return;
            }
        }
        paired_binary_exact(
            context, target, Operation, left_register, right_register, Lane, record,
            services, guest_pc);
    }
}

GALAXY_NOINLINE inline void paired_ternary_exact(
    PpcContext* context, std::uint32_t target, PpcFloatTernaryOperation operation,
    std::uint32_t multiplicand_register, std::uint32_t multiplier_register,
    std::uint32_t addend_register, PairedLane lane, bool record,
    const NativeServicesV1* services, std::uint32_t guest_pc) {
    require_paired_single_mode(context, false, services, guest_pc);
    const std::uint32_t multiplicand_ps0 = require_single_precision_bits(
        context->fpr_bits[multiplicand_register], services, guest_pc);
    const std::uint32_t multiplicand_ps1 = require_single_precision_bits(
        context->ps1_bits[multiplicand_register], services, guest_pc);
    const std::uint64_t multiplier0_bits = lane == PairedLane::Lane1
        ? context->ps1_bits[multiplier_register] : context->fpr_bits[multiplier_register];
    const std::uint64_t multiplier1_bits = lane == PairedLane::Lane0
        ? context->fpr_bits[multiplier_register] : context->ps1_bits[multiplier_register];
    const std::uint32_t multiplier_ps0 =
        require_single_precision_bits(multiplier0_bits, services, guest_pc);
    const std::uint32_t multiplier_ps1 =
        require_single_precision_bits(multiplier1_bits, services, guest_pc);
    const std::uint32_t addend_ps0 = require_single_precision_bits(
        context->fpr_bits[addend_register], services, guest_pc);
    const std::uint32_t addend_ps1 = require_single_precision_bits(
        context->ps1_bits[addend_register], services, guest_pc);
    const PpcFloatResult result_ps0 = ppc_f32_ternary(
        operation, multiplicand_ps0, multiplier_ps0, addend_ps0, context->fpscr);
    const PpcFloatResult result_ps1 = ppc_f32_ternary(
        operation, multiplicand_ps1, multiplier_ps1, addend_ps1, context->fpscr);
    ppc_commit_paired_result(
        context, target, result_ps0, result_ps1, false, record, services, guest_pc);
}

template <PpcFloatTernaryOperation Operation, PairedLane Lane>
GALAXY_ALWAYS_INLINE void paired_ternary(
    PpcContext* context, std::uint32_t target, std::uint32_t multiplicand_register,
    std::uint32_t multiplier_register, std::uint32_t addend_register, bool record,
    const NativeServicesV1* services, std::uint32_t guest_pc) {
    if constexpr (kFastFloat) {
        fast_float_detail::paired_ternary_fast<Operation, Lane>(context, context->fpr_bits, context->ps1_bits, target, multiplicand_register, multiplier_register, addend_register, record);
    } else {
        std::uint32_t a0 = 0u, a1 = 0u, c0 = 0u, c1 = 0u, b0 = 0u, b1 = 0u;
        const std::uint64_t multiplier0_bits = Lane == PairedLane::Lane1
            ? context->ps1_bits[multiplier_register] : context->fpr_bits[multiplier_register];
        const std::uint64_t multiplier1_bits = Lane == PairedLane::Lane0
            ? context->fpr_bits[multiplier_register] : context->ps1_bits[multiplier_register];
        using float_native_detail::try_narrow_normal_or_zero_widened_f32;
        if (paired_mode(context) && round_nearest(context) &&
            try_narrow_normal_or_zero_widened_f32(context->fpr_bits[multiplicand_register], a0) &&
            try_narrow_normal_or_zero_widened_f32(context->ps1_bits[multiplicand_register], a1) &&
            try_narrow_normal_or_zero_widened_f32(multiplier0_bits, c0) &&
            try_narrow_normal_or_zero_widened_f32(multiplier1_bits, c1) &&
            try_narrow_normal_or_zero_widened_f32(context->fpr_bits[addend_register], b0) &&
            try_narrow_normal_or_zero_widened_f32(context->ps1_bits[addend_register], b1)) GALAXY_LIKELY {
            const float result0 = ternary_op_f32<Operation>(
                std::bit_cast<float>(a0), std::bit_cast<float>(c0), std::bit_cast<float>(b0));
            const float result1 = ternary_op_f32<Operation>(
                std::bit_cast<float>(a1), std::bit_cast<float>(c1), std::bit_cast<float>(b1));
            const std::uint32_t bits0 = std::bit_cast<std::uint32_t>(result0);
            const std::uint32_t bits1 = std::bit_cast<std::uint32_t>(result1);
            if (f32_ordinary(bits0) && f32_ordinary(bits1)) GALAXY_LIKELY {
                commit_paired(context, target, bits0, bits1, record);
                return;
            }
        }
        paired_ternary_exact(
            context, target, Operation, multiplicand_register, multiplier_register,
            addend_register, Lane, record, services, guest_pc);
    }
}

GALAXY_NOINLINE inline void paired_sum_exact(
    PpcContext* context, std::uint32_t target, std::uint32_t left_register,
    std::uint32_t right_register, std::uint32_t copy_register, bool sum_in_lane1,
    bool record, const NativeServicesV1* services, std::uint32_t guest_pc) {
    require_paired_single_mode(context, false, services, guest_pc);
    const std::uint32_t left_ps0 =
        require_single_precision_bits(context->fpr_bits[left_register], services, guest_pc);
    const std::uint32_t right_ps1 =
        require_single_precision_bits(context->ps1_bits[right_register], services, guest_pc);
    const PpcFloatResult sum = ppc_f32_binary(
        PpcFloatBinaryOperation::Add, left_ps0, right_ps1, context->fpscr);
    if (sum_in_lane1) {
        const PpcFloatResult copy = ppc_f32_passthrough(
            require_single_precision_bits(context->fpr_bits[copy_register], services, guest_pc));
        ppc_commit_paired_result(context, target, copy, sum, true, record, services, guest_pc);
    } else {
        const PpcFloatResult copy = ppc_f32_passthrough(
            require_single_precision_bits(context->ps1_bits[copy_register], services, guest_pc));
        ppc_commit_paired_result(context, target, sum, copy, false, record, services, guest_pc);
    }
}

// ps_sum0: ps0 = frA.ps0 + frB.ps1, ps1 = frC.ps1. ps_sum1: ps1 = the sum,
// ps0 = frC.ps0.
template <bool SumInLane1>
GALAXY_ALWAYS_INLINE void paired_sum(
    PpcContext* context, std::uint32_t target, std::uint32_t left_register,
    std::uint32_t right_register, std::uint32_t copy_register, bool record,
    const NativeServicesV1* services, std::uint32_t guest_pc) {
    if constexpr (kFastFloat) {
        fast_float_detail::paired_sum_fast<SumInLane1>(context, context->fpr_bits, context->ps1_bits, target, left_register, right_register, copy_register, record);
    } else {
        std::uint32_t l0 = 0u, r1 = 0u, copy = 0u;
        const std::uint64_t copy_bits = SumInLane1
            ? context->fpr_bits[copy_register] : context->ps1_bits[copy_register];
        using float_native_detail::try_narrow_normal_or_zero_widened_f32;
        if (paired_mode(context) && round_nearest(context) &&
            try_narrow_normal_or_zero_widened_f32(context->fpr_bits[left_register], l0) &&
            try_narrow_normal_or_zero_widened_f32(context->ps1_bits[right_register], r1) &&
            try_narrow_normal_or_zero_widened_f32(copy_bits, copy)) GALAXY_LIKELY {
            const float sum = std::bit_cast<float>(l0) + std::bit_cast<float>(r1);
            const std::uint32_t sum_bits = std::bit_cast<std::uint32_t>(sum);
            if (f32_ordinary(sum_bits)) GALAXY_LIKELY {
                if constexpr (SumInLane1) {
                    commit_paired(context, target, copy, sum_bits, record);
                } else {
                    commit_paired(context, target, sum_bits, copy, record);
                }
                return;
            }
        }
        paired_sum_exact(
            context, target, left_register, right_register, copy_register, SumInLane1,
            record, services, guest_pc);
    }
}

// ---------------------------------------------------------------------------
// Register-resident floating point
// ---------------------------------------------------------------------------
//
// Bodies produced with register residency keep the guest FPR and PS1 files in
// two function-local arrays (fpr[32], ps1[32]) and hand them to these `_r`
// overloads instead of reading `context->fpr_bits`/`ps1_bits`. The fast paths
// touch only the arrays; FPSCR, HID2, GQR and CR stay in `context`. The exact
// fallbacks are the unchanged index-based helpers: before one runs, every
// register it may read (and the target, so an untouched PS1 half survives) is
// copied into `context`, and afterwards the target is copied back. The arrays
// never escape to an out-of-line call, so the host compiler can keep them in
// registers.

namespace resident_detail {

GALAXY_ALWAYS_INLINE void sync_in(
    PpcContext* context, const std::uint64_t* fpr, const std::uint64_t* ps1,
    std::uint32_t index) {
    context->fpr_bits[index] = fpr[index];
    context->ps1_bits[index] = ps1[index];
}

GALAXY_ALWAYS_INLINE void sync_out(
    const PpcContext* context, std::uint64_t* fpr, std::uint64_t* ps1,
    std::uint32_t index) {
    fpr[index] = context->fpr_bits[index];
    ps1[index] = context->ps1_bits[index];
}

GALAXY_ALWAYS_INLINE void commit_f64_r(
    PpcContext* context, std::uint64_t* fpr, std::uint32_t target,
    std::uint64_t bits, bool record) {
    fpr[target] = bits;
    if (record) {
        update_cr1_from_fpscr(context);
    }
}

GALAXY_ALWAYS_INLINE void commit_single_r(
    PpcContext* context, std::uint64_t* fpr, std::uint64_t* ps1,
    std::uint32_t target, std::uint32_t f32_bits, bool duplicate, bool record) {
    const std::uint64_t widened = widen_f32_bits(f32_bits);
    fpr[target] = widened;
    if (duplicate && paired_singles_enabled(context)) {
        ps1[target] = widened;
    }
    if (record) {
        update_cr1_from_fpscr(context);
    }
}

GALAXY_ALWAYS_INLINE void commit_paired_r(
    PpcContext* context, std::uint64_t* fpr, std::uint64_t* ps1,
    std::uint32_t target, std::uint32_t lane0, std::uint32_t lane1, bool record) {
    fpr[target] = widen_f32_bits(lane0);
    ps1[target] = widen_f32_bits(lane1);
    if (record) {
        update_cr1_from_fpscr(context);
    }
}

}  // namespace resident_detail

GALAXY_ALWAYS_INLINE void load_fpr_single_r(
    const std::byte* base, PpcContext* context, std::uint64_t* fpr,
    std::uint64_t* ps1, std::uint32_t index, GuestMemoryV1* memory,
    std::uint32_t address, const NativeServicesV1* services, std::uint32_t guest_pc) {
    const std::uint64_t widened =
        widen_f32_bits(load_u32(base, memory, address, services, guest_pc));
    fpr[index] = widened;
    if (paired_singles_enabled(context)) {
        ps1[index] = widened;
    }
}

GALAXY_ALWAYS_INLINE void store_fpr_single_r(
    std::byte* base, const PpcContext* context, const std::uint64_t* fpr,
    const std::uint64_t* ps1, std::uint32_t index, GuestMemoryV1* memory,
    std::uint32_t address, const NativeServicesV1* services, std::uint32_t guest_pc) {
    static_cast<void>(ps1);
    store<std::uint32_t>(
        base, memory, address,
        native_stored_single_bits(fpr[index], context->fpscr),
        services, guest_pc);
}

GALAXY_ALWAYS_INLINE void psq_load_r(
    const std::byte* base, PpcContext* context, std::uint64_t* fpr,
    std::uint64_t* ps1, std::uint32_t target, GuestMemoryV1* memory,
    std::uint32_t address, std::uint32_t gqr_index, bool one_element, bool d_form,
    const NativeServicesV1* services, std::uint32_t guest_pc) {
    const std::uint32_t gqr = context->gqr[gqr_index];
    const std::uint32_t type = psq_load_type(gqr);
    constexpr std::uint32_t kHid2Lsqe = 0x80000000u;
    constexpr std::uint32_t kHid2Pse = 0x20000000u;
    const std::uint32_t required = kHid2Pse | (d_form ? kHid2Lsqe : 0u);
    if (base != nullptr && (context->hid2 & required) == required) GALAXY_LIKELY {
        if (type == 0u) GALAXY_LIKELY {
            if (flat_span(address, one_element ? 4u : 8u)) GALAXY_LIKELY {
                fpr[target] = widen_f32_bits(
                    guest_flat_load_bytes<std::uint32_t>(base, address));
                ps1[target] = one_element
                    ? 0x3FF0000000000000ull
                    : widen_f32_bits(
                          guest_flat_load_bytes<std::uint32_t>(base, address + 4u));
                return;
            }
        } else if (type >= 4u && type <= 7u) {
            const std::uint32_t element = (type & 1u) != 0u ? 2u : 1u;
            if (flat_span(address, one_element ? element : element * 2u)) GALAXY_LIKELY {
                const std::int32_t scale = psq_load_scale(gqr);
                const auto load_lane = [&](std::uint32_t lane_address) -> std::uint64_t {
                    switch (type) {
                    case 4u:
                        return psq_load_integer_bits(
                            static_cast<std::int32_t>(
                                static_cast<std::uint8_t>(base[lane_address])),
                            scale);
                    case 5u:
                        return psq_load_integer_bits(
                            static_cast<std::int32_t>(
                                guest_flat_load_bytes<std::uint16_t>(base, lane_address)),
                            scale);
                    case 6u:
                        return psq_load_integer_bits(
                            static_cast<std::int32_t>(
                                static_cast<std::int8_t>(base[lane_address])),
                            scale);
                    default:
                        return psq_load_integer_bits(
                            static_cast<std::int32_t>(static_cast<std::int16_t>(
                                guest_flat_load_bytes<std::uint16_t>(base, lane_address))),
                            scale);
                    }
                };
                fpr[target] = load_lane(address);
                ps1[target] = one_element
                    ? 0x3FF0000000000000ull
                    : load_lane(address + element);
                return;
            }
        }
    }
    resident_detail::sync_in(context, fpr, ps1, target);
    galaxy::psq_load(
        context, target, memory, address, gqr_index, one_element, d_form,
        services, guest_pc);
    resident_detail::sync_out(context, fpr, ps1, target);
}

GALAXY_ALWAYS_INLINE void psq_store_r(
    std::byte* base, PpcContext* context, const std::uint64_t* fpr,
    const std::uint64_t* ps1, std::uint32_t source, GuestMemoryV1* memory,
    std::uint32_t address, std::uint32_t gqr_index, bool one_element, bool d_form,
    const NativeServicesV1* services, std::uint32_t guest_pc) {
    const std::uint32_t gqr = context->gqr[gqr_index];
    const std::uint32_t type = psq_store_type(gqr);
    constexpr std::uint32_t kHid2Lsqe = 0x80000000u;
    constexpr std::uint32_t kHid2Pse = 0x20000000u;
    const std::uint32_t required = kHid2Pse | (d_form ? kHid2Lsqe : 0u);
    if (base != nullptr && type == 0u &&
        (context->hid2 & required) == required) GALAXY_LIKELY {
        if (one_element) {
            if (flat_span(address, 4u)) GALAXY_LIKELY {
                const std::uint32_t first = byte_swap_u32(
                    narrow_paired_single_ftz(fpr[source]));
                std::memcpy(base + address, &first, sizeof(first));
                return;
            }
        } else if (flat_span(address, 8u)) GALAXY_LIKELY {
            const std::uint32_t first = byte_swap_u32(
                narrow_paired_single_ftz(fpr[source]));
            const std::uint32_t second = byte_swap_u32(
                narrow_paired_single_ftz(ps1[source]));
            std::memcpy(base + address, &first, sizeof(first));
            std::memcpy(base + address + 4u, &second, sizeof(second));
            return;
        }
    }
    resident_detail::sync_in(context, fpr, ps1, source);
    galaxy::psq_store(
        context, source, memory, address, gqr_index, one_element, d_form,
        services, guest_pc);
}

template <PpcFloatBinaryOperation Operation, bool Single>
GALAXY_ALWAYS_INLINE void scalar_binary_r(
    PpcContext* context, std::uint64_t* fpr, std::uint64_t* ps1,
    std::uint32_t target, std::uint32_t left_register, std::uint32_t right_register,
    bool record, const NativeServicesV1* services, std::uint32_t guest_pc) {
    if constexpr (kFastFloat) {
        fast_float_detail::scalar_binary_fast<Operation, Single>(context, fpr, ps1, target, left_register, right_register, record);
    } else {
        const std::uint64_t left = fpr[left_register];
        std::uint64_t right = fpr[right_register];
        if (round_nearest(context) && f64_ordinary(left) && f64_ordinary(right)) GALAXY_LIKELY {
            if constexpr (Single && Operation == PpcFloatBinaryOperation::Multiply) {
                right = round_multiplier_25(right);
            }
            const double result = binary_op<Operation>(
                std::bit_cast<double>(left), std::bit_cast<double>(right));
            if constexpr (Single) {
                std::uint32_t narrowed = 0u;
                if (narrow_result(result, narrowed)) GALAXY_LIKELY {
                    resident_detail::commit_single_r(
                        context, fpr, ps1, target, narrowed, true, record);
                    return;
                }
            } else {
                const std::uint64_t bits = std::bit_cast<std::uint64_t>(result);
                if (f64_ordinary(bits)) GALAXY_LIKELY {
                    resident_detail::commit_f64_r(context, fpr, target, bits, record);
                    return;
                }
            }
        }
        resident_detail::sync_in(context, fpr, ps1, left_register);
        resident_detail::sync_in(context, fpr, ps1, right_register);
        resident_detail::sync_in(context, fpr, ps1, target);
        scalar_binary_exact<Operation, Single>(
            context, target, left_register, right_register, record, services, guest_pc);
        resident_detail::sync_out(context, fpr, ps1, target);
    }
}

template <PpcFloatTernaryOperation Operation, bool Single>
GALAXY_ALWAYS_INLINE void scalar_ternary_r(
    PpcContext* context, std::uint64_t* fpr, std::uint64_t* ps1,
    std::uint32_t target, std::uint32_t multiplicand_register,
    std::uint32_t multiplier_register, std::uint32_t addend_register, bool record,
    const NativeServicesV1* services, std::uint32_t guest_pc) {
    if constexpr (kFastFloat) {
        fast_float_detail::scalar_ternary_fast<Operation, Single>(context, fpr, ps1, target, multiplicand_register, multiplier_register, addend_register, record);
    } else {
        const std::uint64_t a = fpr[multiplicand_register];
        std::uint64_t c = fpr[multiplier_register];
        const std::uint64_t b = fpr[addend_register];
        if (round_nearest(context) && f64_ordinary(a) && f64_ordinary(c) &&
            f64_ordinary(b)) GALAXY_LIKELY {
            if constexpr (Single) {
                c = round_multiplier_25(c);
            }
            const double result = ternary_op<Operation>(
                std::bit_cast<double>(a), std::bit_cast<double>(c),
                std::bit_cast<double>(b));
            if constexpr (Single) {
                std::uint32_t narrowed = 0u;
                if (narrow_result(result, narrowed)) GALAXY_LIKELY {
                    resident_detail::commit_single_r(
                        context, fpr, ps1, target, narrowed, true, record);
                    return;
                }
            } else {
                const std::uint64_t bits = std::bit_cast<std::uint64_t>(result);
                if (f64_ordinary(bits)) GALAXY_LIKELY {
                    resident_detail::commit_f64_r(context, fpr, target, bits, record);
                    return;
                }
            }
        }
        resident_detail::sync_in(context, fpr, ps1, multiplicand_register);
        resident_detail::sync_in(context, fpr, ps1, multiplier_register);
        resident_detail::sync_in(context, fpr, ps1, addend_register);
        resident_detail::sync_in(context, fpr, ps1, target);
        scalar_ternary_exact<Operation, Single>(
            context, target, multiplicand_register, multiplier_register, addend_register,
            record, services, guest_pc);
        resident_detail::sync_out(context, fpr, ps1, target);
    }
}

GALAXY_ALWAYS_INLINE void scalar_frsp_r(
    PpcContext* context, std::uint64_t* fpr, std::uint64_t* ps1,
    std::uint32_t target, std::uint32_t source_register, bool record,
    const NativeServicesV1* services, std::uint32_t guest_pc) {
    if constexpr (kFastFloat) {
        fast_float_detail::scalar_frsp_fast(context, fpr, ps1, target, source_register, record);
    } else {
        const std::uint64_t source = fpr[source_register];
        if (round_nearest(context) && f64_ordinary(source)) GALAXY_LIKELY {
            std::uint32_t narrowed = 0u;
            if (narrow_result(std::bit_cast<double>(source), narrowed)) GALAXY_LIKELY {
                resident_detail::commit_single_r(
                    context, fpr, ps1, target, narrowed, false, record);
                return;
            }
        }
        resident_detail::sync_in(context, fpr, ps1, source_register);
        resident_detail::sync_in(context, fpr, ps1, target);
        scalar_frsp_exact(context, target, source_register, record, services, guest_pc);
        resident_detail::sync_out(context, fpr, ps1, target);
    }
}

template <PpcFloatBinaryOperation Operation, PairedLane Lane>
GALAXY_ALWAYS_INLINE void paired_binary_r(
    PpcContext* context, std::uint64_t* fpr, std::uint64_t* ps1,
    std::uint32_t target, std::uint32_t left_register, std::uint32_t right_register,
    bool record, const NativeServicesV1* services, std::uint32_t guest_pc) {
    if constexpr (kFastFloat) {
        fast_float_detail::paired_binary_fast<Operation, Lane>(context, fpr, ps1, target, left_register, right_register, record);
    } else {
        std::uint32_t l0 = 0u, l1 = 0u, r0 = 0u, r1 = 0u;
        const std::uint64_t right0_bits = Lane == PairedLane::Lane1
            ? ps1[right_register] : fpr[right_register];
        const std::uint64_t right1_bits = Lane == PairedLane::Lane0
            ? fpr[right_register] : ps1[right_register];
        using float_native_detail::try_narrow_normal_or_zero_widened_f32;
        if (paired_mode(context) && round_nearest(context) &&
            try_narrow_normal_or_zero_widened_f32(fpr[left_register], l0) &&
            try_narrow_normal_or_zero_widened_f32(ps1[left_register], l1) &&
            try_narrow_normal_or_zero_widened_f32(right0_bits, r0) &&
            try_narrow_normal_or_zero_widened_f32(right1_bits, r1)) GALAXY_LIKELY {
            const float result0 = binary_op_f32<Operation>(
                std::bit_cast<float>(l0), std::bit_cast<float>(r0));
            const float result1 = binary_op_f32<Operation>(
                std::bit_cast<float>(l1), std::bit_cast<float>(r1));
            const std::uint32_t bits0 = std::bit_cast<std::uint32_t>(result0);
            const std::uint32_t bits1 = std::bit_cast<std::uint32_t>(result1);
            if (f32_ordinary(bits0) && f32_ordinary(bits1)) GALAXY_LIKELY {
                resident_detail::commit_paired_r(
                    context, fpr, ps1, target, bits0, bits1, record);
                return;
            }
        }
        resident_detail::sync_in(context, fpr, ps1, left_register);
        resident_detail::sync_in(context, fpr, ps1, right_register);
        resident_detail::sync_in(context, fpr, ps1, target);
        paired_binary_exact(
            context, target, Operation, left_register, right_register, Lane, record,
            services, guest_pc);
        resident_detail::sync_out(context, fpr, ps1, target);
    }
}

template <PpcFloatTernaryOperation Operation, PairedLane Lane>
GALAXY_ALWAYS_INLINE void paired_ternary_r(
    PpcContext* context, std::uint64_t* fpr, std::uint64_t* ps1,
    std::uint32_t target, std::uint32_t multiplicand_register,
    std::uint32_t multiplier_register, std::uint32_t addend_register, bool record,
    const NativeServicesV1* services, std::uint32_t guest_pc) {
    if constexpr (kFastFloat) {
        fast_float_detail::paired_ternary_fast<Operation, Lane>(context, fpr, ps1, target, multiplicand_register, multiplier_register, addend_register, record);
    } else {
        std::uint32_t a0 = 0u, a1 = 0u, c0 = 0u, c1 = 0u, b0 = 0u, b1 = 0u;
        const std::uint64_t multiplier0_bits = Lane == PairedLane::Lane1
            ? ps1[multiplier_register] : fpr[multiplier_register];
        const std::uint64_t multiplier1_bits = Lane == PairedLane::Lane0
            ? fpr[multiplier_register] : ps1[multiplier_register];
        using float_native_detail::try_narrow_normal_or_zero_widened_f32;
        if (paired_mode(context) && round_nearest(context) &&
            try_narrow_normal_or_zero_widened_f32(fpr[multiplicand_register], a0) &&
            try_narrow_normal_or_zero_widened_f32(ps1[multiplicand_register], a1) &&
            try_narrow_normal_or_zero_widened_f32(multiplier0_bits, c0) &&
            try_narrow_normal_or_zero_widened_f32(multiplier1_bits, c1) &&
            try_narrow_normal_or_zero_widened_f32(fpr[addend_register], b0) &&
            try_narrow_normal_or_zero_widened_f32(ps1[addend_register], b1)) GALAXY_LIKELY {
            const float result0 = ternary_op_f32<Operation>(
                std::bit_cast<float>(a0), std::bit_cast<float>(c0), std::bit_cast<float>(b0));
            const float result1 = ternary_op_f32<Operation>(
                std::bit_cast<float>(a1), std::bit_cast<float>(c1), std::bit_cast<float>(b1));
            const std::uint32_t bits0 = std::bit_cast<std::uint32_t>(result0);
            const std::uint32_t bits1 = std::bit_cast<std::uint32_t>(result1);
            if (f32_ordinary(bits0) && f32_ordinary(bits1)) GALAXY_LIKELY {
                resident_detail::commit_paired_r(
                    context, fpr, ps1, target, bits0, bits1, record);
                return;
            }
        }
        resident_detail::sync_in(context, fpr, ps1, multiplicand_register);
        resident_detail::sync_in(context, fpr, ps1, multiplier_register);
        resident_detail::sync_in(context, fpr, ps1, addend_register);
        resident_detail::sync_in(context, fpr, ps1, target);
        paired_ternary_exact(
            context, target, Operation, multiplicand_register, multiplier_register,
            addend_register, Lane, record, services, guest_pc);
        resident_detail::sync_out(context, fpr, ps1, target);
    }
}

template <bool SumInLane1>
GALAXY_ALWAYS_INLINE void paired_sum_r(
    PpcContext* context, std::uint64_t* fpr, std::uint64_t* ps1,
    std::uint32_t target, std::uint32_t left_register, std::uint32_t right_register,
    std::uint32_t copy_register, bool record, const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    if constexpr (kFastFloat) {
        fast_float_detail::paired_sum_fast<SumInLane1>(context, fpr, ps1, target, left_register, right_register, copy_register, record);
    } else {
        std::uint32_t l0 = 0u, r1 = 0u, copy = 0u;
        const std::uint64_t copy_bits = SumInLane1
            ? fpr[copy_register] : ps1[copy_register];
        using float_native_detail::try_narrow_normal_or_zero_widened_f32;
        if (paired_mode(context) && round_nearest(context) &&
            try_narrow_normal_or_zero_widened_f32(fpr[left_register], l0) &&
            try_narrow_normal_or_zero_widened_f32(ps1[right_register], r1) &&
            try_narrow_normal_or_zero_widened_f32(copy_bits, copy)) GALAXY_LIKELY {
            const float sum = std::bit_cast<float>(l0) + std::bit_cast<float>(r1);
            const std::uint32_t sum_bits = std::bit_cast<std::uint32_t>(sum);
            if (f32_ordinary(sum_bits)) GALAXY_LIKELY {
                if constexpr (SumInLane1) {
                    resident_detail::commit_paired_r(
                        context, fpr, ps1, target, copy, sum_bits, record);
                } else {
                    resident_detail::commit_paired_r(
                        context, fpr, ps1, target, sum_bits, copy, record);
                }
                return;
            }
        }
        resident_detail::sync_in(context, fpr, ps1, left_register);
        resident_detail::sync_in(context, fpr, ps1, right_register);
        resident_detail::sync_in(context, fpr, ps1, copy_register);
        resident_detail::sync_in(context, fpr, ps1, target);
        paired_sum_exact(
            context, target, left_register, right_register, copy_register, SumInLane1,
            record, services, guest_pc);
        resident_detail::sync_out(context, fpr, ps1, target);
    }
}

// ---------------------------------------------------------------------------
// fctiwz
// ---------------------------------------------------------------------------

// Exact mode keeps the FPSCR-aware software conversion and commit; fast mode
// saturates like the hardware and leaves the FPSCR alone.
GALAXY_ALWAYS_INLINE void fctiwz(
    PpcContext* context, std::uint32_t target, std::uint32_t source_register,
    bool record, const NativeServicesV1* services, std::uint32_t guest_pc) {
    if constexpr (kFastFloat) {
        context->fpr_bits[target] =
            fast_float_detail::fctiwz_bits(context->fpr_bits[source_register]);
        if (record) {
            update_cr1_from_fpscr(context);
        }
    } else {
        const PpcFloatResult result =
            ppc_f64_to_i32_round_zero(context->fpr_bits[source_register], context->fpscr);
        ppc_commit_scalar_result(context, target, result, false, record, services, guest_pc);
    }
}

GALAXY_ALWAYS_INLINE void fctiwz_r(
    PpcContext* context, std::uint64_t* fpr, std::uint64_t* ps1, std::uint32_t target,
    std::uint32_t source_register, bool record, const NativeServicesV1* services,
    std::uint32_t guest_pc) {
    if constexpr (kFastFloat) {
        fpr[target] = fast_float_detail::fctiwz_bits(fpr[source_register]);
        if (record) {
            update_cr1_from_fpscr(context);
        }
    } else {
        resident_detail::sync_in(context, fpr, ps1, source_register);
        resident_detail::sync_in(context, fpr, ps1, target);
        fctiwz(context, target, source_register, record, services, guest_pc);
        resident_detail::sync_out(context, fpr, ps1, target);
    }
}

// ---------------------------------------------------------------------------
// Direct indirect-call dispatch
// ---------------------------------------------------------------------------

// A `bctrl`/`bclrl` whose target has a published direct entry calls the native
// body in place. The runtime leaves the entry null for every function that
// owns a host-side transaction, and withdraws the whole table while an
// external interrupt dispatch is live or a device event is pending, so those
// calls keep the audited service boundary.
GALAXY_ALWAYS_INLINE void call_guest_indirect(
    const NativeServicesV1* services,
    std::uint32_t guest_address,
    std::uint32_t* cached_address,
    NativeGameFunction* cached_function,
    PpcContext* context,
    GuestMemoryV1* memory,
    std::uint32_t caller_pc) {
    if (services != nullptr && services->direct_dispatch != nullptr) GALAXY_LIKELY {
        const std::uint32_t index = guest_address - services->direct_dispatch_base;
        if (index < services->direct_dispatch_count && (index & 3u) == 0u) GALAXY_LIKELY {
            const NativeGameFunction function = services->direct_dispatch[index >> 2u];
            if (function != nullptr &&
                atomic_load_u32(services->pending_event_mask) == 0u &&
                atomic_load_u32(services->direct_dispatch_suspended) == 0u) GALAXY_LIKELY {
                context->pc = guest_address;
                function(context, memory, services);
                return;
            }
        }
    }
    call_guest_cached(
        services, guest_address, cached_address, cached_function, context, memory,
        caller_pc);
}

}  // namespace fast
}  // namespace galaxy
