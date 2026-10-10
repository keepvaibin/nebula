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
// This header is included at the end of galaxy/native_api.h after every guest
// helper it relies on has been declared.

#include <cmath>

namespace galaxy {
namespace fast {

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

// Scalar fadd/fsub/fmul/fdiv and their single-precision forms.
template <PpcFloatBinaryOperation Operation, bool Single>
GALAXY_ALWAYS_INLINE void scalar_binary(
    PpcContext* context, std::uint32_t target, std::uint32_t left_register,
    std::uint32_t right_register, bool record, const NativeServicesV1* services,
    std::uint32_t guest_pc) {
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

// Scalar fmadd family. `multiplicand` is frA, `multiplier` frC, `addend` frB.
template <PpcFloatTernaryOperation Operation, bool Single>
GALAXY_ALWAYS_INLINE void scalar_ternary(
    PpcContext* context, std::uint32_t target, std::uint32_t multiplicand_register,
    std::uint32_t multiplier_register, std::uint32_t addend_register, bool record,
    const NativeServicesV1* services, std::uint32_t guest_pc) {
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

GALAXY_ALWAYS_INLINE void scalar_frsp(
    PpcContext* context, std::uint32_t target, std::uint32_t source_register,
    bool record, const NativeServicesV1* services, std::uint32_t guest_pc) {
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
