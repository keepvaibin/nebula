#include "galaxy/generic_hid.h"

#include "galaxy/input_bindings.h"

#include <algorithm>
#include <cwchar>
#include <cwctype>

namespace galaxy::input::generic_hid {

namespace {

constexpr std::array<int, kMappedButtons> kButtonBits{
    static_cast<int>(pad::kSouthBit),         static_cast<int>(pad::kEastBit),
    static_cast<int>(pad::kWestBit),          static_cast<int>(pad::kNorthBit),
    static_cast<int>(pad::kLeftShoulderBit),  static_cast<int>(pad::kRightShoulderBit),
    static_cast<int>(pad::kGripLeftBit),      static_cast<int>(pad::kGripRightBit),
    static_cast<int>(pad::kBackBit),          static_cast<int>(pad::kStartBit),
    static_cast<int>(pad::kLeftStickBit),     static_cast<int>(pad::kRightStickBit),
    static_cast<int>(pad::kHomeBit),          static_cast<int>(pad::kCaptureBit),
    static_cast<int>(pad::kChatBit),          static_cast<int>(pad::kGripLeft2Bit),
    static_cast<int>(pad::kGripRight2Bit),    static_cast<int>(pad::kLeftPadBit),
    static_cast<int>(pad::kRightPadBit),
};

// -1..1 over the logical range (centre at its middle).
float centred(const ValueRange& range, std::uint32_t raw) noexcept {
    if (!range.present || range.max <= range.min) return 0.0f;
    const double span = static_cast<double>(range.max) - static_cast<double>(range.min);
    const double value = static_cast<double>(logical_value(range, raw)) - static_cast<double>(range.min);
    return std::clamp(static_cast<float>(value / span * 2.0 - 1.0), -1.0f, 1.0f);
}

// 0..1 over the logical range (rest at its minimum).
float pulled(const ValueRange& range, std::uint32_t raw) noexcept {
    if (!range.present || range.max <= range.min) return 0.0f;
    const double span = static_cast<double>(range.max) - static_cast<double>(range.min);
    const double value = static_cast<double>(logical_value(range, raw)) - static_cast<double>(range.min);
    return std::clamp(static_cast<float>(value / span), 0.0f, 1.0f);
}

bool has(const Layout& layout, Axis axis) noexcept {
    return layout.axes[static_cast<std::size_t>(axis)].present;
}

bool path_has(const wchar_t* path, const wchar_t* needle) noexcept {
    const std::size_t n = std::wcslen(needle);
    for (const wchar_t* p = path; *p != L'\0'; ++p) {
        std::size_t i = 0;
        while (i < n && p[i] != L'\0' && std::towlower(p[i]) == std::towlower(needle[i])) ++i;
        if (i == n) return true;
    }
    return false;
}

}  // namespace

bool claimed_elsewhere(std::uint16_t vendor, const wchar_t* device_path) noexcept {
    if (vendor == 0x045e || vendor == 0x057e || vendor == 0x054c || vendor == 0x28de) return true;
    return device_path != nullptr && path_has(device_path, L"ig_");
}

int button_bit(unsigned number) noexcept {
    return number >= 1u && number <= kMappedButtons ? kButtonBits[number - 1u] : -1;
}

std::int32_t logical_value(const ValueRange& range, std::uint32_t raw) noexcept {
    if (range.min < 0 && range.bits > 0u && range.bits < 32u) {
        const std::uint32_t sign = 1u << (range.bits - 1u);
        raw &= (sign << 1u) - 1u;
        return static_cast<std::int32_t>(raw ^ sign) - static_cast<std::int32_t>(sign);
    }
    return static_cast<std::int32_t>(raw);
}

void map_report(const Layout& layout, const Raw& raw, switch_hid::DecodedState& out) noexcept {
    out = {};
    out.normalized = true;
    for (unsigned number = 1; number <= kMappedButtons; ++number) {
        if ((raw.buttons & (1u << (number - 1u))) != 0u) {
            out.buttons |= 1u << static_cast<unsigned>(button_bit(number));
        }
    }
    const auto range = [&](Axis axis) -> const ValueRange& { return layout.axes[static_cast<std::size_t>(axis)]; };
    const auto value = [&](Axis axis) { return raw.axes[static_cast<std::size_t>(axis)]; };
    out.axes[static_cast<std::size_t>(PadAxis::LeftX)] = centred(range(Axis::X), value(Axis::X));
    out.axes[static_cast<std::size_t>(PadAxis::LeftY)] = -centred(range(Axis::Y), value(Axis::Y));
    const bool z_pair = has(layout, Axis::Z) && has(layout, Axis::Rz);
    const Axis right_x = z_pair ? Axis::Z : Axis::Rx;
    const Axis right_y = z_pair ? Axis::Rz : Axis::Ry;
    const Axis left_trigger = z_pair ? Axis::Rx : Axis::Z;
    const Axis right_trigger = z_pair ? Axis::Ry : Axis::Rz;
    out.axes[static_cast<std::size_t>(PadAxis::RightX)] = centred(range(right_x), value(right_x));
    out.axes[static_cast<std::size_t>(PadAxis::RightY)] = -centred(range(right_y), value(right_y));
    out.axes[static_cast<std::size_t>(PadAxis::LeftTrigger)] =
        has(layout, left_trigger) ? pulled(range(left_trigger), value(left_trigger))
                                  : pulled(range(Axis::Brake), value(Axis::Brake));
    out.axes[static_cast<std::size_t>(PadAxis::RightTrigger)] =
        has(layout, right_trigger) ? pulled(range(right_trigger), value(right_trigger))
                                   : pulled(range(Axis::Accelerator), value(Axis::Accelerator));
    // Hat: eight directions clockwise from north over the logical range (four
    // when it has four values); anything outside is the null (centred) state.
    if (layout.hat.present && layout.hat.max > layout.hat.min) {
        const std::int64_t at = static_cast<std::int64_t>(logical_value(layout.hat, raw.hat)) - layout.hat.min;
        const std::int64_t count = static_cast<std::int64_t>(layout.hat.max) - layout.hat.min + 1;
        if (at >= 0 && at < count && (count == 8 || count == 4)) {
            const unsigned direction = static_cast<unsigned>(count == 4 ? at * 2 : at);
            constexpr std::array<std::uint32_t, 8> kHat{
                pad::bit(pad::kDpadUpBit),
                pad::bit(pad::kDpadUpBit) | pad::bit(pad::kDpadRightBit),
                pad::bit(pad::kDpadRightBit),
                pad::bit(pad::kDpadDownBit) | pad::bit(pad::kDpadRightBit),
                pad::bit(pad::kDpadDownBit),
                pad::bit(pad::kDpadDownBit) | pad::bit(pad::kDpadLeftBit),
                pad::bit(pad::kDpadLeftBit),
                pad::bit(pad::kDpadUpBit) | pad::bit(pad::kDpadLeftBit),
            };
            out.buttons |= kHat[direction];
        }
    }
}

}  // namespace galaxy::input::generic_hid
