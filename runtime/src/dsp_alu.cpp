#include "galaxy/dsp_alu.h"

// The DSP ALU semantics are defined once, header-inline, in
// galaxy::dsp_inline (dsp_context.h) so generated DSP code can fold them into
// the lowered ucode. These exported functions are the out-of-line ABI used by
// the runtime and the ALU tests; each one forwards to that single definition.
#include "galaxy/dsp_context.h"

namespace galaxy {

DspCondition dsp_condition_from_bits(std::uint16_t bits) {
    return dsp_inline::dsp_condition_from_bits(bits);
}

bool dsp_condition_holds(DspCondition condition, std::uint16_t status) {
    return dsp_inline::dsp_condition_holds(condition, status);
}

std::uint64_t dsp_pack_accumulator(std::int64_t value) {
    return dsp_inline::dsp_pack_accumulator(value);
}

std::int64_t dsp_sign_extend_accumulator(std::uint64_t value) {
    return dsp_inline::dsp_sign_extend_accumulator(value);
}

std::int64_t dsp_round_accumulator(std::int64_t value) {
    return dsp_inline::dsp_round_accumulator(value);
}

std::uint16_t dsp_accumulator_status(
    std::int64_t value,
    bool carry,
    bool overflow,
    std::uint16_t previous_status) {
    return dsp_inline::dsp_accumulator_status(value, carry, overflow, previous_status);
}

std::uint16_t dsp_status_16(
    std::int16_t value,
    bool carry,
    bool overflow,
    bool over_signed_32,
    std::uint16_t previous_status) {
    return dsp_inline::dsp_status_16(
        value, carry, overflow, over_signed_32, previous_status);
}

DspAccumulatorResult dsp_accumulator_add(
    std::int64_t left,
    std::int64_t right,
    std::uint16_t previous_status) {
    return dsp_inline::dsp_accumulator_add(left, right, previous_status);
}

DspAccumulatorResult dsp_accumulator_subtract(
    std::int64_t left,
    std::int64_t right,
    std::uint16_t previous_status) {
    return dsp_inline::dsp_accumulator_subtract(left, right, previous_status);
}

DspAccumulatorResult dsp_accumulator_negate(
    std::int64_t value,
    std::uint16_t previous_status) {
    return dsp_inline::dsp_accumulator_negate(value, previous_status);
}

DspAccumulatorResult dsp_accumulator_set_value(
    std::int64_t value,
    std::uint16_t previous_status) {
    return dsp_inline::dsp_accumulator_set_value(value, previous_status);
}

DspAccumulatorResult dsp_accumulator_clear_low(
    std::int64_t value,
    std::uint16_t previous_status) {
    return dsp_inline::dsp_accumulator_clear_low(value, previous_status);
}

DspAccumulatorResult dsp_accumulator_absolute(
    std::int64_t value,
    std::uint16_t previous_status) {
    return dsp_inline::dsp_accumulator_absolute(value, previous_status);
}

DspAccumulatorResult dsp_accumulator_shift(
    std::int64_t value,
    DspAccumulatorShiftKind kind,
    std::uint16_t amount,
    std::uint16_t previous_status) {
    return dsp_inline::dsp_accumulator_shift(value, kind, amount, previous_status);
}

DspAccumulatorResult dsp_accumulator_shift_by_signed_count(
    std::int64_t value,
    bool arithmetic_right,
    std::uint16_t count_mid,
    std::uint16_t previous_status) {
    return dsp_inline::dsp_accumulator_shift_by_signed_count(
        value, arithmetic_right, count_mid, previous_status);
}

DspAccumulatorResult dsp_accumulator_shift_by_register_count(
    std::int64_t value,
    bool arithmetic_right,
    std::uint16_t count,
    std::uint16_t previous_status) {
    return dsp_inline::dsp_accumulator_shift_by_register_count(
        value, arithmetic_right, count, previous_status);
}

std::int64_t dsp_accumulator_from_ax_pair(
    std::uint16_t low,
    std::uint16_t high) {
    return dsp_inline::dsp_accumulator_from_ax_pair(low, high);
}

std::int64_t dsp_accumulator_from_signed_mid(std::uint16_t value) {
    return dsp_inline::dsp_accumulator_from_signed_mid(value);
}

std::uint16_t dsp_read_accumulator_low(std::int64_t accumulator) {
    return dsp_inline::dsp_read_accumulator_low(accumulator);
}

std::int64_t dsp_write_accumulator_low(std::int64_t accumulator, std::uint16_t low) {
    return dsp_inline::dsp_write_accumulator_low(accumulator, low);
}

std::uint16_t dsp_read_accumulator_mid(
    std::int64_t accumulator,
    std::uint16_t status) {
    return dsp_inline::dsp_read_accumulator_mid(accumulator, status);
}

std::uint16_t dsp_read_accumulator_mid_raw(std::int64_t accumulator) {
    return dsp_inline::dsp_read_accumulator_mid_raw(accumulator);
}

std::uint16_t dsp_read_accumulator_high(std::int64_t accumulator) {
    return dsp_inline::dsp_read_accumulator_high(accumulator);
}

std::int64_t dsp_write_accumulator_high(
    std::int64_t accumulator,
    std::uint16_t high) {
    return dsp_inline::dsp_write_accumulator_high(accumulator, high);
}

std::int64_t dsp_write_accumulator_mid(
    std::int64_t accumulator,
    std::uint16_t mid,
    std::uint16_t status) {
    return dsp_inline::dsp_write_accumulator_mid(accumulator, mid, status);
}

std::uint16_t dsp_logic_zero_status(bool logic_zero, std::uint16_t previous_status) {
    return dsp_inline::dsp_logic_zero_status(logic_zero, previous_status);
}

DspLogicResult dsp_accumulator_set_mid_logic(
    std::int64_t accumulator,
    std::uint16_t mid,
    std::uint16_t previous_status) {
    return dsp_inline::dsp_accumulator_set_mid_logic(accumulator, mid, previous_status);
}

DspLogicResult dsp_accumulator_mid_xor(
    std::int64_t accumulator,
    std::uint16_t operand,
    std::uint16_t previous_status) {
    return dsp_inline::dsp_accumulator_mid_xor(accumulator, operand, previous_status);
}

DspLogicResult dsp_accumulator_mid_and(
    std::int64_t accumulator,
    std::uint16_t operand,
    std::uint16_t previous_status) {
    return dsp_inline::dsp_accumulator_mid_and(accumulator, operand, previous_status);
}

DspLogicResult dsp_accumulator_mid_or(
    std::int64_t accumulator,
    std::uint16_t operand,
    std::uint16_t previous_status) {
    return dsp_inline::dsp_accumulator_mid_or(accumulator, operand, previous_status);
}

DspLogicResult dsp_accumulator_mid_not(
    std::int64_t accumulator,
    std::uint16_t previous_status) {
    return dsp_inline::dsp_accumulator_mid_not(accumulator, previous_status);
}

DspProductParts dsp_cleared_product() {
    return dsp_inline::dsp_cleared_product();
}

DspProductParts dsp_store_product_value(std::int64_t value) {
    return dsp_inline::dsp_store_product_value(value);
}

std::int64_t dsp_product_value(const DspProductParts& parts) {
    return dsp_inline::dsp_product_value(parts);
}

std::int64_t dsp_round_product(const DspProductParts& parts) {
    return dsp_inline::dsp_round_product(parts);
}

DspAccumulatorResult dsp_accumulator_add_rounded_product_and_ax_clear_low(
    const DspProductParts& product,
    std::uint16_t ax_low,
    std::uint16_t ax_high,
    std::uint16_t previous_status) {
    return dsp_inline::dsp_accumulator_add_rounded_product_and_ax_clear_low(
        product, ax_low, ax_high, previous_status);
}

std::int64_t dsp_multiply_terms(
    std::uint16_t left,
    std::uint16_t right,
    DspMultiplyOperandMode mode,
    std::uint16_t status) {
    return dsp_inline::dsp_multiply_terms(left, right, mode, status);
}

DspProductParts dsp_product_multiply(
    std::uint16_t left,
    std::uint16_t right,
    DspMultiplyOperandMode mode,
    std::uint16_t status) {
    return dsp_inline::dsp_product_multiply(left, right, mode, status);
}

DspProductParts dsp_product_multiply_add(
    const DspProductParts& product,
    std::uint16_t left,
    std::uint16_t right,
    DspMultiplyOperandMode mode,
    std::uint16_t status) {
    return dsp_inline::dsp_product_multiply_add(product, left, right, mode, status);
}

DspProductParts dsp_product_multiply_subtract(
    const DspProductParts& product,
    std::uint16_t left,
    std::uint16_t right,
    DspMultiplyOperandMode mode,
    std::uint16_t status) {
    return dsp_inline::dsp_product_multiply_subtract(product, left, right, mode, status);
}

std::uint16_t dsp_increment_address(std::uint16_t address, std::uint16_t wrap) {
    return dsp_inline::dsp_increment_address(address, wrap);
}

std::uint16_t dsp_decrement_address(std::uint16_t address, std::uint16_t wrap) {
    return dsp_inline::dsp_decrement_address(address, wrap);
}

std::uint16_t dsp_increase_address(
    std::uint16_t address,
    std::int16_t index,
    std::uint16_t wrap) {
    return dsp_inline::dsp_increase_address(address, index, wrap);
}

std::uint16_t dsp_decrease_address(
    std::uint16_t address,
    std::int16_t index,
    std::uint16_t wrap) {
    return dsp_inline::dsp_decrease_address(address, index, wrap);
}

}  // namespace galaxy
