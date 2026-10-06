#pragma once

#include <cstdint>

namespace galaxy {

inline constexpr std::uint64_t kDspAccumulatorBits = 40;
inline constexpr std::uint64_t kDspAccumulatorMask = (1ull << kDspAccumulatorBits) - 1ull;
inline constexpr std::uint64_t kDspAccumulatorSign = 1ull << (kDspAccumulatorBits - 1ull);
inline constexpr std::int64_t kDspAccumulatorMin = -(1ll << 39);
inline constexpr std::int64_t kDspAccumulatorMax = (1ll << 39) - 1ll;
inline constexpr std::uint64_t kDspProductStoreMask = 0x0000'ffff'ffff'ffffull;

inline constexpr std::uint16_t kDspSrCarry = 0x0001u;
inline constexpr std::uint16_t kDspSrOverflow = 0x0002u;
inline constexpr std::uint16_t kDspSrArithmeticZero = 0x0004u;
inline constexpr std::uint16_t kDspSrSign = 0x0008u;
inline constexpr std::uint16_t kDspSrOverSigned32 = 0x0010u;
inline constexpr std::uint16_t kDspSrTopTwoBitsEqual = 0x0020u;
inline constexpr std::uint16_t kDspSrLogicZero = 0x0040u;
inline constexpr std::uint16_t kDspSrStickyOverflow = 0x0080u;
inline constexpr std::uint16_t kDspSrMultiplyModifier = 0x2000u;
inline constexpr std::uint16_t kDspSr40BitMode = 0x4000u;
inline constexpr std::uint16_t kDspSrMultiplyUnsigned = 0x8000u;

inline constexpr std::uint16_t kDspSrArithmeticMask =
    kDspSrCarry | kDspSrOverflow | kDspSrArithmeticZero | kDspSrSign |
    kDspSrOverSigned32 | kDspSrTopTwoBitsEqual;

struct DspAccumulatorResult {
    std::int64_t value{};
    std::uint16_t status{};
};

struct DspLogicResult {
    std::int64_t accumulator{};
    std::uint16_t status{};
};

struct DspProductParts {
    std::uint16_t low{};
    std::uint16_t mid{};
    std::uint16_t high{};
    std::uint16_t mid2{};
};

enum class DspAccumulatorShiftKind : std::uint8_t {
    LogicalLeft,
    LogicalRight,
    ArithmeticLeft,
    ArithmeticRight,
};

enum class DspMultiplyOperandMode : std::uint8_t {
    Signed,
    UnsignedWhenSrUnsigned,
    MixedUnsignedLeftWhenSrUnsigned,
};

enum class DspCondition : std::uint8_t {
    GreaterOrEqual,
    Less,
    Greater,
    LessOrEqual,
    NotZero,
    Zero,
    NotCarry,
    Carry,
    NotOverSigned32,
    OverSigned32,
    ExtendedA,
    ExtendedB,
    LogicNotZero,
    LogicZero,
    Overflow,
    Always,
};

DspCondition dsp_condition_from_bits(std::uint16_t bits);

bool dsp_condition_holds(DspCondition condition, std::uint16_t status);

std::uint64_t dsp_pack_accumulator(std::int64_t value);

std::int64_t dsp_sign_extend_accumulator(std::uint64_t value);

std::int64_t dsp_round_accumulator(std::int64_t value);

std::uint16_t dsp_accumulator_status(
    std::int64_t value,
    bool carry,
    bool overflow,
    std::uint16_t previous_status);

std::uint16_t dsp_status_16(
    std::int16_t value,
    bool carry,
    bool overflow,
    bool over_signed_32,
    std::uint16_t previous_status);

DspAccumulatorResult dsp_accumulator_add(
    std::int64_t left,
    std::int64_t right,
    std::uint16_t previous_status);

DspAccumulatorResult dsp_accumulator_subtract(
    std::int64_t left,
    std::int64_t right,
    std::uint16_t previous_status);

DspAccumulatorResult dsp_accumulator_negate(
    std::int64_t value,
    std::uint16_t previous_status);

DspAccumulatorResult dsp_accumulator_set_value(
    std::int64_t value,
    std::uint16_t previous_status);

DspAccumulatorResult dsp_accumulator_clear_low(
    std::int64_t value,
    std::uint16_t previous_status);

DspAccumulatorResult dsp_accumulator_absolute(
    std::int64_t value,
    std::uint16_t previous_status);

DspAccumulatorResult dsp_accumulator_shift(
    std::int64_t value,
    DspAccumulatorShiftKind kind,
    std::uint16_t amount,
    std::uint16_t previous_status);

DspAccumulatorResult dsp_accumulator_shift_by_signed_count(
    std::int64_t value,
    bool arithmetic_right,
    std::uint16_t count_mid,
    std::uint16_t previous_status);

DspAccumulatorResult dsp_accumulator_shift_by_register_count(
    std::int64_t value,
    bool arithmetic_right,
    std::uint16_t count,
    std::uint16_t previous_status);

std::int64_t dsp_accumulator_from_ax_pair(
    std::uint16_t low,
    std::uint16_t high);

std::int64_t dsp_accumulator_from_signed_mid(std::uint16_t value);

std::uint16_t dsp_read_accumulator_low(std::int64_t accumulator);

std::int64_t dsp_write_accumulator_low(std::int64_t accumulator, std::uint16_t low);

std::uint16_t dsp_read_accumulator_mid(
    std::int64_t accumulator,
    std::uint16_t status);

std::uint16_t dsp_read_accumulator_mid_raw(std::int64_t accumulator);

std::uint16_t dsp_read_accumulator_high(std::int64_t accumulator);

std::int64_t dsp_write_accumulator_high(
    std::int64_t accumulator,
    std::uint16_t high);

std::int64_t dsp_write_accumulator_mid(
    std::int64_t accumulator,
    std::uint16_t mid,
    std::uint16_t status);

std::uint16_t dsp_logic_zero_status(bool logic_zero, std::uint16_t previous_status);

DspLogicResult dsp_accumulator_set_mid_logic(
    std::int64_t accumulator,
    std::uint16_t mid,
    std::uint16_t previous_status);

DspLogicResult dsp_accumulator_mid_xor(
    std::int64_t accumulator,
    std::uint16_t operand,
    std::uint16_t previous_status);

DspLogicResult dsp_accumulator_mid_and(
    std::int64_t accumulator,
    std::uint16_t operand,
    std::uint16_t previous_status);

DspLogicResult dsp_accumulator_mid_or(
    std::int64_t accumulator,
    std::uint16_t operand,
    std::uint16_t previous_status);

DspLogicResult dsp_accumulator_mid_not(
    std::int64_t accumulator,
    std::uint16_t previous_status);

DspProductParts dsp_cleared_product();

DspProductParts dsp_store_product_value(std::int64_t value);

std::int64_t dsp_product_value(const DspProductParts& parts);

std::int64_t dsp_round_product(const DspProductParts& parts);

DspAccumulatorResult dsp_accumulator_add_rounded_product_and_ax_clear_low(
    const DspProductParts& product,
    std::uint16_t ax_low,
    std::uint16_t ax_high,
    std::uint16_t previous_status);

std::int64_t dsp_multiply_terms(
    std::uint16_t left,
    std::uint16_t right,
    DspMultiplyOperandMode mode,
    std::uint16_t status);

DspProductParts dsp_product_multiply(
    std::uint16_t left,
    std::uint16_t right,
    DspMultiplyOperandMode mode,
    std::uint16_t status);

DspProductParts dsp_product_multiply_add(
    const DspProductParts& product,
    std::uint16_t left,
    std::uint16_t right,
    DspMultiplyOperandMode mode,
    std::uint16_t status);

DspProductParts dsp_product_multiply_subtract(
    const DspProductParts& product,
    std::uint16_t left,
    std::uint16_t right,
    DspMultiplyOperandMode mode,
    std::uint16_t status);

std::uint16_t dsp_increment_address(std::uint16_t address, std::uint16_t wrap);

std::uint16_t dsp_decrement_address(std::uint16_t address, std::uint16_t wrap);

std::uint16_t dsp_increase_address(
    std::uint16_t address,
    std::int16_t index,
    std::uint16_t wrap);

std::uint16_t dsp_decrease_address(
    std::uint16_t address,
    std::int16_t index,
    std::uint16_t wrap);

}  // namespace galaxy
