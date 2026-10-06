#include "galaxy/dsp_alu.h"

#include <bit>
#include <limits>

namespace galaxy {
namespace {

bool top_two_bits_equal(std::uint64_t packed) {
    // TB describes the top two bits of the accumulator's middle word.
    const std::uint64_t top = (packed >> 30u) & 0x3u;
    return top == 0x0u || top == 0x3u;
}

bool over_signed_32(std::int64_t value) {
    return value < static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min()) ||
           value > static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max());
}

bool add_overflows_40(std::int64_t left, std::int64_t right, std::int64_t result) {
    return ((left ^ result) & (right ^ result)) < 0;
}

bool subtract_overflows_40(
    std::int64_t left,
    std::int64_t right,
    std::int64_t result) {
    return ((left ^ right) & (left ^ result)) < 0;
}

std::int64_t sign_extend_product_high(std::uint16_t value) {
    std::int64_t high = static_cast<std::int64_t>(value & 0x00ffu);
    if ((high & 0x80ll) != 0) {
        high |= ~0xffll;
    }
    return high;
}

}  // namespace

DspCondition dsp_condition_from_bits(std::uint16_t bits) {
    switch (bits & 0x000fu) {
    case 0x0:
        return DspCondition::GreaterOrEqual;
    case 0x1:
        return DspCondition::Less;
    case 0x2:
        return DspCondition::Greater;
    case 0x3:
        return DspCondition::LessOrEqual;
    case 0x4:
        return DspCondition::NotZero;
    case 0x5:
        return DspCondition::Zero;
    case 0x6:
        return DspCondition::NotCarry;
    case 0x7:
        return DspCondition::Carry;
    case 0x8:
        return DspCondition::NotOverSigned32;
    case 0x9:
        return DspCondition::OverSigned32;
    case 0xa:
        return DspCondition::ExtendedA;
    case 0xb:
        return DspCondition::ExtendedB;
    case 0xc:
        return DspCondition::LogicNotZero;
    case 0xd:
        return DspCondition::LogicZero;
    case 0xe:
        return DspCondition::Overflow;
    default:
        return DspCondition::Always;
    }
}

bool dsp_condition_holds(DspCondition condition, std::uint16_t status) {
    const bool carry = (status & kDspSrCarry) != 0;
    const bool overflow = (status & kDspSrOverflow) != 0;
    const bool zero = (status & kDspSrArithmeticZero) != 0;
    const bool sign = (status & kDspSrSign) != 0;
    const bool over_signed_32 = (status & kDspSrOverSigned32) != 0;
    const bool top_two_bits = (status & kDspSrTopTwoBitsEqual) != 0;
    const bool logic_zero = (status & kDspSrLogicZero) != 0;
    const bool less = overflow != sign;
    const bool condition_b = (!(over_signed_32 || top_two_bits)) || zero;

    switch (condition) {
    case DspCondition::GreaterOrEqual:
        return !less;
    case DspCondition::Less:
        return less;
    case DspCondition::Greater:
        return !less && !zero;
    case DspCondition::LessOrEqual:
        return less || zero;
    case DspCondition::NotZero:
        return !zero;
    case DspCondition::Zero:
        return zero;
    case DspCondition::NotCarry:
        return !carry;
    case DspCondition::Carry:
        return carry;
    case DspCondition::NotOverSigned32:
        return !over_signed_32;
    case DspCondition::OverSigned32:
        return over_signed_32;
    case DspCondition::ExtendedA:
        return !condition_b;
    case DspCondition::ExtendedB:
        return condition_b;
    case DspCondition::LogicNotZero:
        return !logic_zero;
    case DspCondition::LogicZero:
        return logic_zero;
    case DspCondition::Overflow:
        return overflow;
    case DspCondition::Always:
        return true;
    }

    return true;
}

std::uint64_t dsp_pack_accumulator(std::int64_t value) {
    return static_cast<std::uint64_t>(value) & kDspAccumulatorMask;
}

std::int64_t dsp_sign_extend_accumulator(std::uint64_t value) {
    const std::uint64_t packed = value & kDspAccumulatorMask;
    if ((packed & kDspAccumulatorSign) == 0) {
        return static_cast<std::int64_t>(packed);
    }
    return static_cast<std::int64_t>(packed | ~kDspAccumulatorMask);
}

std::int64_t dsp_round_accumulator(std::int64_t value) {
    std::uint64_t packed = dsp_pack_accumulator(value);
    packed += (packed & 0x0001'0000ull) != 0 ? 0x8000ull : 0x7fffull;
    packed &= ~0xffffull;
    return dsp_sign_extend_accumulator(packed);
}

std::uint16_t dsp_accumulator_status(
    std::int64_t value,
    bool carry,
    bool overflow,
    std::uint16_t previous_status) {
    const std::int64_t canonical = dsp_sign_extend_accumulator(dsp_pack_accumulator(value));
    const std::uint64_t packed = dsp_pack_accumulator(canonical);
    std::uint16_t status =
        previous_status & static_cast<std::uint16_t>(~kDspSrArithmeticMask);
    if (carry) {
        status |= kDspSrCarry;
    }
    if (overflow) {
        status |= kDspSrOverflow | kDspSrStickyOverflow;
    }
    if (canonical == 0) {
        status |= kDspSrArithmeticZero;
    }
    if (canonical < 0) {
        status |= kDspSrSign;
    }
    if (over_signed_32(canonical)) {
        status |= kDspSrOverSigned32;
    }
    if (top_two_bits_equal(packed)) {
        status |= kDspSrTopTwoBitsEqual;
    }
    return status;
}

std::uint16_t dsp_status_16(
    std::int16_t value,
    bool carry,
    bool overflow,
    bool over_signed_32,
    std::uint16_t previous_status) {
    std::uint16_t status =
        previous_status & static_cast<std::uint16_t>(~kDspSrArithmeticMask);
    if (carry) {
        status |= kDspSrCarry;
    }
    if (overflow) {
        status |= kDspSrOverflow | kDspSrStickyOverflow;
    }
    if (value == 0) {
        status |= kDspSrArithmeticZero;
    }
    if (value < 0) {
        status |= kDspSrSign;
    }
    if (over_signed_32) {
        status |= kDspSrOverSigned32;
    }
    const std::uint16_t top = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(value) >> 14u);
    if (top == 0 || top == 3) {
        status |= kDspSrTopTwoBitsEqual;
    }
    return status;
}

DspAccumulatorResult dsp_accumulator_add(
    std::int64_t left,
    std::int64_t right,
    std::uint16_t previous_status) {
    const std::int64_t canonical_left =
        dsp_sign_extend_accumulator(dsp_pack_accumulator(left));
    const std::int64_t canonical_right =
        dsp_sign_extend_accumulator(dsp_pack_accumulator(right));
    const std::uint64_t unsigned_left = dsp_pack_accumulator(canonical_left);
    const std::uint64_t unsigned_right = dsp_pack_accumulator(canonical_right);
    const std::uint64_t unsigned_sum = unsigned_left + unsigned_right;
    const std::int64_t result = dsp_sign_extend_accumulator(unsigned_sum);
    const bool carry = unsigned_sum > kDspAccumulatorMask;
    const bool overflow = add_overflows_40(canonical_left, canonical_right, result);
    return DspAccumulatorResult{
        result,
        dsp_accumulator_status(result, carry, overflow, previous_status)};
}

DspAccumulatorResult dsp_accumulator_subtract(
    std::int64_t left,
    std::int64_t right,
    std::uint16_t previous_status) {
    const std::int64_t canonical_left =
        dsp_sign_extend_accumulator(dsp_pack_accumulator(left));
    const std::int64_t canonical_right =
        dsp_sign_extend_accumulator(dsp_pack_accumulator(right));
    const std::uint64_t unsigned_left = dsp_pack_accumulator(canonical_left);
    const std::uint64_t unsigned_right = dsp_pack_accumulator(canonical_right);
    const std::int64_t result =
        dsp_sign_extend_accumulator(unsigned_left - unsigned_right);
    const bool carry = unsigned_left >= unsigned_right;
    const bool overflow =
        subtract_overflows_40(canonical_left, canonical_right, result);
    return DspAccumulatorResult{
        result,
        dsp_accumulator_status(result, carry, overflow, previous_status)};
}

DspAccumulatorResult dsp_accumulator_negate(
    std::int64_t value,
    std::uint16_t previous_status) {
    return dsp_accumulator_subtract(0, value, previous_status);
}

DspAccumulatorResult dsp_accumulator_set_value(
    std::int64_t value,
    std::uint16_t previous_status) {
    const std::int64_t canonical = dsp_sign_extend_accumulator(dsp_pack_accumulator(value));
    return DspAccumulatorResult{
        canonical,
        dsp_accumulator_status(canonical, false, false, previous_status)};
}

DspAccumulatorResult dsp_accumulator_clear_low(
    std::int64_t value,
    std::uint16_t previous_status) {
    return dsp_accumulator_set_value(dsp_round_accumulator(value), previous_status);
}

DspAccumulatorResult dsp_accumulator_absolute(
    std::int64_t value,
    std::uint16_t previous_status) {
    std::int64_t canonical = dsp_sign_extend_accumulator(dsp_pack_accumulator(value));
    if (canonical < 0) {
        canonical = dsp_sign_extend_accumulator(dsp_pack_accumulator(0 - canonical));
    }
    return dsp_accumulator_set_value(canonical, previous_status);
}

DspAccumulatorResult dsp_accumulator_shift(
    std::int64_t value,
    DspAccumulatorShiftKind kind,
    std::uint16_t amount,
    std::uint16_t previous_status) {
    const std::uint16_t shift = amount & 0x003fu;
    const std::uint64_t packed = dsp_pack_accumulator(value);
    switch (kind) {
    case DspAccumulatorShiftKind::LogicalLeft:
    case DspAccumulatorShiftKind::ArithmeticLeft:
        return dsp_accumulator_set_value(
            static_cast<std::int64_t>(packed << shift), previous_status);
    case DspAccumulatorShiftKind::LogicalRight:
        return dsp_accumulator_set_value(
            static_cast<std::int64_t>(packed >> shift), previous_status);
    case DspAccumulatorShiftKind::ArithmeticRight: {
        const std::int64_t canonical = dsp_sign_extend_accumulator(packed);
        return dsp_accumulator_set_value(canonical >> shift, previous_status);
    }
    }

    return dsp_accumulator_set_value(value, previous_status);
}

DspAccumulatorResult dsp_accumulator_shift_by_signed_count(
    std::int64_t value,
    bool arithmetic_right,
    std::uint16_t count_mid,
    std::uint16_t previous_status) {
    const std::uint16_t magnitude = count_mid & 0x003fu;
    if (magnitude == 0) {
        return dsp_accumulator_set_value(value, previous_status);
    }
    if ((count_mid & 0x0040u) != 0) {
        const std::uint16_t left_amount = static_cast<std::uint16_t>(0x40u - magnitude);
        return dsp_accumulator_shift(
            value, DspAccumulatorShiftKind::LogicalLeft, left_amount, previous_status);
    }
    return dsp_accumulator_shift(
        value,
        arithmetic_right ? DspAccumulatorShiftKind::ArithmeticRight :
                           DspAccumulatorShiftKind::LogicalRight,
        magnitude,
        previous_status);
}

DspAccumulatorResult dsp_accumulator_shift_by_register_count(
    std::int64_t value,
    bool arithmetic_right,
    std::uint16_t count,
    std::uint16_t previous_status) {
    const std::uint16_t magnitude = count & 0x003fu;
    if (magnitude == 0) {
        return dsp_accumulator_set_value(value, previous_status);
    }
    if ((count & 0x0040u) != 0) {
        const std::uint16_t right_amount = static_cast<std::uint16_t>(0x40u - magnitude);
        return dsp_accumulator_shift(
            value,
            arithmetic_right ? DspAccumulatorShiftKind::ArithmeticRight :
                               DspAccumulatorShiftKind::LogicalRight,
            right_amount,
            previous_status);
    }
    return dsp_accumulator_shift(
        value, DspAccumulatorShiftKind::LogicalLeft, magnitude, previous_status);
}

std::int64_t dsp_accumulator_from_ax_pair(
    std::uint16_t low,
    std::uint16_t high) {
    const std::uint32_t packed =
        static_cast<std::uint32_t>(low) | (static_cast<std::uint32_t>(high) << 16u);
    return static_cast<std::int64_t>(static_cast<std::int32_t>(packed));
}

std::int64_t dsp_accumulator_from_signed_mid(std::uint16_t value) {
    // Multiplication preserves the architectural two's-complement value without
    // relying on the undefined C++ operation of left-shifting a negative integer.
    return static_cast<std::int64_t>(static_cast<std::int16_t>(value)) * 0x1'0000ll;
}

std::uint16_t dsp_read_accumulator_low(std::int64_t accumulator) {
    return static_cast<std::uint16_t>(dsp_pack_accumulator(accumulator));
}

std::int64_t dsp_write_accumulator_low(std::int64_t accumulator, std::uint16_t low) {
    std::uint64_t packed = dsp_pack_accumulator(accumulator);
    packed &= ~0xffffull;
    packed |= static_cast<std::uint64_t>(low);
    return dsp_sign_extend_accumulator(packed);
}

std::uint16_t dsp_read_accumulator_mid(
    std::int64_t accumulator,
    std::uint16_t status) {
    const std::int64_t canonical =
        dsp_sign_extend_accumulator(dsp_pack_accumulator(accumulator));
    if ((status & kDspSr40BitMode) != 0 && over_signed_32(canonical)) {
        return canonical > 0 ? 0x7fffu : 0x8000u;
    }
    return static_cast<std::uint16_t>(dsp_pack_accumulator(canonical) >> 16u);
}

std::uint16_t dsp_read_accumulator_mid_raw(std::int64_t accumulator) {
    return static_cast<std::uint16_t>(dsp_pack_accumulator(accumulator) >> 16u);
}

std::uint16_t dsp_read_accumulator_high(std::int64_t accumulator) {
    const std::uint16_t high =
        static_cast<std::uint16_t>((dsp_pack_accumulator(accumulator) >> 32u) & 0xffu);
    return (high & 0x80u) != 0 ? static_cast<std::uint16_t>(high | 0xff00u) :
                                 high;
}

std::int64_t dsp_write_accumulator_high(
    std::int64_t accumulator,
    std::uint16_t high) {
    std::uint64_t packed = dsp_pack_accumulator(accumulator);
    packed &= ~(0xffull << 32u);
    packed |= static_cast<std::uint64_t>(high & 0x00ffu) << 32u;
    return dsp_sign_extend_accumulator(packed);
}

std::int64_t dsp_write_accumulator_mid(
    std::int64_t accumulator,
    std::uint16_t mid,
    std::uint16_t status) {
    if ((status & kDspSr40BitMode) != 0) {
        const std::int64_t signed_mid =
            static_cast<std::int64_t>(static_cast<std::int16_t>(mid));
        return dsp_sign_extend_accumulator(
            static_cast<std::uint64_t>(signed_mid * 0x10000ll));
    }

    std::uint64_t packed = dsp_pack_accumulator(accumulator);
    packed &= ~(0xffffull << 16u);
    packed |= static_cast<std::uint64_t>(mid) << 16u;
    return dsp_sign_extend_accumulator(packed);
}

std::uint16_t dsp_logic_zero_status(bool logic_zero, std::uint16_t previous_status) {
    std::uint16_t status =
        previous_status & static_cast<std::uint16_t>(~kDspSrLogicZero);
    if (logic_zero) {
        status |= kDspSrLogicZero;
    }
    return status;
}

DspLogicResult dsp_accumulator_set_mid_logic(
    std::int64_t accumulator,
    std::uint16_t mid,
    std::uint16_t previous_status) {
    std::uint64_t packed = dsp_pack_accumulator(accumulator);
    packed &= ~(0xffffull << 16u);
    packed |= static_cast<std::uint64_t>(mid) << 16u;
    const std::int64_t value = dsp_sign_extend_accumulator(packed);
    return DspLogicResult{
        value,
        dsp_status_16(std::bit_cast<std::int16_t>(mid), false, false,
                      over_signed_32(value), previous_status)};
}

DspLogicResult dsp_accumulator_mid_xor(
    std::int64_t accumulator,
    std::uint16_t operand,
    std::uint16_t previous_status) {
    const std::uint16_t mid =
        static_cast<std::uint16_t>(dsp_pack_accumulator(accumulator) >> 16u);
    return dsp_accumulator_set_mid_logic(
        accumulator, static_cast<std::uint16_t>(mid ^ operand), previous_status);
}

DspLogicResult dsp_accumulator_mid_and(
    std::int64_t accumulator,
    std::uint16_t operand,
    std::uint16_t previous_status) {
    const std::uint16_t mid =
        static_cast<std::uint16_t>(dsp_pack_accumulator(accumulator) >> 16u);
    return dsp_accumulator_set_mid_logic(
        accumulator, static_cast<std::uint16_t>(mid & operand), previous_status);
}

DspLogicResult dsp_accumulator_mid_or(
    std::int64_t accumulator,
    std::uint16_t operand,
    std::uint16_t previous_status) {
    const std::uint16_t mid =
        static_cast<std::uint16_t>(dsp_pack_accumulator(accumulator) >> 16u);
    return dsp_accumulator_set_mid_logic(
        accumulator, static_cast<std::uint16_t>(mid | operand), previous_status);
}

DspLogicResult dsp_accumulator_mid_not(
    std::int64_t accumulator,
    std::uint16_t previous_status) {
    const std::uint16_t mid =
        static_cast<std::uint16_t>(dsp_pack_accumulator(accumulator) >> 16u);
    return dsp_accumulator_set_mid_logic(
        accumulator, static_cast<std::uint16_t>(mid ^ 0xffffu), previous_status);
}

DspProductParts dsp_cleared_product() {
    return DspProductParts{
        0x0000u,
        0xfff0u,
        0x00ffu,
        0x0010u,
    };
}

DspProductParts dsp_store_product_value(std::int64_t value) {
    const std::uint64_t packed =
        static_cast<std::uint64_t>(value) & kDspProductStoreMask;
    return DspProductParts{
        static_cast<std::uint16_t>(packed),
        static_cast<std::uint16_t>(packed >> 16u),
        static_cast<std::uint16_t>(packed >> 32u),
        0,
    };
}

std::int64_t dsp_product_value(const DspProductParts& parts) {
    // The product high byte is signed.  Use multiplication instead of a signed
    // left shift so negative audio products have fully defined C++ semantics.
    const std::int64_t high =
        sign_extend_product_high(parts.high) * 0x1'0000'0000ll;
    const std::int64_t mid =
        static_cast<std::int64_t>(
            static_cast<std::uint32_t>(parts.mid) + parts.mid2) *
        0x1'0000ll;
    return high + mid + parts.low;
}

std::int64_t dsp_round_product(const DspProductParts& parts) {
    const std::uint64_t packed =
        static_cast<std::uint64_t>(dsp_product_value(parts)) & kDspAccumulatorMask;
    const std::uint64_t rounded =
        ((packed & 0x0001'0000ull) != 0 ? packed + 0x8000ull
                                        : packed + 0x7fffull) &
        ~0xffffull;
    return dsp_sign_extend_accumulator(rounded);
}

DspAccumulatorResult dsp_accumulator_add_rounded_product_and_ax_clear_low(
    const DspProductParts& product,
    std::uint16_t ax_low,
    std::uint16_t ax_high,
    std::uint16_t previous_status) {
    const std::int64_t old_product = dsp_product_value(product);
    const std::int64_t rounded_product = dsp_round_product(product);
    const std::int64_t ax =
        dsp_accumulator_from_ax_pair(ax_low, ax_high) & ~0xffffll;
    const std::int64_t result =
        dsp_sign_extend_accumulator(dsp_pack_accumulator(rounded_product + ax));
    const bool carry =
        static_cast<std::uint64_t>(old_product) > static_cast<std::uint64_t>(result);
    return DspAccumulatorResult{
        result,
        dsp_accumulator_status(result, carry, false, previous_status)};
}

std::int64_t dsp_multiply_terms(
    std::uint16_t left,
    std::uint16_t right,
    DspMultiplyOperandMode mode,
    std::uint16_t status) {
    const bool unsigned_mode = (status & kDspSrMultiplyUnsigned) != 0;
    std::int64_t product = 0;
    switch (mode) {
    case DspMultiplyOperandMode::UnsignedWhenSrUnsigned:
        if (unsigned_mode) {
            product = static_cast<std::int64_t>(
                static_cast<std::uint32_t>(left) *
                static_cast<std::uint32_t>(right));
        } else {
            product =
                static_cast<std::int64_t>(static_cast<std::int16_t>(left)) *
                static_cast<std::int64_t>(static_cast<std::int16_t>(right));
        }
        break;
    case DspMultiplyOperandMode::MixedUnsignedLeftWhenSrUnsigned:
        if (unsigned_mode) {
            product =
                static_cast<std::int64_t>(static_cast<std::uint32_t>(left)) *
                static_cast<std::int64_t>(static_cast<std::int16_t>(right));
        } else {
            product =
                static_cast<std::int64_t>(static_cast<std::int16_t>(left)) *
                static_cast<std::int64_t>(static_cast<std::int16_t>(right));
        }
        break;
    case DspMultiplyOperandMode::Signed:
        product =
            static_cast<std::int64_t>(static_cast<std::int16_t>(left)) *
            static_cast<std::int64_t>(static_cast<std::int16_t>(right));
        break;
    }

    if ((status & kDspSrMultiplyModifier) == 0) {
        // A negative signed left shift is undefined in C++.  The 16x16 product
        // is far inside int64_t range, so multiplication expresses M2 exactly.
        product *= 2ll;
    }
    return product;
}

DspProductParts dsp_product_multiply(
    std::uint16_t left,
    std::uint16_t right,
    DspMultiplyOperandMode mode,
    std::uint16_t status) {
    return dsp_store_product_value(dsp_multiply_terms(left, right, mode, status));
}

DspProductParts dsp_product_multiply_add(
    const DspProductParts& product,
    std::uint16_t left,
    std::uint16_t right,
    DspMultiplyOperandMode mode,
    std::uint16_t status) {
    return dsp_store_product_value(
        dsp_product_value(product) + dsp_multiply_terms(left, right, mode, status));
}

DspProductParts dsp_product_multiply_subtract(
    const DspProductParts& product,
    std::uint16_t left,
    std::uint16_t right,
    DspMultiplyOperandMode mode,
    std::uint16_t status) {
    return dsp_store_product_value(
        dsp_product_value(product) - dsp_multiply_terms(left, right, mode, status));
}

std::uint16_t dsp_increment_address(std::uint16_t address, std::uint16_t wrap) {
    const std::uint32_t ar = address;
    const std::uint32_t wr = wrap;
    std::uint32_t next = ar + 1u;
    if ((next ^ ar) > ((wr | 1u) << 1u)) {
        next -= wr + 1u;
    }
    return static_cast<std::uint16_t>(next);
}

std::uint16_t dsp_decrement_address(std::uint16_t address, std::uint16_t wrap) {
    const std::uint32_t ar = address;
    const std::uint32_t wr = wrap;
    std::uint32_t next = ar + wr;
    if (((next ^ ar) & ((wr | 1u) << 1u)) > wr) {
        next -= wr + 1u;
    }
    return static_cast<std::uint16_t>(next);
}

std::uint16_t dsp_increase_address(
    std::uint16_t address,
    std::int16_t index,
    std::uint16_t wrap) {
    const std::uint32_t ar = address;
    const std::uint32_t wr = wrap;
    const std::int32_t ix = index;
    const std::uint32_t unsigned_ix = static_cast<std::uint32_t>(ix);
    const std::uint32_t mask = (wr | 1u) << 1u;
    std::uint32_t next = ar + unsigned_ix;
    const std::uint32_t delta = (next ^ ar ^ unsigned_ix) & mask;

    if (ix >= 0) {
        if (delta > wr) {
            next -= wr + 1u;
        }
    } else if ((((next + wr + 1u) ^ next) & delta) <= wr) {
        next += wr + 1u;
    }
    return static_cast<std::uint16_t>(next);
}

std::uint16_t dsp_decrease_address(
    std::uint16_t address,
    std::int16_t index,
    std::uint16_t wrap) {
    const std::uint32_t ar = address;
    const std::uint32_t wr = wrap;
    const std::int32_t ix = index;
    const std::uint32_t unsigned_ix = static_cast<std::uint32_t>(ix);
    const std::uint32_t mask = (wr | 1u) << 1u;
    std::uint32_t next = ar - unsigned_ix;
    const std::uint32_t delta = (next ^ ar ^ ~unsigned_ix) & mask;

    if (unsigned_ix > 0xffff8000u) {
        if (delta > wr) {
            next -= wr + 1u;
        }
    } else if ((((next + wr + 1u) ^ next) & delta) <= wr) {
        next += wr + 1u;
    }
    return static_cast<std::uint16_t>(next);
}

}  // namespace galaxy
