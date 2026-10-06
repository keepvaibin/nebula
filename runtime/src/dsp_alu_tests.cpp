#include "galaxy/dsp_alu.h"
#include "galaxy/dsp_context.h"
#include "galaxy/dsp_guest_bridge.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <utility>

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

template <typename Fn>
bool expect_hard_trap(Fn&& fn, const char* message) {
    try {
        fn();
    } catch (const galaxy::DspHardTrap&) {
        return true;
    }
    return expect(false, message);
}

bool has(std::uint16_t status, std::uint16_t mask) {
    return (status & mask) == mask;
}

bool expect_condition(
    galaxy::DspCondition condition,
    std::uint16_t status,
    bool expected,
    const char* message) {
    return expect(galaxy::dsp_condition_holds(condition, status) == expected, message);
}

// RMGE01's final two-channel conversion routine at DSP PC 0x0DB1 runs in
// SET40/SET15/M0 mode: unsigned master gain times signed mixer sample, followed
// by ASRNRX with +4 and a saturating AC0.M store.  Keep the scalar expression
// independent from the ALU helpers so this is a useful differential oracle.
std::uint16_t reference_rmge01_output_sample(
    std::uint16_t master_gain,
    std::uint16_t mixer_sample) {
    const std::int64_t product =
        static_cast<std::int64_t>(master_gain) *
        static_cast<std::int64_t>(static_cast<std::int16_t>(mixer_sample));
    const std::int64_t shifted = product * 16ll;
    if (shifted > 0x7fff'ffffll) {
        return 0x7fffu;
    }
    if (shifted < -0x8000'0000ll) {
        return 0x8000u;
    }
    return static_cast<std::uint16_t>(
        static_cast<std::uint64_t>(shifted) >> 16u);
}

std::uint16_t native_rmge01_output_sample(
    std::uint16_t master_gain,
    std::uint16_t mixer_sample) {
    constexpr std::uint16_t output_status =
        galaxy::kDspSrMultiplyModifier |
        galaxy::kDspSr40BitMode |
        galaxy::kDspSrMultiplyUnsigned;
    const auto product = galaxy::dsp_product_multiply(
        master_gain,
        mixer_sample,
        galaxy::DspMultiplyOperandMode::MixedUnsignedLeftWhenSrUnsigned,
        output_status);
    const auto moved = galaxy::dsp_accumulator_set_value(
        galaxy::dsp_product_value(product), output_status);
    const auto shifted = galaxy::dsp_accumulator_shift_by_register_count(
        moved.value, true, 0x0004u, moved.status);
    return galaxy::dsp_read_accumulator_mid(shifted.value, shifted.status);
}

// The 0x0DA5/0x0DA7 mixer body adds a signed AC1.M*AX1.H product to a
// signed sample held in the accumulator middle word while M0 is active.
std::uint16_t reference_rmge01_mix_sample(
    std::uint16_t base_sample,
    std::uint16_t ramp_gain,
    std::uint16_t source_sample) {
    const std::int64_t base =
        static_cast<std::int64_t>(static_cast<std::int16_t>(base_sample)) *
        0x1'0000ll;
    const std::int64_t product =
        static_cast<std::int64_t>(static_cast<std::int16_t>(ramp_gain)) *
        static_cast<std::int64_t>(static_cast<std::int16_t>(source_sample));
    return static_cast<std::uint16_t>(
        static_cast<std::uint64_t>(base + product) >> 16u);
}

std::uint16_t native_rmge01_mix_sample(
    std::uint16_t base_sample,
    std::uint16_t ramp_gain,
    std::uint16_t source_sample) {
    constexpr std::uint16_t mixer_status = galaxy::kDspSrMultiplyModifier;
    const std::int64_t base = galaxy::dsp_accumulator_from_signed_mid(base_sample);
    const auto product = galaxy::dsp_product_multiply(
        ramp_gain,
        source_sample,
        galaxy::DspMultiplyOperandMode::Signed,
        mixer_status);
    const auto mixed = galaxy::dsp_accumulator_set_value(
        base + galaxy::dsp_product_value(product), mixer_status);
    return galaxy::dsp_read_accumulator_mid(mixed.value, mixed.status);
}

struct InterruptProbe {
    std::uint32_t count{};
};

void record_interrupt(void* user) {
    auto* const probe = static_cast<InterruptProbe*>(user);
    ++probe->count;
}

struct BridgeProbe {
    std::uint32_t interrupt_count{};
    std::uint32_t exception_count{};
    galaxy::DspAcceleratorException last_exception{};
};

void record_bridge_interrupt(void* user) {
    auto* const probe = static_cast<BridgeProbe*>(user);
    ++probe->interrupt_count;
}

void record_bridge_exception(void* user, galaxy::DspAcceleratorException exception) {
    auto* const probe = static_cast<BridgeProbe*>(user);
    ++probe->exception_count;
    probe->last_exception = exception;
}

struct AcceleratorMemoryProbe {
    std::array<std::uint8_t, 256> memory{};
    std::uint32_t byte_reads{};
    std::uint32_t byte_writes{};
    std::uint32_t exception_count{};
    galaxy::DspAcceleratorException last_exception{};
    galaxy::DspContext* context{};
    std::uint32_t current_address_at_exception{};
    bool reads_stopped_at_exception{};
};

bool validate_external_span(
    void* user,
    std::uint32_t address,
    std::uint32_t size) {
    const auto* const probe = static_cast<const AcceleratorMemoryProbe*>(user);
    return probe != nullptr &&
           galaxy::dsp_u32_span_is_valid(address, size) &&
           address <= probe->memory.size() &&
           size <= probe->memory.size() - address;
}

bool read_external_byte(
    void* user,
    std::uint32_t address,
    std::uint8_t* value) {
    auto* const probe = static_cast<AcceleratorMemoryProbe*>(user);
    if (value == nullptr || !validate_external_span(user, address, 1u)) {
        return false;
    }
    ++probe->byte_reads;
    *value = probe->memory[address];
    return true;
}

bool write_external_byte(void* user, std::uint32_t address, std::uint8_t value) {
    auto* const probe = static_cast<AcceleratorMemoryProbe*>(user);
    if (!validate_external_span(user, address, 1u)) {
        return false;
    }
    ++probe->byte_writes;
    probe->memory[address] = value;
    return true;
}

void record_accelerator_exception(void* user, galaxy::DspAcceleratorException exception) {
    auto* const probe = static_cast<AcceleratorMemoryProbe*>(user);
    ++probe->exception_count;
    probe->last_exception = exception;
    if (probe->context != nullptr) {
        probe->current_address_at_exception =
            galaxy::dsp_accelerator_current_address(*probe->context);
        probe->reads_stopped_at_exception =
            probe->context->accelerator_reads_stopped;
    }
}

}  // namespace

int main() {
    bool passed = true;

    for (std::uint16_t bits = 0; bits < 16; ++bits) {
        passed &= expect(
            static_cast<std::uint8_t>(galaxy::dsp_condition_from_bits(bits)) == bits,
            "condition low nibble maps in architectural order");
    }

    passed &= expect_condition(
        galaxy::DspCondition::Always, 0, true, "always condition is true");
    passed &= expect_condition(
        galaxy::DspCondition::GreaterOrEqual, 0, true, "GE true when not less");
    passed &= expect_condition(
        galaxy::DspCondition::Less, 0, false, "L false when overflow equals sign");
    passed &= expect_condition(
        galaxy::DspCondition::Greater, 0, true, "G true when not less and not zero");
    passed &= expect_condition(
        galaxy::DspCondition::LessOrEqual, 0, false, "LE false when neither less nor zero");
    passed &= expect_condition(
        galaxy::DspCondition::Less,
        galaxy::kDspSrSign,
        true,
        "L true when sign differs from overflow");
    passed &= expect_condition(
        galaxy::DspCondition::GreaterOrEqual,
        galaxy::kDspSrSign,
        false,
        "GE false when less");
    passed &= expect_condition(
        galaxy::DspCondition::Greater,
        galaxy::kDspSrArithmeticZero,
        false,
        "G false when zero");
    passed &= expect_condition(
        galaxy::DspCondition::LessOrEqual,
        galaxy::kDspSrArithmeticZero,
        true,
        "LE true when zero");
    passed &= expect_condition(
        galaxy::DspCondition::NotZero,
        galaxy::kDspSrArithmeticZero,
        false,
        "NZ false when arithmetic zero");
    passed &= expect_condition(
        galaxy::DspCondition::Zero,
        galaxy::kDspSrArithmeticZero,
        true,
        "Z true when arithmetic zero");
    passed &= expect_condition(
        galaxy::DspCondition::NotCarry,
        galaxy::kDspSrCarry,
        false,
        "NC false when carry set");
    passed &= expect_condition(
        galaxy::DspCondition::Carry,
        galaxy::kDspSrCarry,
        true,
        "C true when carry set");
    passed &= expect_condition(
        galaxy::DspCondition::NotOverSigned32,
        galaxy::kDspSrOverSigned32,
        false,
        "condition 8 false when OVER_S32 set");
    passed &= expect_condition(
        galaxy::DspCondition::OverSigned32,
        galaxy::kDspSrOverSigned32,
        true,
        "condition 9 true when OVER_S32 set");
    passed &= expect_condition(
        galaxy::DspCondition::ExtendedA,
        galaxy::kDspSrOverSigned32,
        true,
        "condition A true when condition B is false");
    passed &= expect_condition(
        galaxy::DspCondition::ExtendedB,
        galaxy::kDspSrOverSigned32,
        false,
        "condition B false when over-s32 without zero");
    passed &= expect_condition(
        galaxy::DspCondition::ExtendedA,
        galaxy::kDspSrOverSigned32 | galaxy::kDspSrArithmeticZero,
        false,
        "condition A false when zero makes condition B true");
    passed &= expect_condition(
        galaxy::DspCondition::ExtendedB,
        galaxy::kDspSrTopTwoBitsEqual,
        false,
        "condition B false when top-two-bits set without zero");
    passed &= expect_condition(
        galaxy::DspCondition::LogicNotZero,
        galaxy::kDspSrLogicZero,
        false,
        "LNZ false when logic zero");
    passed &= expect_condition(
        galaxy::DspCondition::LogicZero,
        galaxy::kDspSrLogicZero,
        true,
        "LZ true when logic zero");
    passed &= expect_condition(
        galaxy::DspCondition::Overflow,
        galaxy::kDspSrOverflow,
        true,
        "O true when overflow set");

    passed &= expect(
        galaxy::dsp_pack_accumulator(-1) == 0xffffffffffull,
        "packing masks to forty bits");
    passed &= expect(
        galaxy::dsp_sign_extend_accumulator(0x0000000000ull) == 0,
        "zero sign-extends");
    passed &= expect(
        galaxy::dsp_sign_extend_accumulator(0x7fffffffffull) ==
            galaxy::kDspAccumulatorMax,
        "maximum positive accumulator sign-extends");
    passed &= expect(
        galaxy::dsp_sign_extend_accumulator(0x8000000000ull) ==
            galaxy::kDspAccumulatorMin,
        "minimum negative accumulator sign-extends");
    passed &= expect(
        galaxy::dsp_sign_extend_accumulator(0xffffffffffull) == -1,
        "negative all-ones accumulator sign-extends");

    passed &= expect(galaxy::dsp_round_accumulator(0x00007fff) == 0,
                     "rounding below bit 16 clears low word");
    passed &= expect(galaxy::dsp_round_accumulator(0x00008000) == 0,
                     "rounding half low word without bit 16 rounds down");
    passed &= expect(galaxy::dsp_round_accumulator(0x0000ffff) == 0x00010000,
                     "rounding just below bit 16 rounds up to mid word");
    passed &= expect(galaxy::dsp_round_accumulator(0x00010000) == 0x00010000,
                     "rounding an exact mid word is stable");
    passed &= expect(galaxy::dsp_round_accumulator(0x00018000) == 0x00020000,
                     "rounding with bit 16 set uses the high bias");
    passed &= expect(galaxy::dsp_round_accumulator(-1) == 0,
                     "rounding negative low bits crosses to zero");
    passed &= expect(galaxy::dsp_round_accumulator(-0x00010000) == -0x00010000,
                     "rounding exact negative mid word is stable");

    const auto positive_overflow =
        galaxy::dsp_accumulator_add(galaxy::kDspAccumulatorMax, 1, 0);
    passed &= expect(
        positive_overflow.value == galaxy::kDspAccumulatorMin,
        "adding past forty-bit positive max wraps to min");
    passed &= expect(
        has(positive_overflow.status, galaxy::kDspSrOverflow),
        "positive add overflow sets overflow");
    passed &= expect(
        has(positive_overflow.status, galaxy::kDspSrStickyOverflow),
        "positive add overflow sets sticky overflow");
    passed &= expect(
        has(positive_overflow.status, galaxy::kDspSrSign),
        "positive add overflow result is negative");
    passed &= expect(
        !has(positive_overflow.status, galaxy::kDspSrCarry),
        "positive add overflow does not imply carry");
    passed &= expect(
        has(positive_overflow.status, galaxy::kDspSrTopTwoBitsEqual),
        "wrapped min has equal top two middle-word bits");

    // Literal SR expectations distinguish middle bits 30/31 from bits 38/39.
    for (const auto& [value, expected] :
         std::array<std::pair<std::int64_t, std::uint16_t>, 8>{{
             {0, std::uint16_t{0x0024u}}, {1, std::uint16_t{0x0020u}},
             {0x40000000ll, std::uint16_t{0x0000u}},
             {0x80000000ll, std::uint16_t{0x0010u}},
             {0xC0000000ll, std::uint16_t{0x0030u}},
             {-1, std::uint16_t{0x0028u}},
             {-0x8000000000ll, std::uint16_t{0x0038u}},
             {0x7FFFFFFFFFll, std::uint16_t{0x0030u}}}}) {
        passed &= expect(
            galaxy::dsp_accumulator_status(value, false, false, 0x40C0u) ==
                (expected | 0x40C0u),
            "full accumulator flags use middle TB and preserve unrelated SR");
    }
    passed &= expect(
        !galaxy::dsp_condition_holds(galaxy::DspCondition::ExtendedA, 0x0000u) &&
            galaxy::dsp_condition_holds(galaxy::DspCondition::ExtendedB, 0x0000u),
        "2^30 flags select extended B rather than extended A");

    const auto add_to_zero = galaxy::dsp_accumulator_add(-1, 1, 0);
    passed &= expect(add_to_zero.value == 0, "minus one plus one is zero");
    passed &= expect(
        has(add_to_zero.status, galaxy::kDspSrCarry),
        "minus one plus one carries out of forty bits");
    passed &= expect(
        has(add_to_zero.status, galaxy::kDspSrArithmeticZero),
        "zero result sets arithmetic zero");
    passed &= expect(
        has(add_to_zero.status, galaxy::kDspSrTopTwoBitsEqual),
        "zero result has equal top two bits");

    const auto subtract_to_negative = galaxy::dsp_accumulator_subtract(0, 1, 0);
    passed &= expect(subtract_to_negative.value == -1, "zero minus one wraps negative");
    passed &= expect(
        !has(subtract_to_negative.status, galaxy::kDspSrCarry),
        "zero minus one records borrow by clearing carry");
    passed &= expect(
        has(subtract_to_negative.status, galaxy::kDspSrSign),
        "zero minus one sets sign");

    const auto negative_overflow =
        galaxy::dsp_accumulator_subtract(galaxy::kDspAccumulatorMin, 1, 0);
    passed &= expect(
        negative_overflow.value == galaxy::kDspAccumulatorMax,
        "subtracting from forty-bit min wraps to max");
    passed &= expect(
        has(negative_overflow.status, galaxy::kDspSrOverflow),
        "negative subtract overflow sets overflow");
    passed &= expect(
        has(negative_overflow.status, galaxy::kDspSrCarry),
        "negative subtract overflow still has no unsigned borrow");

    const auto negate_zero = galaxy::dsp_accumulator_negate(0, 0);
    passed &= expect(negate_zero.value == 0, "negating zero stays zero");
    passed &= expect(
        has(negate_zero.status, galaxy::kDspSrCarry),
        "negating zero sets carry through subtract semantics");

    const auto negate_min = galaxy::dsp_accumulator_negate(galaxy::kDspAccumulatorMin, 0);
    passed &= expect(
        negate_min.value == galaxy::kDspAccumulatorMin,
        "negating forty-bit min wraps to itself");
    passed &= expect(
        has(negate_min.status, galaxy::kDspSrOverflow),
        "negating forty-bit min overflows");

    const auto set_negative =
        galaxy::dsp_accumulator_set_value(0x0000ffffffffffll, 0);
    passed &= expect(
        set_negative.value == -1,
        "setting an accumulator value canonicalizes to signed forty-bit");
    passed &= expect(
        has(set_negative.status, galaxy::kDspSrSign),
        "setting a negative value updates sign status");

    const auto clear_low =
        galaxy::dsp_accumulator_clear_low(0x00010001ffffll, 0);
    passed &= expect(
        clear_low.value == 0x000100020000ll,
        "clear-low helper rounds and clears the low accumulator word");

    const auto absolute_negative =
        galaxy::dsp_accumulator_absolute(-0x00020000ll, 0);
    passed &= expect(
        absolute_negative.value == 0x00020000ll,
        "absolute helper negates negative accumulator values");
    passed &= expect(
        !has(absolute_negative.status, galaxy::kDspSrSign),
        "absolute helper updates sign status from the final value");

    const auto absolute_min =
        galaxy::dsp_accumulator_absolute(galaxy::kDspAccumulatorMin, 0);
    passed &= expect(
        absolute_min.value == galaxy::kDspAccumulatorMin,
        "absolute helper preserves wrapped forty-bit minimum value");
    passed &= expect(
        !has(absolute_min.status, galaxy::kDspSrOverflow),
        "absolute helper follows status-update semantics without subtract overflow");

    const auto shift_left = galaxy::dsp_accumulator_shift(
        0x0000000001ll, galaxy::DspAccumulatorShiftKind::LogicalLeft, 16, 0);
    passed &= expect(
        shift_left.value == 0x0000010000ll,
        "logical left shift moves through the forty-bit accumulator");

    const auto shift_logical_right = galaxy::dsp_accumulator_shift(
        -1, galaxy::DspAccumulatorShiftKind::LogicalRight, 16, 0);
    passed &= expect(
        shift_logical_right.value == 0x00ffffffll,
        "logical right shift masks to forty bits before shifting");
    passed &= expect(
        !has(shift_logical_right.status, galaxy::kDspSrSign),
        "logical right shift updates sign from the final positive value");

    const auto shift_arithmetic_right = galaxy::dsp_accumulator_shift(
        -0x00020000ll, galaxy::DspAccumulatorShiftKind::ArithmeticRight, 1, 0);
    passed &= expect(
        shift_arithmetic_right.value == -0x00010000ll,
        "arithmetic right shift preserves the accumulator sign");

    const auto shift_dynamic_right =
        galaxy::dsp_accumulator_shift_by_signed_count(0x00040000ll, false, 0x0002, 0);
    passed &= expect(
        shift_dynamic_right.value == 0x00010000ll,
        "dynamic logical shift uses a positive AC1.M count as a right shift");

    const auto shift_dynamic_left =
        galaxy::dsp_accumulator_shift_by_signed_count(0x00010000ll, false, 0x007f, 0);
    passed &= expect(
        shift_dynamic_left.value == 0x00020000ll,
        "dynamic logical shift uses a negative AC1.M count as a left shift");

    const auto shift_dynamic_arithmetic =
        galaxy::dsp_accumulator_shift_by_signed_count(-0x00040000ll, true, 0x0002, 0);
    passed &= expect(
        shift_dynamic_arithmetic.value == -0x00010000ll,
        "dynamic arithmetic shift sign-extends positive right-shift counts");

    const auto register_shift_left =
        galaxy::dsp_accumulator_shift_by_register_count(0x00010000ll, false, 0x0002, 0);
    passed &= expect(
        register_shift_left.value == 0x00040000ll,
        "register-counted logical shift uses a positive count as a left shift");

    const auto register_shift_right =
        galaxy::dsp_accumulator_shift_by_register_count(0x00010000ll, false, 0x007f, 0);
    passed &= expect(
        register_shift_right.value == 0x00008000ll,
        "register-counted logical shift uses a negative count as a right shift");

    const auto register_shift_arithmetic =
        galaxy::dsp_accumulator_shift_by_register_count(-0x00040000ll, true, 0x007f, 0);
    passed &= expect(
        register_shift_arithmetic.value == -0x00020000ll,
        "register-counted arithmetic shift sign-extends negative right-shift counts");

    passed &= expect(
        galaxy::dsp_accumulator_from_ax_pair(0x1234, 0x8001) == -2147413452ll,
        "AX pair helper sign-extends the high half as a 32-bit accumulator value");
    passed &= expect(
        galaxy::dsp_accumulator_from_signed_mid(0x8001) == -2147418112ll,
        "signed-mid helper sign-extends a halfword into accumulator mid position");

    const std::uint16_t preserved =
        galaxy::kDspSrLogicZero | galaxy::kDspSrStickyOverflow;
    const auto preserved_status = galaxy::dsp_accumulator_add(1, 1, preserved);
    passed &= expect(
        has(preserved_status.status, galaxy::kDspSrLogicZero),
        "arithmetic status preserves logic-zero flag");
    passed &= expect(
        has(preserved_status.status, galaxy::kDspSrStickyOverflow),
        "arithmetic status preserves prior sticky overflow");
    passed &= expect(
        !has(preserved_status.status, galaxy::kDspSrArithmeticZero),
        "nonzero result clears arithmetic zero");

    const auto status16_zero = galaxy::dsp_status_16(
        0,
        false,
        false,
        false,
        galaxy::kDspSrCarry | galaxy::kDspSrStickyOverflow);
    passed &= expect(
        has(status16_zero, galaxy::kDspSrArithmeticZero),
        "16-bit status helper sets zero for zero value");
    passed &= expect(
        has(status16_zero, galaxy::kDspSrTopTwoBitsEqual),
        "16-bit status helper sets top-two-bits flag for 0x0000");
    passed &= expect(
        !has(status16_zero, galaxy::kDspSrCarry),
        "16-bit status helper clears old carry");
    passed &= expect(
        has(status16_zero, galaxy::kDspSrStickyOverflow),
        "16-bit status helper preserves old sticky overflow");

    const auto status16_negative = galaxy::dsp_status_16(
        static_cast<std::int16_t>(0x8000u), false, false, false, 0);
    passed &= expect(
        has(status16_negative, galaxy::kDspSrSign),
        "16-bit status helper sets sign for negative value");
    passed &= expect(
        !has(status16_negative, galaxy::kDspSrTopTwoBitsEqual),
        "16-bit status helper clears top-two-bits flag for 0x8000");

    const auto status16_over_s32 =
        galaxy::dsp_status_16(0x4000, false, false, true, 0);
    passed &= expect(
        has(status16_over_s32, galaxy::kDspSrOverSigned32),
        "16-bit status helper accepts explicit over-signed-32 input");

    passed &= expect(galaxy::dsp_read_accumulator_low(0x0012345678ll) == 0x5678,
                     "low accumulator read returns the low word");
    passed &= expect(galaxy::dsp_write_accumulator_low(0x0012345678ll, 0xabcd) ==
                         0x001234abcdll,
                     "low accumulator write preserves high and mid words");
    passed &= expect(galaxy::dsp_write_accumulator_low(-1, 0x0000) == -65536ll,
                     "low accumulator write preserves 40-bit sign extension");

    passed &= expect(
        galaxy::dsp_read_accumulator_mid(0x0012345678ll, 0) == 0x1234,
        "mid accumulator read returns unsaturated middle word in 16-bit mode");
    passed &= expect(
        galaxy::dsp_read_accumulator_mid_raw(0x0012345678ll) == 0x1234,
        "raw mid accumulator read returns the middle word");
    passed &= expect(
        galaxy::dsp_read_accumulator_high(0x7f00000000ll) == 0x007f,
        "high accumulator read returns a positive sign-extended byte");
    passed &= expect(
        galaxy::dsp_read_accumulator_high(-1) == 0xffff,
        "high accumulator read sign-extends negative one");
    passed &= expect(
        galaxy::dsp_read_accumulator_high(galaxy::kDspAccumulatorMin) == 0xff80,
        "high accumulator read sign-extends the 40-bit minimum byte");
    passed &= expect(
        galaxy::dsp_write_accumulator_high(0x0012345678ll, 0x007f) == 0x7f12345678ll,
        "high accumulator write preserves mid and low words for positive high byte");
    const auto negative_high =
        galaxy::dsp_write_accumulator_high(0x0012345678ll, 0xff80);
    passed &= expect(
        negative_high < 0 &&
            galaxy::dsp_pack_accumulator(negative_high) == 0x8012345678ull,
        "high accumulator write masks to 8 bits and sign-extends negative high byte");
    passed &= expect(
        galaxy::dsp_read_accumulator_mid(0x000080000000ll, galaxy::kDspSr40BitMode) ==
            0x7fff,
        "mid accumulator read saturates positive overflow in 40-bit mode");
    passed &= expect(
        galaxy::dsp_read_accumulator_mid_raw(0x000080000000ll) == 0x8000,
        "raw mid accumulator read bypasses 40-bit saturation");
    passed &= expect(
        galaxy::dsp_read_accumulator_mid(-2147483649ll, galaxy::kDspSr40BitMode) ==
            0x8000,
        "mid accumulator read saturates negative overflow in 40-bit mode");
    passed &= expect(
        galaxy::dsp_write_accumulator_mid(0x0011223344ll, 0xabcd, 0) ==
            0x00abcd3344ll,
        "mid accumulator write preserves high and low in 16-bit mode");
    passed &= expect(
        galaxy::dsp_write_accumulator_mid(0x0011223344ll, 0x1234, galaxy::kDspSr40BitMode) ==
            0x000012340000ll,
        "mid accumulator write sign-extends and clears low in 40-bit mode");
    passed &= expect(
        galaxy::dsp_write_accumulator_mid(0, 0x8001, galaxy::kDspSr40BitMode) ==
            -2147418112ll,
        "negative mid accumulator write sign-extends in 40-bit mode");

    passed &= expect(
        galaxy::dsp_logic_zero_status(true, galaxy::kDspSrCarry) ==
            (galaxy::kDspSrCarry | galaxy::kDspSrLogicZero),
        "logic-zero status helper sets LZ while preserving unrelated flags");
    passed &= expect(
        galaxy::dsp_logic_zero_status(
            false, galaxy::kDspSrCarry | galaxy::kDspSrLogicZero) ==
            galaxy::kDspSrCarry,
        "logic-zero status helper clears LZ while preserving unrelated flags");

    const auto set_mid_logic = galaxy::dsp_accumulator_set_mid_logic(
        0x0011223344ll,
        0xabcd,
        galaxy::kDspSrStickyOverflow | galaxy::kDspSrLogicZero);
    passed &= expect(set_mid_logic.accumulator == 0x00abcd3344ll,
                     "logic mid write replaces only the accumulator mid word");
    passed &= expect(
        has(set_mid_logic.status, galaxy::kDspSrStickyOverflow),
        "logic mid write preserves sticky overflow");
    passed &= expect(
        has(set_mid_logic.status, galaxy::kDspSrLogicZero),
        "logic mid write preserves logic-zero flag");
    passed &= expect(
        has(set_mid_logic.status, galaxy::kDspSrOverSigned32),
        "logic mid write updates arithmetic over-signed-32 flag");
    passed &= expect(
        !has(set_mid_logic.status, galaxy::kDspSrCarry),
        "logic mid write clears arithmetic carry");

    const auto xor_mid =
        galaxy::dsp_accumulator_mid_xor(0x0012345678ll, 0x00ff, 0);
    passed &= expect(xor_mid.accumulator == 0x0012cb5678ll,
                     "mid xor updates only the middle word");

    struct MidFlagsCase {
        std::int64_t input;
        std::uint16_t middle;
        std::int64_t output;
        std::uint16_t status;
    };
    constexpr std::array<MidFlagsCase, 6> mid_flags{{
        {1, 0, 1, 0x0024u},
        {0, 0x8000u, 0x80000000ll, 0x0018u},
        {-0xFFFFFFFFll, 0, -0xFFFFFFFFll, 0x0034u},
        {0, 0x3FFFu, 0x3FFF0000ll, 0x0020u},
        {0, 0x4000u, 0x40000000ll, 0x0000u},
        {0, 0xC000u, 0xC0000000ll, 0x0038u},
    }};
    for (const auto& item : mid_flags) {
        const auto result = galaxy::dsp_accumulator_set_mid_logic(
            item.input, item.middle, 0x40FFu);
        passed &= expect(
            result.accumulator == item.output &&
                result.status == (item.status | 0x40C0u),
            "mid logic uses middle AZ/sign/TB and full-value S32");
    }
    const auto low_only_and = galaxy::dsp_accumulator_mid_and(1, 0, 0);
    passed &= expect(low_only_and.accumulator == 1 &&
                         low_only_and.status == 0x0024u,
                     "AND of zero middle sets AZ despite retained low word");

    const auto and_mid =
        galaxy::dsp_accumulator_mid_and(0x0000ffff0000ll, 0x0000, 0);
    passed &= expect(and_mid.accumulator == 0,
                     "mid and can clear the full accumulator when high/low are zero");
    passed &= expect(
        has(and_mid.status, galaxy::kDspSrArithmeticZero),
        "mid and zero result sets arithmetic zero");

    const auto or_mid =
        galaxy::dsp_accumulator_mid_or(0x0000000001ll, 0x00f0, 0);
    passed &= expect(or_mid.accumulator == 0x0000f00001ll,
                     "mid or preserves low word while setting mid bits");

    const auto not_mid = galaxy::dsp_accumulator_mid_not(0, 0);
    passed &= expect(not_mid.accumulator == 0x000ffff0000ll,
                     "mid not flips the sixteen-bit middle word");
    passed &= expect(
        has(not_mid.status, galaxy::kDspSrOverSigned32),
        "mid not result updates arithmetic range flags");

    const auto cleared_product = galaxy::dsp_cleared_product();
    passed &= expect(cleared_product.low == 0x0000,
                     "cleared product has documented low word");
    passed &= expect(cleared_product.mid == 0xfff0,
                     "cleared product has documented mid word");
    passed &= expect(cleared_product.high == 0x00ff,
                     "cleared product has documented high word");
    passed &= expect(cleared_product.mid2 == 0x0010,
                     "cleared product has documented mid2 word");
    passed &= expect(galaxy::dsp_product_value(cleared_product) == 0,
                     "cleared product parts evaluate to zero");

    const auto negative_product = galaxy::dsp_store_product_value(-1);
    passed &= expect(negative_product.low == 0xffff,
                     "stored negative product keeps low word");
    passed &= expect(negative_product.mid == 0xffff,
                     "stored negative product keeps mid word");
    passed &= expect(negative_product.high == 0xffff,
                     "stored negative product keeps sign high word");
    passed &= expect(negative_product.mid2 == 0,
                     "stored product clears mid2 word");
    passed &= expect(galaxy::dsp_product_value(negative_product) == -1,
                     "stored negative product evaluates correctly");

    passed &= expect(
        galaxy::dsp_round_product(galaxy::dsp_store_product_value(0x0000ffff)) ==
            0x00010000,
        "product rounding below bit 16 rounds into mid word");
    passed &= expect(
        galaxy::dsp_round_product(galaxy::dsp_store_product_value(-1)) == 0,
        "product rounding all low negative bits crosses to zero");

    const auto product_ax_sum =
        galaxy::dsp_accumulator_add_rounded_product_and_ax_clear_low(
            galaxy::dsp_store_product_value(0x0000ffff), 0x5678, 0x0001, 0);
    passed &= expect(
        product_ax_sum.value == 0x00020000ll,
        "rounded product plus AX helper clears AX low bits before adding");
    passed &= expect(
        !has(product_ax_sum.status, galaxy::kDspSrCarry),
        "rounded product plus AX helper leaves carry clear without unsigned wrap");

    const auto product_negative_ax =
        galaxy::dsp_accumulator_add_rounded_product_and_ax_clear_low(
            galaxy::dsp_store_product_value(0), 0xffff, 0xffff, 0);
    passed &= expect(
        product_negative_ax.value == -0x00010000ll,
        "rounded product plus AX helper sign-extends negative AX values");
    passed &= expect(
        has(product_negative_ax.status, galaxy::kDspSrSign),
        "rounded product plus AX helper updates sign status");

    const auto product_ax_carry =
        galaxy::dsp_accumulator_add_rounded_product_and_ax_clear_low(
            galaxy::dsp_store_product_value(-1), 0, 0, 0);
    passed &= expect(
        product_ax_carry.value == 0,
        "rounded negative product can cross to zero before AX add");
    passed &= expect(
        has(product_ax_carry.status, galaxy::kDspSrCarry),
        "rounded product plus AX helper mirrors ADDPAXZ carry status");
    passed &= expect(
        !has(product_ax_carry.status, galaxy::kDspSrOverflow),
        "rounded product plus AX helper keeps overflow clear");

    constexpr std::uint16_t normal_multiply = galaxy::kDspSrMultiplyModifier;
    constexpr std::uint16_t unsigned_normal_multiply =
        galaxy::kDspSrMultiplyModifier | galaxy::kDspSrMultiplyUnsigned;
    passed &= expect(
        galaxy::dsp_multiply_terms(
            0xffff,
            0x0002,
            galaxy::DspMultiplyOperandMode::Signed,
            normal_multiply) == -2,
        "signed multiply treats 0xffff as negative one");
    passed &= expect(
        galaxy::dsp_multiply_terms(
            0xffff,
            0x0002,
            galaxy::DspMultiplyOperandMode::Signed,
            0) == -4,
        "clear multiply-modifier bit doubles signed products");
    passed &= expect(
        galaxy::dsp_multiply_terms(
            0xffff,
            0x0002,
            galaxy::DspMultiplyOperandMode::UnsignedWhenSrUnsigned,
            unsigned_normal_multiply) == 131070,
        "unsigned multiply mode treats low halves as unsigned");
    passed &= expect(
        galaxy::dsp_multiply_terms(
            0xffff,
            0xffff,
            galaxy::DspMultiplyOperandMode::MixedUnsignedLeftWhenSrUnsigned,
            unsigned_normal_multiply) == -65535,
        "mixed multiply mode treats left low half as unsigned");
    passed &= expect(
        galaxy::dsp_multiply_terms(
            0xffff,
            0xffff,
            galaxy::DspMultiplyOperandMode::MixedUnsignedLeftWhenSrUnsigned,
            normal_multiply) == 1,
        "mixed multiply falls back to signed when unsigned mode is clear");

    const auto unsigned_product = galaxy::dsp_product_multiply(
        0xffff,
        0xffff,
        galaxy::DspMultiplyOperandMode::UnsignedWhenSrUnsigned,
        unsigned_normal_multiply);
    passed &= expect(
        galaxy::dsp_product_value(unsigned_product) == 0xfffe0001ll,
        "product multiply stores unsigned low-half result");

    passed &= expect(
        galaxy::dsp_product_value(galaxy::dsp_product_multiply(
            0x4000,
            0xffff,
            galaxy::DspMultiplyOperandMode::MixedUnsignedLeftWhenSrUnsigned,
            unsigned_normal_multiply)) == -0x4000ll,
        "RMGE01 output multiply preserves a negative signed mixer sample");
    passed &= expect(
        native_rmge01_mix_sample(0x8000u, 0x8000u, 0x7fffu) ==
            reference_rmge01_mix_sample(0x8000u, 0x8000u, 0x7fffu),
        "RMGE01 upstream mixer preserves negative base and product semantics");

    std::uint64_t rmge01_mix_lcg = 0xd5a6'12f0'87c4'39b1ull;
    bool rmge01_mix_sweep_matches = true;
    for (std::uint32_t sample = 0; sample < 0x2'0000u; ++sample) {
        const auto next_mix_value = [&rmge01_mix_lcg]() {
            rmge01_mix_lcg =
                rmge01_mix_lcg * 6364136223846793005ull +
                1442695040888963407ull;
            return static_cast<std::uint16_t>(rmge01_mix_lcg >> 32u);
        };
        const std::uint16_t base_sample = next_mix_value();
        const std::uint16_t ramp_gain = next_mix_value();
        const std::uint16_t source_sample = next_mix_value();
        if (native_rmge01_mix_sample(base_sample, ramp_gain, source_sample) !=
            reference_rmge01_mix_sample(base_sample, ramp_gain, source_sample)) {
            rmge01_mix_sweep_matches = false;
            break;
        }
    }
    passed &= expect(
        rmge01_mix_sweep_matches,
        "RMGE01 PCs 0x0DA5/0x0DA7 match the independent signed mixer oracle");

    passed &= expect(
        native_rmge01_output_sample(0x4000u, 0x1fffu) == 0x7ffcu,
        "RMGE01 output conversion keeps the last unsaturated positive sample");
    passed &= expect(
        native_rmge01_output_sample(0x4000u, 0x2000u) == 0x7fffu,
        "RMGE01 output conversion saturates at positive signed-32 overflow");
    passed &= expect(
        native_rmge01_output_sample(0x4000u, 0xe000u) == 0x8000u,
        "RMGE01 output conversion preserves the exact negative signed-32 edge");

    constexpr std::array<std::uint16_t, 8> rmge01_master_gains{
        0x0000u,
        0x0001u,
        0x0fffu,
        0x1000u,
        0x4000u,
        0x7fffu,
        0x8000u,
        0xffffu,
    };
    bool rmge01_output_sweep_matches = true;
    for (const std::uint16_t master_gain : rmge01_master_gains) {
        for (std::uint32_t sample = 0; sample <= 0xffffu; ++sample) {
            const auto mixer_sample = static_cast<std::uint16_t>(sample);
            if (native_rmge01_output_sample(master_gain, mixer_sample) !=
                reference_rmge01_output_sample(master_gain, mixer_sample)) {
                rmge01_output_sweep_matches = false;
                break;
            }
        }
        if (!rmge01_output_sweep_matches) {
            break;
        }
    }
    passed &= expect(
        rmge01_output_sweep_matches,
        "RMGE01 PC 0x0DB1 ALU path matches the independent output-conversion oracle");

    const auto product_sum = galaxy::dsp_product_multiply_add(
        galaxy::dsp_store_product_value(0x00010000),
        0xffff,
        0x0002,
        galaxy::DspMultiplyOperandMode::Signed,
        normal_multiply);
    passed &= expect(galaxy::dsp_product_value(product_sum) == 0x0000fffe,
                     "multiply-add adds signed product to old product");

    const auto product_difference = galaxy::dsp_product_multiply_subtract(
        galaxy::dsp_store_product_value(0x00010000),
        0xffff,
        0x0002,
        galaxy::DspMultiplyOperandMode::Signed,
        normal_multiply);
    passed &= expect(galaxy::dsp_product_value(product_difference) == 0x00010002,
                     "multiply-subtract subtracts signed product from old product");

    passed &= expect(galaxy::dsp_increment_address(0x1234, 0xffff) == 0x1235,
                     "address increment advances without modulo wrap");
    passed &= expect(galaxy::dsp_increment_address(0x000f, 0x000f) == 0x0000,
                     "address increment wraps inside a small modulo range");
    passed &= expect(galaxy::dsp_increment_address(0x001f, 0x000f) == 0x0010,
                     "address increment preserves the modulo window base");
    passed &= expect(galaxy::dsp_decrement_address(0x0000, 0x000f) == 0x000f,
                     "address decrement wraps to the end of a modulo range");
    passed &= expect(galaxy::dsp_increase_address(0x000e, 2, 0x000f) == 0x0000,
                     "positive indexed address increase wraps forward");
    passed &= expect(galaxy::dsp_decrease_address(0x0001, 2, 0x000f) == 0x000f,
                     "positive indexed address decrease wraps backward");
    passed &= expect(galaxy::dsp_increase_address(0x0001, -2, 0x000f) == 0x000f,
                     "negative indexed address increase wraps backward");
    passed &= expect(galaxy::dsp_decrease_address(0x000e, -2, 0x000f) == 0x0000,
                     "negative indexed address decrease wraps forward");

    std::uint64_t lcg_state = 0x1234abcd5678ef90ull;
    const auto next_lcg = [&lcg_state]() {
        lcg_state = lcg_state * 6364136223846793005ull + 1442695040888963407ull;
        return lcg_state;
    };
    for (int i = 0; i < 4096; ++i) {
        const std::int64_t left = galaxy::dsp_sign_extend_accumulator(next_lcg());
        const std::int64_t right = galaxy::dsp_sign_extend_accumulator(next_lcg());
        const auto sum = galaxy::dsp_accumulator_add(left, right, 0);
        const auto difference = galaxy::dsp_accumulator_subtract(sum.value, right, 0);
        passed &= expect(
            difference.value == left,
            "deterministic accumulator add/sub sweep round-trips 40-bit values");
    }

    constexpr std::array<std::uint16_t, 4> wrap_samples{
        0x000fu,
        0x00ffu,
        0x0fffu,
        0xffffu,
    };
    for (const std::uint16_t wrap : wrap_samples) {
        for (std::uint32_t sample = 0; sample < 512; ++sample) {
            const auto address =
                static_cast<std::uint16_t>((sample * 37u) ^ (wrap << 1u));
            const auto incremented = galaxy::dsp_increment_address(address, wrap);
            const auto decremented = galaxy::dsp_decrement_address(incremented, wrap);
            passed &= expect(
                decremented == address,
                "deterministic modulo address sweep preserves inc/dec inverse");
        }
    }

    const galaxy::DspContext context{};
    passed &= expect(context.ar[0] == 0, "DSP context address registers default to zero");
    passed &= expect(context.ix[0] == 0, "DSP context index registers default to zero");
    passed &= expect(context.wr[0] == 0, "DSP context wrap registers default to zero");
    passed &= expect(context.ax[0][0] == 0, "DSP context operand registers default to zero");
    passed &= expect(context.ac[0] == 0, "DSP context accumulators default to zero");
    passed &= expect(context.st[0] == 0, "DSP context stack registers default to zero");
    passed &= expect(context.cr == 0, "DSP context control register defaults to zero");
    passed &= expect(context.sr == 0, "DSP context status register defaults to zero");
    passed &= expect(context.iram[0] == 0, "DSP context IRAM defaults to zero");
    passed &= expect(context.dram[0] == 0, "DSP context DRAM defaults to zero");
    passed &= expect(context.coef[0] == 0, "DSP context coefficient ROM defaults to zero");
    passed &= expect(context.ifx[0] == 0, "DSP context IFX registers default to zero");
    passed &= expect(context.dsp_mailbox == 0, "DSP mailbox defaults to empty");
    passed &= expect(context.cpu_mailbox == 0, "CPU mailbox defaults to empty");
    passed &= expect(context.hardware.request_interrupt == nullptr,
                     "DSP hardware services default to disconnected");
    passed &= expect(!context.halted, "DSP context is not halted by default");
    passed &= expect(!context.coef_loaded, "DSP coefficient ROM starts unloaded");

    galaxy::DspContext mutable_context{};
    mutable_context.iram[0x0123] = 0xabcd;
    passed &= expect(galaxy::dsp_iram_read(mutable_context, 0x0123) == 0xabcd,
                     "DSP context IRAM helper reads a user-supplied word");
    galaxy::dsp_dram_write(mutable_context, 0x0123, 0xbeef);
    passed &= expect(galaxy::dsp_dram_read(mutable_context, 0x0123) == 0xbeef,
                     "DSP context DRAM helper round-trips a word");
    galaxy::dsp_data_write(mutable_context, 0x0456, 0xcafe);
    passed &= expect(galaxy::dsp_data_read(mutable_context, 0x0456) == 0xcafe,
                     "DSP data helper routes low memory to DRAM");
    mutable_context.coef_loaded = true;
    mutable_context.coef[0x0003] = 0x4567;
    passed &= expect(galaxy::dsp_data_read(mutable_context, 0x1803) == 0x4567,
                     "DSP data helper reads mirrored coefficient ROM after it is loaded");

    // Address ranges outside DRAM (0x0xxx), coefficient ROM (0x1xxx), and IFX
    // (0xFxxx) are wired to nothing on real DSP silicon: there is no MMU or
    // access-fault mechanism, so hardware reads back an undefined value (0)
    // and drops writes. Dolphin's DSP LLE core -- which runs real DSP
    // microcode bit-exactly -- models the same behavior (ReadDMEM/WriteDMEM
    // log and return 0 / drop the write; they never fault). This is reachable
    // during ordinary gameplay: a legitimate JAudio2 call site feeds a
    // non-positional/BGM channel's distance term with a deliberate "not
    // applicable" sentinel, which the predictor-index computation turns into
    // an address like 0x7fff.
    passed &= expect(galaxy::dsp_data_read(mutable_context, 0x7fffu) == 0u,
                     "DSP data helper returns zero for an address outside DRAM/COEF/IFX");
    const auto dram_before_unmapped_write = mutable_context.dram[0x0123];
    galaxy::dsp_data_write(mutable_context, 0x7fffu, 0xdead);
    passed &= expect(
        mutable_context.dram[0x0123] == dram_before_unmapped_write &&
            galaxy::dsp_data_read(mutable_context, 0x7fffu) == 0u,
        "DSP data helper silently drops a write to an address outside DRAM/COEF/IFX");

    galaxy::dsp_data_write(mutable_context, 0xfffc, 0x1234);
    passed &= expect(mutable_context.dsp_mailbox == 0x12340000u,
                     "DSP mailbox high write updates the high half and clears busy");
    galaxy::dsp_data_write(mutable_context, 0xfffd, 0x5678);
    passed &= expect(mutable_context.dsp_mailbox == 0x92345678u,
                     "DSP mailbox low write updates the low half and sets busy");
    passed &= expect(galaxy::dsp_data_read(mutable_context, 0xfffc) == 0x9234,
                     "DSP mailbox high read includes the busy bit");
    passed &= expect(galaxy::dsp_data_read(mutable_context, 0xfffd) == 0x5678,
                     "DSP mailbox low read returns the payload");
    passed &= expect(mutable_context.dsp_mailbox == 0x12345678u,
                     "DSP mailbox low read clears busy");

    galaxy::dsp_data_write(mutable_context, 0xfffe, 0xaaaa);
    galaxy::dsp_data_write(mutable_context, 0xffff, 0xbbbb);
    passed &= expect(
        mutable_context.cpu_mailbox == (0xaaaabbbbu | galaxy::kDspMailboxBusy),
        "CPU mailbox IFX writes use the same busy-bit convention");

    InterruptProbe interrupt_probe{};
    mutable_context.hardware.request_interrupt = record_interrupt;
    mutable_context.hardware_user = &interrupt_probe;
    galaxy::dsp_data_write(mutable_context, 0xfffb, 1);
    passed &= expect(interrupt_probe.count == 1,
                     "DSP DIRQ bit zero requests a host interrupt through services");

    galaxy::dsp_data_write(mutable_context, 0xffef, 1);
    galaxy::dsp_data_write(mutable_context, 0xffc9, 0x0003);
    galaxy::dsp_data_write(mutable_context, 0xffcb, 0x0020);
    passed &= expect(mutable_context.ifx[galaxy::kDspIfxDsbl] == 0,
                     "masked DSP DMA clears the block-length register");
    passed &= expect(mutable_context.ifx[galaxy::kDspIfxDscr] == 0x0003,
                     "masked DSP DMA clears the transient busy bit");

    AcceleratorMemoryProbe dma_in_probe{};
    galaxy::DspContext dma_in_context{};
    dma_in_context.hardware.validate_external_span = validate_external_span;
    dma_in_context.hardware.external_read_byte = read_external_byte;
    dma_in_context.hardware_user = &dma_in_probe;
    dma_in_probe.memory[0x20] = 0x12;
    dma_in_probe.memory[0x21] = 0x34;
    dma_in_probe.memory[0x22] = 0xab;
    dma_in_probe.memory[0x23] = 0xcd;
    galaxy::dsp_data_write(dma_in_context, 0xffce, 0x0000);
    galaxy::dsp_data_write(dma_in_context, 0xffcf, 0x0020);
    galaxy::dsp_data_write(dma_in_context, 0xffcd, 0x0001);
    galaxy::dsp_data_write(dma_in_context, 0xffc9, 0x0000);
    galaxy::dsp_data_write(dma_in_context, 0xffcb, 0x0004);
    passed &= expect(dma_in_context.dram[1] == 0x1234 && dma_in_context.dram[2] == 0xabcd,
                     "DSP DMA host-to-DRAM copies external bytes into big-endian words");
    passed &= expect(dma_in_probe.byte_reads == 4,
                     "DSP DMA host-to-DRAM consumes the block length in bytes");
    passed &= expect(dma_in_context.ifx[galaxy::kDspIfxDsbl] == 0,
                     "DSP DMA host-to-DRAM clears the block-length register");
    passed &= expect(dma_in_context.ifx[galaxy::kDspIfxDscr] == 0x0000,
                     "DSP DMA host-to-DRAM clears busy after copying");

    AcceleratorMemoryProbe dma_out_probe{};
    galaxy::DspContext dma_out_context{};
    dma_out_context.hardware.validate_external_span = validate_external_span;
    dma_out_context.hardware.external_write_byte = write_external_byte;
    dma_out_context.hardware_user = &dma_out_probe;
    dma_out_context.dram[1] = 0x5566;
    dma_out_context.dram[2] = 0x7788;
    galaxy::dsp_data_write(dma_out_context, 0xffce, 0x0000);
    galaxy::dsp_data_write(dma_out_context, 0xffcf, 0x0030);
    galaxy::dsp_data_write(dma_out_context, 0xffcd, 0x0001);
    galaxy::dsp_data_write(dma_out_context, 0xffc9, 0x0001);
    galaxy::dsp_data_write(dma_out_context, 0xffcb, 0x0004);
    passed &= expect(dma_out_probe.memory[0x30] == 0x55 && dma_out_probe.memory[0x31] == 0x66 &&
                         dma_out_probe.memory[0x32] == 0x77 &&
                         dma_out_probe.memory[0x33] == 0x88,
                     "DSP DMA DRAM-to-host copies big-endian word bytes out");
    passed &= expect(dma_out_probe.byte_writes == 4,
                     "DSP DMA DRAM-to-host writes the block length in bytes");
    passed &= expect(dma_out_context.ifx[galaxy::kDspIfxDscr] == 0x0001,
                     "DSP DMA DRAM-to-host preserves direction bits after busy clears");

    AcceleratorMemoryProbe dma_in_oob_probe{};
    galaxy::DspContext dma_in_oob_context{};
    dma_in_oob_context.pc = 0x0123u;
    dma_in_oob_context.hardware.validate_external_span = validate_external_span;
    dma_in_oob_context.hardware.external_read_byte = read_external_byte;
    dma_in_oob_context.hardware_user = &dma_in_oob_probe;
    dma_in_oob_context.dram[1] = 0x1357u;
    galaxy::dsp_data_write(dma_in_oob_context, 0xffceu, 0x0000u);
    galaxy::dsp_data_write(dma_in_oob_context, 0xffcfu, 0x00feu);
    galaxy::dsp_data_write(dma_in_oob_context, 0xffcdu, 0x0001u);
    galaxy::dsp_data_write(dma_in_oob_context, 0xffc9u, 0x0000u);
    passed &= expect_hard_trap(
        [&] {
            galaxy::dsp_data_write(dma_in_oob_context, 0xffcbu, 0x0004u);
        },
        "DSP DMA host-to-DRAM rejects a full-span MRAM boundary failure");
    passed &= expect(
        dma_in_oob_probe.byte_reads == 0u &&
            dma_in_oob_context.dram[1] == 0x1357u,
        "DSP DMA host-to-DRAM preflight prevents a partial transfer");

    AcceleratorMemoryProbe dma_out_oob_probe{};
    galaxy::DspContext dma_out_oob_context{};
    dma_out_oob_context.pc = 0x0456u;
    dma_out_oob_context.hardware.validate_external_span = validate_external_span;
    dma_out_oob_context.hardware.external_write_byte = write_external_byte;
    dma_out_oob_context.hardware_user = &dma_out_oob_probe;
    dma_out_oob_context.dram[1] = 0x2468u;
    dma_out_oob_probe.memory[0xfeu] = 0xaau;
    dma_out_oob_probe.memory[0xffu] = 0xbbu;
    galaxy::dsp_data_write(dma_out_oob_context, 0xffceu, 0x0000u);
    galaxy::dsp_data_write(dma_out_oob_context, 0xffcfu, 0x00feu);
    galaxy::dsp_data_write(dma_out_oob_context, 0xffcdu, 0x0001u);
    galaxy::dsp_data_write(dma_out_oob_context, 0xffc9u, 0x0001u);
    passed &= expect_hard_trap(
        [&] {
            galaxy::dsp_data_write(dma_out_oob_context, 0xffcbu, 0x0004u);
        },
        "DSP DMA DRAM-to-host rejects a full-span MRAM boundary failure");
    passed &= expect(
        dma_out_oob_probe.byte_writes == 0u &&
            dma_out_oob_probe.memory[0xfeu] == 0xaau &&
            dma_out_oob_probe.memory[0xffu] == 0xbbu,
        "DSP DMA DRAM-to-host preflight prevents a partial write");

    AcceleratorMemoryProbe dma_wrap_probe{};
    galaxy::DspContext dma_wrap_context{};
    dma_wrap_context.pc = 0x0789u;
    dma_wrap_context.hardware.validate_external_span = validate_external_span;
    dma_wrap_context.hardware.external_read_byte = read_external_byte;
    dma_wrap_context.hardware_user = &dma_wrap_probe;
    galaxy::dsp_data_write(dma_wrap_context, 0xffceu, 0xffffu);
    galaxy::dsp_data_write(dma_wrap_context, 0xffcfu, 0xfffeu);
    galaxy::dsp_data_write(dma_wrap_context, 0xffcdu, 0x0000u);
    galaxy::dsp_data_write(dma_wrap_context, 0xffc9u, 0x0000u);
    passed &= expect_hard_trap(
        [&] {
            galaxy::dsp_data_write(dma_wrap_context, 0xffcbu, 0x0004u);
        },
        "DSP DMA rejects a wrapping 32-bit MRAM span");
    passed &= expect(
        dma_wrap_probe.byte_reads == 0u,
        "DSP DMA wrap rejection happens before the first host callback");

    AcceleratorMemoryProbe dma_dsp_wrap_probe{};
    galaxy::DspContext dma_dsp_wrap_context{};
    dma_dsp_wrap_context.pc = 0x079au;
    dma_dsp_wrap_context.hardware.validate_external_span =
        validate_external_span;
    dma_dsp_wrap_context.hardware.external_read_byte = read_external_byte;
    dma_dsp_wrap_context.hardware_user = &dma_dsp_wrap_probe;
    galaxy::dsp_data_write(dma_dsp_wrap_context, 0xffceu, 0x0000u);
    galaxy::dsp_data_write(dma_dsp_wrap_context, 0xffcfu, 0x0000u);
    galaxy::dsp_data_write(dma_dsp_wrap_context, 0xffcdu, 0x8000u);
    galaxy::dsp_data_write(dma_dsp_wrap_context, 0xffc9u, 0x0000u);
    passed &= expect_hard_trap(
        [&] {
            galaxy::dsp_data_write(
                dma_dsp_wrap_context, 0xffcbu, 0x0002u);
        },
        "DSP DMA rejects a DSPA word address that would wrap when doubled");
    passed &= expect(
        dma_dsp_wrap_probe.byte_reads == 0u,
        "DSP DMA validates the complete DSP-local span before host reads");

    AcceleratorMemoryProbe idma_probe{};
    galaxy::DspContext idma_context{};
    idma_context.hardware.validate_external_span = validate_external_span;
    idma_context.hardware.external_read_byte = read_external_byte;
    idma_context.hardware_user = &idma_probe;
    idma_probe.memory[0x40] = 0xde;
    idma_probe.memory[0x41] = 0xad;
    galaxy::dsp_data_write(idma_context, 0xffce, 0x0000);
    galaxy::dsp_data_write(idma_context, 0xffcf, 0x0040);
    galaxy::dsp_data_write(idma_context, 0xffcd, 0x0002);
    galaxy::dsp_data_write(idma_context, 0xffc9, 0x0002);
    galaxy::dsp_data_write(idma_context, 0xffcb, 0x0002);
    passed &= expect(idma_context.iram[2] == 0xdead,
                     "DSP DMA host-to-IRAM copies external bytes into instruction words");
    passed &= expect(idma_context.ifx[galaxy::kDspIfxDscr] == 0x0002,
                     "DSP DMA host-to-IRAM preserves memory selector bits after busy clears");

    AcceleratorMemoryProbe pcm_probe{};
    galaxy::DspContext pcm_context{};
    pcm_context.hardware.validate_external_span = validate_external_span;
    pcm_context.hardware.external_read_byte = read_external_byte;
    pcm_context.hardware_user = &pcm_probe;
    pcm_probe.memory[0] = 0x12;
    pcm_probe.memory[1] = 0x34;
    galaxy::dsp_data_write(pcm_context, 0xffd1, 0x001a);
    galaxy::dsp_data_write(pcm_context, 0xffde, 0x0001);
    galaxy::dsp_data_write(pcm_context, 0xffd6, 0x0000);
    galaxy::dsp_data_write(pcm_context, 0xffd7, 0x0010);
    galaxy::dsp_data_write(pcm_context, 0xffd8, 0xffff);
    galaxy::dsp_data_write(pcm_context, 0xffd9, 0x0000);
    passed &= expect(galaxy::dsp_data_read(pcm_context, 0xffd8) == 0xbfff,
                     "DSP accelerator current-address high write masks bit 30");
    galaxy::dsp_data_write(pcm_context, 0xffd8, 0x0000);
    galaxy::dsp_data_write(pcm_context, 0xffd9, 0x0000);
    passed &= expect(galaxy::dsp_data_read(pcm_context, 0xffdd) == 0x1234,
                     "DSP accelerator decodes 16-bit PCM samples through host bytes");
    passed &= expect(galaxy::dsp_data_read(pcm_context, 0xffd9) == 0x0001,
                     "DSP accelerator PCM sample read advances the current address");
    passed &= expect(pcm_context.ifx[galaxy::kDspIfxYn1] == 0x1234,
                     "DSP accelerator PCM sample read updates YN1 history");
    passed &= expect(pcm_probe.byte_reads == 2,
                     "DSP accelerator 16-bit PCM reads two host bytes");

    AcceleratorMemoryProbe adpcm_probe{};
    galaxy::DspContext adpcm_context{};
    adpcm_context.hardware.validate_external_span = validate_external_span;
    adpcm_context.hardware.external_read_byte = read_external_byte;
    adpcm_context.hardware_user = &adpcm_probe;
    adpcm_probe.memory[0] = 0x70;
    galaxy::dsp_data_write(adpcm_context, 0xffd1, 0x0000);
    galaxy::dsp_data_write(adpcm_context, 0xffda, 0x0000);
    galaxy::dsp_data_write(adpcm_context, 0xffd6, 0x0000);
    galaxy::dsp_data_write(adpcm_context, 0xffd7, 0x0010);
    galaxy::dsp_data_write(adpcm_context, 0xffd8, 0x0000);
    galaxy::dsp_data_write(adpcm_context, 0xffd9, 0x0000);
    passed &= expect(galaxy::dsp_data_read(adpcm_context, 0xffdd) == 0x0007,
                     "DSP accelerator decodes the high nibble of 4-bit ADPCM");
    passed &= expect(galaxy::dsp_data_read(adpcm_context, 0xffd9) == 0x0001,
                     "DSP accelerator ADPCM sample read advances by one nibble");
    passed &= expect(adpcm_context.ifx[galaxy::kDspIfxYn1] == 0x0007,
                     "DSP accelerator ADPCM sample read updates YN1 history");

    AcceleratorMemoryProbe pred_scale_probe{};
    galaxy::DspContext pred_scale_context{};
    pred_scale_context.hardware.validate_external_span = validate_external_span;
    pred_scale_context.hardware.external_read_byte = read_external_byte;
    pred_scale_context.hardware_user = &pred_scale_probe;
    pred_scale_probe.memory[7] = 0x05;
    pred_scale_probe.memory[8] = 0x34;
    galaxy::dsp_data_write(pred_scale_context, 0xffd1, 0x0000);
    galaxy::dsp_data_write(pred_scale_context, 0xffda, 0x0000);
    galaxy::dsp_data_write(pred_scale_context, 0xffd6, 0x0000);
    galaxy::dsp_data_write(pred_scale_context, 0xffd7, 0x0020);
    galaxy::dsp_data_write(pred_scale_context, 0xffd8, 0x0000);
    galaxy::dsp_data_write(pred_scale_context, 0xffd9, 0x000f);
    passed &= expect(galaxy::dsp_data_read(pred_scale_context, 0xffdd) == 0x0005,
                     "DSP accelerator ADPCM reads the low nibble at odd addresses");
    passed &= expect(pred_scale_context.ifx[galaxy::kDspIfxPredScale] == 0x0034,
                     "DSP accelerator reloads pred-scale at ADPCM block boundaries");
    passed &= expect(galaxy::dsp_data_read(pred_scale_context, 0xffd9) == 0x0012,
                     "DSP accelerator pred-scale reload skips the block header bytes");

    AcceleratorMemoryProbe adpcm_clamp_probe{};
    galaxy::DspContext adpcm_clamp_context{};
    adpcm_clamp_context.hardware.validate_external_span = validate_external_span;
    adpcm_clamp_context.hardware.external_read_byte = read_external_byte;
    adpcm_clamp_context.hardware_user = &adpcm_clamp_probe;
    adpcm_clamp_probe.memory[0] = 0x80;
    galaxy::dsp_data_write(adpcm_clamp_context, 0xffd1, 0x0000);
    galaxy::dsp_data_write(adpcm_clamp_context, 0xffda, 0x000c);
    galaxy::dsp_data_write(adpcm_clamp_context, 0xffd6, 0x0000);
    galaxy::dsp_data_write(adpcm_clamp_context, 0xffd7, 0x0010);
    passed &= expect(galaxy::dsp_data_read(adpcm_clamp_context, 0xffdd) == 0x8001,
                     "DSP accelerator ADPCM clamps negative overflow to -0x7fff");

    AcceleratorMemoryProbe mmio_pcm_probe{};
    galaxy::DspContext mmio_pcm_context{};
    mmio_pcm_context.hardware.validate_external_span = validate_external_span;
    mmio_pcm_context.hardware.external_read_byte = read_external_byte;
    mmio_pcm_context.hardware_user = &mmio_pcm_probe;
    galaxy::dsp_data_write(mmio_pcm_context, 0xffd1, 0x0016);
    galaxy::dsp_data_write(mmio_pcm_context, 0xffde, 0x0001);
    galaxy::dsp_data_write(mmio_pcm_context, 0xffdf, 0x2468);
    galaxy::dsp_data_write(mmio_pcm_context, 0xffd8, 0x0000);
    galaxy::dsp_data_write(mmio_pcm_context, 0xffd9, 0x0005);
    galaxy::dsp_data_write(mmio_pcm_context, 0xffd6, 0x0000);
    galaxy::dsp_data_write(mmio_pcm_context, 0xffd7, 0x0020);
    passed &= expect(galaxy::dsp_data_read(mmio_pcm_context, 0xffdd) == 0x2468,
                     "DSP accelerator MMIO PCM reads ACIN instead of external memory");
    passed &= expect(galaxy::dsp_data_read(mmio_pcm_context, 0xffd9) == 0x0005,
                     "DSP accelerator MMIO PCM no-increment keeps current address");
    passed &= expect(mmio_pcm_probe.byte_reads == 0,
                     "DSP accelerator MMIO PCM no-increment does not touch external memory");

    // Accepted IFX extremes exercise the preserved modular32 predictor model;
    // these are host arithmetic fixtures, not an independent silicon oracle.
    AcceleratorMemoryProbe extreme_adpcm_probe{};
    galaxy::DspContext extreme_adpcm{};
    extreme_adpcm.hardware.validate_external_span = validate_external_span;
    extreme_adpcm.hardware.external_read_byte = read_external_byte;
    extreme_adpcm.hardware_user = &extreme_adpcm_probe;
    galaxy::dsp_data_write(extreme_adpcm, 0xFFD1u, 0x0000u);
    galaxy::dsp_data_write(extreme_adpcm, 0xFFD7u, 0x0010u);
    galaxy::dsp_data_write(extreme_adpcm, 0xFFA0u, 0x8000u);
    galaxy::dsp_data_write(extreme_adpcm, 0xFFA1u, 0x8000u);
    galaxy::dsp_data_write(extreme_adpcm, 0xFFDBu, 0x8000u);
    galaxy::dsp_data_write(extreme_adpcm, 0xFFDCu, 0x8000u);
    // 0x400 + 2*0x40000000 wraps to 0x80000400 before >>11 and clamp.
    passed &= expect(galaxy::dsp_data_read(extreme_adpcm, 0xFFDDu) == 0x8001u &&
                         extreme_adpcm.ifx[galaxy::kDspIfxYn1] == 0x8001u &&
                         extreme_adpcm.ifx[galaxy::kDspIfxYn2] == 0x8000u,
                     "ADPCM extreme prediction has defined wrap before shift and clamp");

    galaxy::DspContext extreme_pcm{};
    galaxy::dsp_data_write(extreme_pcm, 0xFFD1u, 0x0016u);
    galaxy::dsp_data_write(extreme_pcm, 0xFFD7u, 0x0020u);
    galaxy::dsp_data_write(extreme_pcm, 0xFFDEu, 0x8000u);
    galaxy::dsp_data_write(extreme_pcm, 0xFFDFu, 0x8001u);
    galaxy::dsp_data_write(extreme_pcm, 0xFFA0u, 0x8000u);
    galaxy::dsp_data_write(extreme_pcm, 0xFFA1u, 0x8000u);
    galaxy::dsp_data_write(extreme_pcm, 0xFFDBu, 0x8000u);
    galaxy::dsp_data_write(extreme_pcm, 0xFFDCu, 0x8000u);
    // 0x3fff8000 + 0x40000000 + 0x40000000 has low sixteen bits 0x8000.
    passed &= expect(galaxy::dsp_data_read(extreme_pcm, 0xFFDDu) == 0x8000u &&
                         extreme_pcm.ifx[galaxy::kDspIfxYn1] == 0x8000u &&
                         extreme_pcm.ifx[galaxy::kDspIfxYn2] == 0x8000u &&
                         galaxy::dsp_accelerator_current_address(extreme_pcm) == 0u,
                     "PCM extreme prediction wraps low16 without signed-sum overflow");

    AcceleratorMemoryProbe stop_probe{};
    galaxy::DspContext stop_context{};
    stop_context.hardware.validate_external_span = validate_external_span;
    stop_context.hardware.external_read_byte = read_external_byte;
    stop_context.hardware.accelerator_exception = record_accelerator_exception;
    stop_context.hardware_user = &stop_probe;
    stop_probe.context = &stop_context;
    stop_probe.memory[0] = 0x01;
    galaxy::dsp_data_write(stop_context, 0xffd1, 0x0000);
    galaxy::dsp_data_write(stop_context, 0xffda, 0x0000);
    galaxy::dsp_data_write(stop_context, 0xffd5, 0x0005);
    passed &= expect(galaxy::dsp_data_read(stop_context, 0xffdd) == 0x0000,
                     "DSP accelerator sample-end read returns the decoded sample");
    passed &= expect(stop_context.accelerator_reads_stopped,
                     "DSP accelerator sample-end stops future processed reads");
    passed &= expect(galaxy::dsp_data_read(stop_context, 0xffdd) == 0x0000,
                     "DSP accelerator stopped processed reads return zero");
    galaxy::dsp_data_write(stop_context, 0xffdc, 0x0000);
    passed &= expect(!stop_context.accelerator_reads_stopped,
                     "DSP accelerator YN2 write clears the stopped-read latch");
    passed &= expect(
        stop_probe.exception_count == 1 &&
            stop_probe.last_exception == galaxy::DspAcceleratorException::SampleReadEnd,
        "DSP accelerator sample end signals an accelerator exception");
    passed &= expect(
        stop_probe.current_address_at_exception == 0x00000005u &&
            stop_probe.reads_stopped_at_exception,
        "DSP accelerator sample-end callback observes committed current and stopped state");

    AcceleratorMemoryProbe raw_read_probe{};
    galaxy::DspContext raw_read_context{};
    raw_read_context.hardware.validate_external_span = validate_external_span;
    raw_read_context.hardware.external_read_byte = read_external_byte;
    raw_read_context.hardware.accelerator_exception = record_accelerator_exception;
    raw_read_context.hardware_user = &raw_read_probe;
    raw_read_probe.memory[2] = 0xab;
    galaxy::dsp_data_write(raw_read_context, 0xffd1, 0x0001);
    galaxy::dsp_data_write(raw_read_context, 0xffd4, 0x0000);
    galaxy::dsp_data_write(raw_read_context, 0xffd5, 0x0005);
    galaxy::dsp_data_write(raw_read_context, 0xffd6, 0x0000);
    galaxy::dsp_data_write(raw_read_context, 0xffd7, 0x0002);
    galaxy::dsp_data_write(raw_read_context, 0xffd8, 0x0000);
    galaxy::dsp_data_write(raw_read_context, 0xffd9, 0x0002);
    passed &= expect(galaxy::dsp_data_read(raw_read_context, 0xffd3) == 0x00ab,
                     "DSP accelerator raw reads use sample-size byte addressing");
    passed &= expect(galaxy::dsp_data_read(raw_read_context, 0xffd9) == 0x0005,
                     "DSP accelerator raw read wraps current address to start at end");
    passed &= expect(
        raw_read_probe.exception_count == 1 &&
            raw_read_probe.last_exception == galaxy::DspAcceleratorException::RawReadEnd,
        "DSP accelerator raw read end signals an accelerator exception");

    AcceleratorMemoryProbe raw_write_probe{};
    galaxy::DspContext raw_write_context{};
    raw_write_context.hardware.validate_external_span = validate_external_span;
    raw_write_context.hardware.external_write_byte = write_external_byte;
    raw_write_context.hardware.accelerator_exception = record_accelerator_exception;
    raw_write_context.hardware_user = &raw_write_probe;
    galaxy::dsp_data_write(raw_write_context, 0xffd8, 0x8000);
    galaxy::dsp_data_write(raw_write_context, 0xffd9, 0x0003);
    galaxy::dsp_data_write(raw_write_context, 0xffd3, 0xbeef);
    passed &= expect(raw_write_probe.memory[6] == 0xbe && raw_write_probe.memory[7] == 0xef,
                     "DSP accelerator raw writes store a big-endian 16-bit sample");
    passed &= expect(galaxy::dsp_data_read(raw_write_context, 0xffd8) == 0x8000 &&
                         galaxy::dsp_data_read(raw_write_context, 0xffd9) == 0x0004,
                     "DSP accelerator raw write advances the high-bit current address");
    passed &= expect(
        raw_write_probe.byte_writes == 2 && raw_write_probe.exception_count == 1 &&
            raw_write_probe.last_exception == galaxy::DspAcceleratorException::RawWriteEnd,
        "DSP accelerator raw write signals an accelerator exception");

    AcceleratorMemoryProbe raw_write_oob_probe{};
    galaxy::DspContext raw_write_oob_context{};
    raw_write_oob_context.pc = 0x0abcu;
    raw_write_oob_context.hardware.validate_external_span = validate_external_span;
    raw_write_oob_context.hardware.external_write_byte = write_external_byte;
    raw_write_oob_context.hardware.accelerator_exception =
        record_accelerator_exception;
    raw_write_oob_context.hardware_user = &raw_write_oob_probe;
    galaxy::dsp_data_write(raw_write_oob_context, 0xffd8u, 0x8000u);
    galaxy::dsp_data_write(raw_write_oob_context, 0xffd9u, 0x0080u);
    passed &= expect_hard_trap(
        [&] {
            galaxy::dsp_data_write(raw_write_oob_context, 0xffd3u, 0xbeefu);
        },
        "DSP accelerator raw write rejects a full-span ARAM boundary failure");
    passed &= expect(
        raw_write_oob_probe.byte_writes == 0u &&
            raw_write_oob_probe.exception_count == 0u &&
            galaxy::dsp_accelerator_current_address(raw_write_oob_context) ==
                0x80000080u,
        "DSP accelerator ARAM preflight preserves memory, current address, and terminal events");

    std::array<std::byte, 64> bridge_bytes{};
    galaxy::GuestMemoryV1 bridge_memory{};
    bridge_memory.fast_regions[0].size = static_cast<std::uint32_t>(bridge_bytes.size());
    bridge_memory.fast_regions[0].host_base = bridge_bytes.data();
    bridge_memory.fast_regions[8].size = static_cast<std::uint32_t>(bridge_bytes.size());
    bridge_memory.fast_regions[8].host_base = bridge_bytes.data();

    BridgeProbe bridge_probe{};
    galaxy::DspGuestMemoryBridge bridge{};
    bridge.memory = &bridge_memory;
    bridge.guest_pc = 0x80004000u;
    bridge.user = &bridge_probe;
    bridge.request_interrupt = record_bridge_interrupt;
    bridge.accelerator_exception = record_bridge_exception;

    galaxy::DspContext bridge_context{};
    galaxy::dsp_attach_guest_memory_bridge(bridge_context, bridge);
    bridge_bytes[4] = std::byte{0x12};
    bridge_bytes[5] = std::byte{0x34};
    galaxy::dsp_data_write(bridge_context, 0xffce, 0x8000);
    galaxy::dsp_data_write(bridge_context, 0xffcf, 0x0004);
    galaxy::dsp_data_write(bridge_context, 0xffcd, 0x0000);
    galaxy::dsp_data_write(bridge_context, 0xffc9, 0x0000);
    galaxy::dsp_data_write(bridge_context, 0xffcb, 0x0002);
    passed &= expect(
        bridge_context.dram[0] == 0x1234,
        "DSP guest-memory bridge feeds DMA from GuestMemoryV1 bytes");

    bridge_context.dram[1] = 0xabcd;
    galaxy::dsp_data_write(bridge_context, 0xffce, 0x8000);
    galaxy::dsp_data_write(bridge_context, 0xffcf, 0x0008);
    galaxy::dsp_data_write(bridge_context, 0xffcd, 0x0001);
    galaxy::dsp_data_write(bridge_context, 0xffc9, 0x0001);
    galaxy::dsp_data_write(bridge_context, 0xffcb, 0x0002);
    passed &= expect(
        bridge_bytes[8] == std::byte{0xab} && bridge_bytes[9] == std::byte{0xcd},
        "DSP guest-memory bridge writes DMA output through GuestMemoryV1 bytes");

    galaxy::dsp_data_write(bridge_context, 0xfffb, 1);
    passed &= expect(
        bridge_probe.interrupt_count == 1,
        "DSP guest-memory bridge forwards DIRQ to the host interrupt callback");

    bridge_bytes[16] = std::byte{0x55};
    bridge_bytes[17] = std::byte{0x66};
    galaxy::dsp_data_write(bridge_context, 0xffd1, 0x001a);
    galaxy::dsp_data_write(bridge_context, 0xffde, 0x0001);
    galaxy::dsp_data_write(bridge_context, 0xffd4, 0x0000);
    galaxy::dsp_data_write(bridge_context, 0xffd5, 0x0010);
    galaxy::dsp_data_write(bridge_context, 0xffd6, 0x0000);
    galaxy::dsp_data_write(bridge_context, 0xffd7, 0x0008);
    galaxy::dsp_data_write(bridge_context, 0xffd8, 0x0000);
    galaxy::dsp_data_write(bridge_context, 0xffd9, 0x0008);
    passed &= expect(
        galaxy::dsp_data_read(bridge_context, 0xffdd) == 0x5566,
        "DSP guest-memory bridge feeds accelerator samples from GuestMemoryV1");
    passed &= expect(
        bridge_probe.exception_count == 1 &&
            bridge_probe.last_exception == galaxy::DspAcceleratorException::SampleReadEnd,
        "DSP guest-memory bridge forwards accelerator exceptions to the host callback");

    galaxy::dsp_stack_push(mutable_context, 0, 0x1111);
    galaxy::dsp_stack_push(mutable_context, 0, 0x2222);
    passed &= expect(galaxy::dsp_stack_pop(mutable_context, 0) == 0x2222,
                     "DSP stack pop returns the current stack register");
    passed &= expect(galaxy::dsp_stack_pop(mutable_context, 0) == 0x1111,
                     "DSP stack pop restores the previous top word");
    passed &= expect(galaxy::dsp_stack_pop(mutable_context, 0) == 0x0000,
                     "DSP stack pop restores the initial zero word");

    return passed ? 0 : 1;
}
