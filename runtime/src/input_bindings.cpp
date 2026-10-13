#include "galaxy/input_bindings.h"
#include "galaxy/rumble.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <clocale>
#include <locale.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace galaxy::input {

namespace {

constexpr std::array<const char*, kWiiTargetCount> kTargetIds{
    "a", "b", "one", "two", "plus", "minus", "home",
    "dpad_up", "dpad_down", "dpad_left", "dpad_right",
    "stick_up", "stick_down", "stick_left", "stick_right",
    "c", "z", "shake", "nunchuk_shake",
    "pointer_up", "pointer_down", "pointer_left", "pointer_right",
    "pointer_recenter", "jump_back",
};

constexpr std::array<const char*, kWiiTargetCount> kTargetLabels{
    "A", "B", "1", "2", "+", "-", "HOME",
    "D-PAD UP", "D-PAD DOWN", "D-PAD LEFT", "D-PAD RIGHT",
    "STICK UP", "STICK DOWN", "STICK LEFT", "STICK RIGHT",
    "C", "Z", "SHAKE", "NUNCHUK SHAKE",
    "POINTER UP", "POINTER DOWN", "POINTER LEFT", "POINTER RIGHT",
    "RECENTER", "A IN PLAY, B IN MENUS",
};

constexpr std::array<const char*, kControlSchemeCount> kSchemeIds{
    "keyboard", "xbox", "switch_pro", "switch_pro2"};
constexpr std::array<const char*, kControlSchemeCount> kSchemeLabels{
    "KEYBOARD", "XBOX", "SWITCH PRO", "PRO CONTROLLER 2"};

struct NamedKey {
    std::uint8_t vk;
    const char* name;
};

// Keys whose name is not a single letter or digit.
constexpr NamedKey kNamedKeys[] = {
    {VK_SPACE, "Space"}, {VK_RETURN, "Enter"}, {VK_TAB, "Tab"},
    {VK_BACK, "Backspace"}, {VK_ESCAPE, "Escape"},
    {VK_LSHIFT, "LShift"}, {VK_RSHIFT, "RShift"}, {VK_SHIFT, "Shift"},
    {VK_LCONTROL, "LCtrl"}, {VK_RCONTROL, "RCtrl"}, {VK_CONTROL, "Ctrl"},
    {VK_LMENU, "LAlt"}, {VK_RMENU, "RAlt"}, {VK_MENU, "Alt"},
    {VK_UP, "Up"}, {VK_DOWN, "Down"}, {VK_LEFT, "Left"}, {VK_RIGHT, "Right"},
    {VK_INSERT, "Insert"}, {VK_DELETE, "Delete"}, {VK_HOME, "Home"},
    {VK_END, "End"}, {VK_PRIOR, "PageUp"}, {VK_NEXT, "PageDown"},
    {VK_CAPITAL, "CapsLock"},
    {VK_OEM_MINUS, "Minus"}, {VK_OEM_PLUS, "Equals"},
    {VK_OEM_4, "LBracket"}, {VK_OEM_6, "RBracket"}, {VK_OEM_5, "Backslash"},
    {VK_OEM_1, "Semicolon"}, {VK_OEM_7, "Quote"}, {VK_OEM_COMMA, "Comma"},
    {VK_OEM_PERIOD, "Period"}, {VK_OEM_2, "Slash"}, {VK_OEM_3, "Grave"},
    {VK_ADD, "NumAdd"}, {VK_SUBTRACT, "NumSub"}, {VK_MULTIPLY, "NumMul"},
    {VK_DIVIDE, "NumDiv"}, {VK_DECIMAL, "NumDecimal"},
};

constexpr std::array<const char*, kMouseButtonCount> kMouseNames{
    "MouseLeft", "MouseRight", "MouseMiddle", "Mouse4", "Mouse5"};

constexpr std::array<const char*, pad::kButtonBitCount> kXboxButtonNames{
    "DpadUp", "DpadDown", "DpadLeft", "DpadRight", "Menu", "View", "LS", "RS",
    "LB", "RB", "Guide", "Share", "A", "B", "X", "Y", "GL", "GR", "C"};
constexpr std::array<const char*, pad::kButtonBitCount> kSwitchButtonNames{
    "DpadUp", "DpadDown", "DpadLeft", "DpadRight", "Plus", "Minus", "LS", "RS",
    "L", "R", "Home", "Capture", "B", "A", "Y", "X", "GL", "GR", "C"};

constexpr std::array<const char*, 4> kStickAxisNames{"LX", "LY", "RX", "RY"};

bool iequals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

std::string_view trim(std::string_view text) noexcept {
    const std::size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) return {};
    const std::size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1u);
}

const std::array<const char*, pad::kButtonBitCount>& button_names(ControlScheme scheme) noexcept {
    return scheme == ControlScheme::Xbox ? kXboxButtonNames : kSwitchButtonNames;
}

const char* trigger_name(ControlScheme scheme, PadAxis axis) noexcept {
    const bool left = axis == PadAxis::LeftTrigger;
    if (scheme == ControlScheme::Xbox) return left ? "LT" : "RT";
    return left ? "ZL" : "ZR";
}

std::string format_key(std::uint8_t vk) {
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) {
        return std::string(1, static_cast<char>(vk));
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
        return "Num" + std::to_string(vk - VK_NUMPAD0);
    }
    if (vk >= VK_F1 && vk <= VK_F24) {
        return "F" + std::to_string(vk - VK_F1 + 1);
    }
    for (const NamedKey& key : kNamedKeys) {
        if (key.vk == vk) return key.name;
    }
    char text[16]{};
    std::snprintf(text, sizeof(text), "VK%02X", static_cast<unsigned>(vk));
    return text;
}

std::optional<std::uint8_t> parse_key(std::string_view text) noexcept {
    if (text.size() == 1u) {
        const char ch = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
        if ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z')) {
            return static_cast<std::uint8_t>(ch);
        }
    }
    for (const NamedKey& key : kNamedKeys) {
        if (iequals(text, key.name)) return key.vk;
    }
    const auto parse_number = [](std::string_view digits, int base, int& out) {
        if (digits.empty() || digits.size() > 2u) return false;
        int value = 0;
        for (char ch : digits) {
            int digit = -1;
            if (ch >= '0' && ch <= '9') digit = ch - '0';
            else if (base == 16 && ch >= 'a' && ch <= 'f') digit = ch - 'a' + 10;
            else if (base == 16 && ch >= 'A' && ch <= 'F') digit = ch - 'A' + 10;
            if (digit < 0) return false;
            value = value * base + digit;
        }
        out = value;
        return true;
    };
    int value = 0;
    if (text.size() == 4u && iequals(text.substr(0, 3), "Num") &&
        parse_number(text.substr(3), 10, value)) {
        return static_cast<std::uint8_t>(VK_NUMPAD0 + value);
    }
    if (text.size() >= 2u && (text[0] == 'F' || text[0] == 'f') &&
        parse_number(text.substr(1), 10, value) && value >= 1 && value <= 24) {
        return static_cast<std::uint8_t>(VK_F1 + value - 1);
    }
    if (text.size() == 4u && iequals(text.substr(0, 2), "VK") &&
        parse_number(text.substr(2), 16, value) && value > 0) {
        return static_cast<std::uint8_t>(value);
    }
    return std::nullopt;
}

InputSource key(std::uint8_t vk) noexcept { return {SourceKind::Key, vk}; }
InputSource mouse(MouseButton b) noexcept {
    return {SourceKind::Mouse, static_cast<std::uint8_t>(b)};
}
InputSource button(unsigned bit) noexcept {
    return {SourceKind::Button, static_cast<std::uint8_t>(bit)};
}
InputSource axis_pos(PadAxis axis) noexcept {
    return {SourceKind::AxisPositive, static_cast<std::uint8_t>(axis)};
}
InputSource axis_neg(PadAxis axis) noexcept {
    return {SourceKind::AxisNegative, static_cast<std::uint8_t>(axis)};
}

void set(BindingTable& table, WiiTarget target, std::initializer_list<InputSource> sources) {
    TargetBindings bindings{};
    std::size_t i = 0;
    for (InputSource source : sources) {
        if (i < bindings.size()) bindings[i++] = source;
    }
    table[target] = bindings;
}

bool parse_float_c(std::string_view text, float& out) noexcept {
    const std::string copy(trim(text));
    if (copy.empty()) return false;
    _locale_t locale = _create_locale(LC_NUMERIC, "C");
    if (locale == nullptr) return false;
    char* end = nullptr;
    errno = 0;
    const float value = _strtof_l(copy.c_str(), &end, locale);
    _free_locale(locale);
    if (end == copy.c_str() || *end != '\0' || errno == ERANGE || !std::isfinite(value)) {
        return false;
    }
    out = value;
    return true;
}

bool parse_bool(std::string_view text, bool& out) noexcept {
    text = trim(text);
    if (text == "1" || iequals(text, "true") || iequals(text, "on")) { out = true; return true; }
    if (text == "0" || iequals(text, "false") || iequals(text, "off")) { out = false; return true; }
    return false;
}

std::string format_float_c(float value) {
    char text[48]{};
    _locale_t locale = _create_locale(LC_NUMERIC, "C");
    if (locale != nullptr) {
        _snprintf_s_l(text, sizeof(text), _TRUNCATE, "%.9g", locale, static_cast<double>(value));
        _free_locale(locale);
    }
    return text;
}

constexpr const char* kMotionNames[] = {"off", "remote", "nunchuk"};
static_assert(SchemeTuning{}.rumble == rumble::kDefaultStrength);

}  // namespace

BindingTable legacy_binding_table(ControlScheme scheme) {
    BindingTable table{};
    using T = WiiTarget;
    if (scheme == ControlScheme::Keyboard) {
        set(table, T::A, {key(VK_SPACE), key(VK_RETURN), mouse(MouseButton::Left)});
        set(table, T::B, {key(VK_LSHIFT), key(VK_RSHIFT), mouse(MouseButton::Right)});
        set(table, T::One, {key('1'), key(VK_NUMPAD1)});
        set(table, T::Two, {key('2'), key(VK_NUMPAD2)});
        set(table, T::Plus, {key(VK_OEM_PLUS), key('P')});
        set(table, T::Minus, {key(VK_OEM_MINUS), key('M')});
        set(table, T::Home, {key('H')});
        set(table, T::DpadUp, {key(VK_UP)});
        set(table, T::DpadDown, {key(VK_DOWN)});
        set(table, T::DpadLeft, {key(VK_LEFT)});
        set(table, T::DpadRight, {key(VK_RIGHT)});
        set(table, T::StickUp, {key('W')});
        set(table, T::StickDown, {key('S')});
        set(table, T::StickLeft, {key('A')});
        set(table, T::StickRight, {key('D')});
        set(table, T::C, {key('C')});
        set(table, T::Z, {key('Z'), key(VK_LCONTROL), key(VK_RCONTROL)});
        set(table, T::Shake, {key('Q'), key('E')});
        set(table, T::PointerUp, {key('I')});
        set(table, T::PointerDown, {key('K')});
        set(table, T::PointerLeft, {key('J')});
        set(table, T::PointerRight, {key('L')});
        set(table, T::PointerRecenter, {key('R')});
        return table;
    }
    set(table, T::DpadUp, {button(pad::kDpadUpBit)});
    set(table, T::DpadDown, {button(pad::kDpadDownBit)});
    set(table, T::DpadLeft, {button(pad::kDpadLeftBit)});
    set(table, T::DpadRight, {button(pad::kDpadRightBit)});
    set(table, T::Plus, {button(pad::kStartBit)});
    set(table, T::Minus, {button(pad::kBackBit)});
    set(table, T::StickUp, {axis_pos(PadAxis::LeftY)});
    set(table, T::StickDown, {axis_neg(PadAxis::LeftY)});
    set(table, T::StickLeft, {axis_neg(PadAxis::LeftX)});
    set(table, T::StickRight, {axis_pos(PadAxis::LeftX)});
    set(table, T::C, {button(pad::kLeftStickBit)});
    set(table, T::Z, {button(pad::kLeftShoulderBit), axis_pos(PadAxis::LeftTrigger)});
    set(table, T::PointerUp, {axis_pos(PadAxis::RightY)});
    set(table, T::PointerDown, {axis_neg(PadAxis::RightY)});
    set(table, T::PointerLeft, {axis_neg(PadAxis::RightX)});
    set(table, T::PointerRight, {axis_pos(PadAxis::RightX)});
    set(table, T::PointerRecenter, {button(pad::kRightStickBit)});
    set(table, T::Shake, {button(pad::kWestBit), button(pad::kNorthBit),
                          button(pad::kRightShoulderBit)});
    if (scheme == ControlScheme::Xbox) {
        set(table, T::A, {button(pad::kSouthBit)});
        set(table, T::B, {button(pad::kEastBit), axis_pos(PadAxis::RightTrigger)});
    } else {
        set(table, T::A, {button(pad::kEastBit)});
        set(table, T::B, {button(pad::kSouthBit), axis_pos(PadAxis::RightTrigger)});
        set(table, T::Home, {button(pad::kHomeBit)});
    }
    return table;
}

// The defaults shipped in controls version 2 (Native29), for migration.
BindingTable version2_binding_table(ControlScheme scheme) {
    BindingTable table = legacy_binding_table(scheme);
    if (scheme == ControlScheme::Keyboard) {
        return table;
    }
    using T = WiiTarget;
    const bool xbox = scheme == ControlScheme::Xbox;
    set(table, T::A, {button(xbox ? pad::kSouthBit : pad::kEastBit)});
    set(table, T::JumpBack, {button(xbox ? pad::kEastBit : pad::kSouthBit)});
    set(table, T::B, {axis_pos(PadAxis::RightTrigger)});
    set(table, T::Z, {axis_pos(PadAxis::LeftTrigger)});
    set(table, T::C, {button(pad::kLeftShoulderBit), button(pad::kLeftStickBit)});
    if (xbox) {
        set(table, T::Shake, {button(pad::kWestBit), button(pad::kNorthBit),
                              button(pad::kRightShoulderBit)});
        set(table, T::PointerRecenter, {button(pad::kRightStickBit)});
    } else {
        set(table, T::Shake, {button(pad::kWestBit), button(pad::kNorthBit)});
        set(table, T::PointerRecenter, {button(pad::kRightShoulderBit)});
        set(table, T::DpadUp, {button(pad::kDpadUpBit), axis_pos(PadAxis::RightY)});
        set(table, T::DpadDown, {button(pad::kDpadDownBit), axis_neg(PadAxis::RightY)});
        set(table, T::DpadLeft, {button(pad::kDpadLeftBit), axis_neg(PadAxis::RightX)});
        set(table, T::DpadRight, {button(pad::kDpadRightBit), axis_pos(PadAxis::RightX)});
    }
    return table;
}

// The defaults shipped in controls version 3 (Native29b), for migration.
BindingTable version3_binding_table(ControlScheme scheme) {
    BindingTable table = version2_binding_table(scheme);
    if (scheme == ControlScheme::Keyboard) {
        return table;
    }
    using T = WiiTarget;
    set(table, T::B, {button(pad::kLeftShoulderBit)});
    set(table, T::Z, {axis_pos(PadAxis::LeftTrigger), axis_pos(PadAxis::RightTrigger)});
    set(table, T::C, {button(pad::kLeftStickBit)});
    return table;
}

// The Super Mario Galaxy on Switch shape with the player's choices: A or B
// jump (B backs out of menus), L/LB Star Bits, ZL/LT crouch, X/Y/ZR (Xbox
// X/Y/RB/RT) or a shake spin, left-stick click resets the camera, R /
// right-stick click recenters (held: pinned at the centre), the gyro points
// and the right stick turns the camera (the pointer when the gyro is off;
// Xbox: the pointer).
BindingTable default_binding_table(ControlScheme scheme) {
    BindingTable table = version3_binding_table(scheme);
    if (scheme == ControlScheme::Keyboard) {
        return table;
    }
    using T = WiiTarget;
    set(table, T::Z, {axis_pos(PadAxis::LeftTrigger)});
    if (scheme == ControlScheme::Xbox) {
        set(table, T::Shake, {button(pad::kWestBit), button(pad::kNorthBit),
                              button(pad::kRightShoulderBit), axis_pos(PadAxis::RightTrigger)});
    } else {
        set(table, T::Shake, {button(pad::kWestBit), button(pad::kNorthBit),
                              axis_pos(PadAxis::RightTrigger)});
    }
    return table;
}

SchemeTuning legacy_scheme_tuning(ControlScheme scheme) {
    SchemeTuning tuning{};
    switch (scheme) {
    case ControlScheme::Keyboard:
        tuning.pointer_speed = 1.6f;
        break;
    case ControlScheme::Xbox:
        // XINPUT_GAMEPAD_LEFT/RIGHT_THUMB_DEADZONE.
        tuning.stick_deadzone = 7849.0f / 32767.0f;
        tuning.pointer_deadzone = 8689.0f / 32767.0f;
        tuning.pointer_speed = 2.4f;
        tuning.pointer_hide_seconds = 4.0f;
        break;
    case ControlScheme::SwitchPro:
    case ControlScheme::SwitchPro2:
        tuning.stick_deadzone = 0.12f;
        tuning.pointer_deadzone = 0.12f;
        tuning.pointer_speed = 2.4f;
        tuning.motion_shake = MotionShake::Remote;
        tuning.shake_sensitivity = 3;
        tuning.pointer_hide_seconds = 4.0f;
        break;
    }
    return tuning;
}

SchemeTuning default_scheme_tuning(ControlScheme scheme) {
    SchemeTuning tuning = legacy_scheme_tuning(scheme);
    if (scheme_has_motion(scheme)) {
        tuning.gyro_pointer = true;
    }
    if (scheme_is_gamepad(scheme)) {
        // Still-pointer hiding (pointer_control.h kDefaultPointerHide*).
        tuning.pointer_hide_seconds = 2.0f;
        tuning.pointer_hide_radius = 0.12f;
    }
    return tuning;
}

unsigned migrate_legacy_controls(ControlsConfig& config, unsigned version) noexcept {
    unsigned changed = 0u;
    for (std::size_t s = 0; s < kControlSchemeCount; ++s) {
        const auto scheme = static_cast<ControlScheme>(s);
        const BindingTable legacy = legacy_binding_table(scheme);
        const BindingTable version2 = version2_binding_table(scheme);
        const BindingTable version3 = version3_binding_table(scheme);
        const BindingTable current = default_binding_table(scheme);
        for (std::size_t t = 0; t < kWiiTargetCount; ++t) {
            TargetBindings& bindings = config.tables[s].targets[t];
            const bool old_default = bindings == legacy.targets[t] || bindings == version2.targets[t] ||
                                     bindings == version3.targets[t];
            if (old_default && bindings != current.targets[t]) {
                bindings = current.targets[t];
                ++changed;
            }
        }
        const SchemeTuning legacy_tuning = legacy_scheme_tuning(scheme);
        const SchemeTuning current_tuning = default_scheme_tuning(scheme);
        SchemeTuning& tuning = config.tuning[s];
        if (version < 2u && tuning.gyro_pointer == legacy_tuning.gyro_pointer &&
            legacy_tuning.gyro_pointer != current_tuning.gyro_pointer) {
            tuning.gyro_pointer = current_tuning.gyro_pointer;
            ++changed;
        }
        if (version < 4u && scheme_is_gamepad(scheme)) {
            if (tuning.pointer_hide_seconds == kVersion3PointerHideSeconds) {
                tuning.pointer_hide_seconds = current_tuning.pointer_hide_seconds;
                ++changed;
            }
            if (tuning.pointer_hide_radius == kVersion3PointerHideRadius) {
                tuning.pointer_hide_radius = current_tuning.pointer_hide_radius;
                ++changed;
            }
        }
    }
    return changed;
}

ControlsConfig default_controls_config() {
    ControlsConfig config{};
    for (std::size_t s = 0; s < kControlSchemeCount; ++s) {
        config.tables[s] = default_binding_table(static_cast<ControlScheme>(s));
        config.tuning[s] = default_scheme_tuning(static_cast<ControlScheme>(s));
    }
    return config;
}

const char* control_scheme_id(ControlScheme scheme) noexcept {
    return kSchemeIds[static_cast<std::size_t>(scheme) % kControlSchemeCount];
}
const char* control_scheme_label(ControlScheme scheme) noexcept {
    return kSchemeLabels[static_cast<std::size_t>(scheme) % kControlSchemeCount];
}
const char* wii_target_id(WiiTarget target) noexcept {
    return kTargetIds[static_cast<std::size_t>(target) % kWiiTargetCount];
}
const char* wii_target_label(WiiTarget target) noexcept {
    return kTargetLabels[static_cast<std::size_t>(target) % kWiiTargetCount];
}

std::optional<ControlScheme> parse_control_scheme(std::string_view id) noexcept {
    for (std::size_t i = 0; i < kSchemeIds.size(); ++i) {
        if (iequals(id, kSchemeIds[i])) return static_cast<ControlScheme>(i);
    }
    return std::nullopt;
}

std::optional<WiiTarget> parse_wii_target(std::string_view id) noexcept {
    for (std::size_t i = 0; i < kTargetIds.size(); ++i) {
        if (iequals(id, kTargetIds[i])) return static_cast<WiiTarget>(i);
    }
    return std::nullopt;
}

std::string format_input_source(ControlScheme scheme, InputSource source) {
    switch (source.kind) {
    case SourceKind::None:
        return "None";
    case SourceKind::Key:
        return format_key(source.code);
    case SourceKind::Mouse:
        return source.code < kMouseNames.size() ? kMouseNames[source.code] : "None";
    case SourceKind::Button:
        return source.code < pad::kButtonBitCount ? button_names(scheme)[source.code] : "None";
    case SourceKind::AxisPositive:
    case SourceKind::AxisNegative: {
        const auto axis = static_cast<PadAxis>(source.code);
        if (axis == PadAxis::LeftTrigger || axis == PadAxis::RightTrigger) {
            return trigger_name(scheme, axis);
        }
        if (source.code >= kStickAxisNames.size()) return "None";
        return std::string(kStickAxisNames[source.code]) +
               (source.kind == SourceKind::AxisPositive ? "+" : "-");
    }
    }
    return "None";
}

std::optional<InputSource> parse_input_source(ControlScheme scheme, std::string_view text) {
    text = trim(text);
    if (text.empty()) return std::nullopt;
    if (scheme == ControlScheme::Keyboard) {
        for (std::size_t i = 0; i < kMouseNames.size(); ++i) {
            if (iequals(text, kMouseNames[i])) return mouse(static_cast<MouseButton>(i));
        }
        if (const auto vk = parse_key(text)) return key(*vk);
        return std::nullopt;
    }
    const auto& names = button_names(scheme);
    for (unsigned i = 0; i < names.size(); ++i) {
        if (iequals(text, names[i])) return button(i);
    }
    for (PadAxis axis : {PadAxis::LeftTrigger, PadAxis::RightTrigger}) {
        if (iequals(text, trigger_name(scheme, axis))) return axis_pos(axis);
    }
    if (text.size() == 3u && (text[2] == '+' || text[2] == '-')) {
        for (std::size_t i = 0; i < kStickAxisNames.size(); ++i) {
            if (iequals(text.substr(0, 2), kStickAxisNames[i])) {
                return text[2] == '+' ? axis_pos(static_cast<PadAxis>(i))
                                      : axis_neg(static_cast<PadAxis>(i));
            }
        }
    }
    return std::nullopt;
}

std::string format_target_bindings(ControlScheme scheme, const TargetBindings& bindings) {
    std::string text;
    for (const InputSource& source : bindings) {
        if (source.empty()) continue;
        if (!text.empty()) text += ", ";
        text += format_input_source(scheme, source);
    }
    return text.empty() ? std::string("None") : text;
}

bool parse_target_bindings(ControlScheme scheme, std::string_view text, TargetBindings& bindings) {
    TargetBindings parsed{};
    std::size_t count = 0;
    text = trim(text);
    if (text.empty() || iequals(text, "None")) {
        bindings = parsed;
        return true;
    }
    while (!text.empty()) {
        const std::size_t comma = text.find(',');
        const std::string_view token = trim(text.substr(0, comma));
        text = comma == std::string_view::npos ? std::string_view{} : text.substr(comma + 1u);
        if (token.empty()) continue;
        const auto source = parse_input_source(scheme, token);
        if (!source) return false;
        if (std::find(parsed.begin(), parsed.begin() + static_cast<std::ptrdiff_t>(count), *source) !=
            parsed.begin() + static_cast<std::ptrdiff_t>(count)) {
            continue;
        }
        if (count == parsed.size()) return false;
        parsed[count++] = *source;
    }
    bindings = parsed;
    return true;
}

bool binding_source_allowed(ControlScheme scheme, WiiTarget target, InputSource source) noexcept {
    switch (source.kind) {
    case SourceKind::None:
        return false;
    case SourceKind::Key:
        return scheme == ControlScheme::Keyboard && source.code != 0u;
    case SourceKind::Mouse:
        return scheme == ControlScheme::Keyboard && source.code < kMouseButtonCount &&
               !wii_target_is_analog(target);
    case SourceKind::Button:
        if (scheme == ControlScheme::Keyboard || source.code >= pad::kButtonBitCount) return false;
        if (scheme == ControlScheme::Xbox &&
            (source.code == pad::kHomeBit || source.code == pad::kCaptureBit ||
             source.code >= pad::kGripLeftBit)) {
            return false;
        }
        if (scheme == ControlScheme::SwitchPro && source.code >= pad::kGripLeftBit) return false;
        return true;
    case SourceKind::AxisPositive:
        return scheme != ControlScheme::Keyboard && source.code < kPadAxisCount;
    case SourceKind::AxisNegative:
        return scheme != ControlScheme::Keyboard &&
               source.code <= static_cast<std::uint8_t>(PadAxis::RightY);
    }
    return false;
}

void add_binding(TargetBindings& bindings, InputSource source) noexcept {
    if (source.empty()) return;
    for (const InputSource& existing : bindings) {
        if (existing == source) return;
    }
    for (InputSource& slot : bindings) {
        if (slot.empty()) {
            slot = source;
            return;
        }
    }
    bindings.back() = source;
}

bool apply_controls_setting(ControlsConfig& config, std::string_view key_text, std::string_view value) {
    const bool bind = key_text.starts_with("bind.");
    const bool tune = key_text.starts_with("tune.");
    if (!bind && !tune) return false;
    key_text.remove_prefix(5u);
    const std::size_t dot = key_text.find('.');
    if (dot == std::string_view::npos) return true;
    const auto scheme = parse_control_scheme(key_text.substr(0, dot));
    const std::string_view name = key_text.substr(dot + 1u);
    if (!scheme) return true;
    if (bind) {
        const auto target = parse_wii_target(name);
        if (!target) return true;
        TargetBindings bindings{};
        if (parse_target_bindings(*scheme, value, bindings)) {
            TargetBindings allowed{};
            for (const InputSource& source : bindings) {
                if (binding_source_allowed(*scheme, *target, source)) add_binding(allowed, source);
            }
            config.table(*scheme)[*target] = allowed;
        }
        return true;
    }
    SchemeTuning& tuning = config.tune(*scheme);
    float number = 0.0f;
    bool flag = false;
    if (name == "stick_deadzone" && parse_float_c(value, number)) {
        tuning.stick_deadzone = number;
    } else if (name == "pointer_deadzone" && parse_float_c(value, number)) {
        tuning.pointer_deadzone = number;
    } else if (name == "pointer_speed" && parse_float_c(value, number)) {
        tuning.pointer_speed = number;
    } else if (name == "invert_pointer_y" && parse_bool(value, flag)) {
        tuning.invert_pointer_y = flag;
    } else if (name == "gyro_pointer" && parse_bool(value, flag)) {
        tuning.gyro_pointer = flag;
    } else if (name == "gyro_sensitivity" && parse_float_c(value, number)) {
        tuning.gyro_sensitivity = number;
    } else if (name == "pointer_hide_seconds" && parse_float_c(value, number)) {
        tuning.pointer_hide_seconds = number;
    } else if (name == "pointer_hide_radius" && parse_float_c(value, number)) {
        tuning.pointer_hide_radius = number;
    } else if (name == "shake_sensitivity" && parse_float_c(value, number)) {
        tuning.shake_sensitivity = static_cast<std::uint8_t>(
            std::clamp(static_cast<int>(std::lround(number)), 1, int{kShakeSensitivitySteps}));
    } else if (name == "motion") {
        const std::string_view text = trim(value);
        for (std::size_t i = 0; i < std::size(kMotionNames); ++i) {
            if (iequals(text, kMotionNames[i])) tuning.motion_shake = static_cast<MotionShake>(i);
        }
    } else if (name == "rumble" && parse_float_c(value, number)) {
        tuning.rumble = rumble::snap_strength(number);
    }
    config = sanitize_controls_config(config);
    return true;
}

ControlsConfig sanitize_controls_config(ControlsConfig config) noexcept {
    for (std::size_t s = 0; s < kControlSchemeCount; ++s) {
        SchemeTuning& tuning = config.tuning[s];
        const auto clamp_or = [](float value, float lo, float hi, float fallback) {
            return std::isfinite(value) ? std::clamp(value, lo, hi) : fallback;
        };
        const SchemeTuning defaults = default_scheme_tuning(static_cast<ControlScheme>(s));
        tuning.stick_deadzone = clamp_or(tuning.stick_deadzone, 0.0f, 0.9f, defaults.stick_deadzone);
        tuning.pointer_deadzone = clamp_or(tuning.pointer_deadzone, 0.0f, 0.9f, defaults.pointer_deadzone);
        tuning.pointer_speed = clamp_or(tuning.pointer_speed, 0.1f, 10.0f, defaults.pointer_speed);
        tuning.gyro_sensitivity = clamp_or(tuning.gyro_sensitivity, 0.2f, 5.0f, defaults.gyro_sensitivity);
        tuning.pointer_hide_seconds = clamp_or(tuning.pointer_hide_seconds, 0.0f, 60.0f, defaults.pointer_hide_seconds);
        tuning.pointer_hide_radius = clamp_or(tuning.pointer_hide_radius, 0.0f, 0.5f, defaults.pointer_hide_radius);
        tuning.shake_sensitivity = static_cast<std::uint8_t>(
            std::clamp<int>(tuning.shake_sensitivity, 1, kShakeSensitivitySteps));
        tuning.rumble = rumble::snap_strength(static_cast<float>(tuning.rumble));
        if (static_cast<unsigned>(tuning.motion_shake) > 2u) tuning.motion_shake = MotionShake::Off;
        if (!scheme_has_motion(static_cast<ControlScheme>(s))) {
            tuning.motion_shake = MotionShake::Off;
            tuning.gyro_pointer = false;
            tuning.gyro_sensitivity = defaults.gyro_sensitivity;
        }
    }
    return config;
}

std::string format_controls_settings(const ControlsConfig& config) {
    std::string text;
    for (std::size_t s = 0; s < kControlSchemeCount; ++s) {
        const auto scheme = static_cast<ControlScheme>(s);
        const std::string prefix = std::string(control_scheme_id(scheme)) + ".";
        for (std::size_t t = 0; t < kWiiTargetCount; ++t) {
            text += "bind." + prefix + kTargetIds[t] + "=" +
                    format_target_bindings(scheme, config.tables[s].targets[t]) + "\n";
        }
        const SchemeTuning& tuning = config.tuning[s];
        if (scheme_is_gamepad(scheme)) {
            text += "tune." + prefix + "stick_deadzone=" + format_float_c(tuning.stick_deadzone) + "\n";
            text += "tune." + prefix + "pointer_deadzone=" + format_float_c(tuning.pointer_deadzone) + "\n";
            text += "tune." + prefix + "pointer_hide_seconds=" + format_float_c(tuning.pointer_hide_seconds) + "\n";
            text += "tune." + prefix + "pointer_hide_radius=" + format_float_c(tuning.pointer_hide_radius) + "\n";
            text += "tune." + prefix + "rumble=" + std::to_string(tuning.rumble) + "\n";
        }
        text += "tune." + prefix + "pointer_speed=" + format_float_c(tuning.pointer_speed) + "\n";
        text += "tune." + prefix + "invert_pointer_y=" + (tuning.invert_pointer_y ? "1" : "0") + "\n";
        if (scheme_has_motion(scheme)) {
            text += "tune." + prefix + "motion=" +
                    kMotionNames[static_cast<std::size_t>(tuning.motion_shake) % 3u] + "\n";
            text += "tune." + prefix + "shake_sensitivity=" +
                    std::to_string(tuning.shake_sensitivity) + "\n";
            text += "tune." + prefix + "gyro_pointer=" + (tuning.gyro_pointer ? "1" : "0") + "\n";
            text += "tune." + prefix + "gyro_sensitivity=" + format_float_c(tuning.gyro_sensitivity) + "\n";
        }
    }
    return text;
}

namespace {

float source_value_keyboard(InputSource source, const KeyboardMouseLevels& levels) noexcept {
    switch (source.kind) {
    case SourceKind::Key:
        return levels.key(source.code) ? 1.0f : 0.0f;
    case SourceKind::Mouse:
        return source.code < kMouseButtonCount &&
                       levels.mouse_button(static_cast<MouseButton>(source.code))
                   ? 1.0f : 0.0f;
    default:
        return 0.0f;
    }
}

float source_value_pad(InputSource source, const PadSample& sample) noexcept {
    switch (source.kind) {
    case SourceKind::Button:
        return source.code < 32u && ((sample.buttons >> source.code) & 1u) != 0u ? 1.0f : 0.0f;
    case SourceKind::AxisPositive:
        return source.code < kPadAxisCount ? std::max(sample.axes[source.code], 0.0f) : 0.0f;
    case SourceKind::AxisNegative:
        return source.code < kPadAxisCount ? std::max(-sample.axes[source.code], 0.0f) : 0.0f;
    default:
        return 0.0f;
    }
}

bool is_trigger_source(InputSource source) noexcept {
    return source.kind == SourceKind::AxisPositive &&
           (source.code == static_cast<std::uint8_t>(PadAxis::LeftTrigger) ||
            source.code == static_cast<std::uint8_t>(PadAxis::RightTrigger));
}

bool is_stick_source(InputSource source) noexcept {
    return (source.kind == SourceKind::AxisPositive || source.kind == SourceKind::AxisNegative) &&
           source.code <= static_cast<std::uint8_t>(PadAxis::RightY);
}

bool is_pointer_target(WiiTarget target) noexcept {
    return target >= WiiTarget::PointerUp && target <= WiiTarget::PointerRight;
}

// Bit per stick axis (code) bound to a pointer direction / to anything else.
struct StickRoles {
    std::uint8_t pointer = 0;
    std::uint8_t other = 0;
};

StickRoles stick_roles(const BindingTable& table) noexcept {
    StickRoles roles{};
    for (std::size_t t = 0; t < kWiiTargetCount; ++t) {
        for (const InputSource& source : table.targets[t]) {
            if (!is_stick_source(source)) continue;
            const auto bit = static_cast<std::uint8_t>(1u << source.code);
            (is_pointer_target(static_cast<WiiTarget>(t)) ? roles.pointer : roles.other) |= bit;
        }
    }
    return roles;
}

template <typename ValueOf, typename PressedOf, typename Usable>
LogicalInput evaluate(const BindingTable& table, ValueOf&& value_of, PressedOf&& pressed_of,
                      Usable&& usable) noexcept {
    LogicalInput out{};
    std::array<float, kWiiTargetCount> analog{};
    for (std::size_t t = 0; t < kWiiTargetCount; ++t) {
        const auto target = static_cast<WiiTarget>(t);
        const bool is_analog = wii_target_is_analog(target);
        float magnitude = 0.0f;
        bool pressed = false;
        for (const InputSource& source : table.targets[t]) {
            if (source.empty() || !usable(target, source)) continue;
            if (is_analog) {
                magnitude = std::max(magnitude, std::min(value_of(source), 1.0f));
            } else if (pressed_of(target, source)) {
                pressed = true;
            }
        }
        if (is_analog) {
            analog[t] = magnitude;
            pressed = magnitude >= kStickDirectionPress;
        }
        if (pressed) out.pressed |= wii_target_bit(target);
    }
    const auto at = [&analog](WiiTarget t) { return analog[static_cast<std::size_t>(t)]; };
    out.stick_x = at(WiiTarget::StickRight) - at(WiiTarget::StickLeft);
    out.stick_y = at(WiiTarget::StickUp) - at(WiiTarget::StickDown);
    out.pointer_x = at(WiiTarget::PointerRight) - at(WiiTarget::PointerLeft);
    out.pointer_y = at(WiiTarget::PointerUp) - at(WiiTarget::PointerDown);
    return out;
}

}  // namespace

LogicalInput evaluate_keyboard_bindings(const BindingTable& table, const KeyboardMouseLevels& levels) noexcept {
    return evaluate(
        table,
        [&levels](InputSource s) { return source_value_keyboard(s, levels); },
        [&levels](WiiTarget, InputSource s) { return source_value_keyboard(s, levels) >= 0.5f; },
        [](WiiTarget, InputSource) { return true; });
}

LogicalInput evaluate_pad_bindings(const BindingTable& table, const PadSample& sample,
                                   float trigger_threshold) noexcept {
    return evaluate_pad_bindings(table, sample, trigger_threshold, 0u, false);
}

LogicalInput evaluate_pad_bindings(const BindingTable& table, const PadSample& sample,
                                   float trigger_threshold, std::uint32_t previous_pressed,
                                   bool gyro_pointer) noexcept {
    if (!sample.connected) return {};
    const StickRoles roles = stick_roles(table);
    return evaluate(
        table,
        [&sample](InputSource s) { return source_value_pad(s, sample); },
        [&sample, trigger_threshold, previous_pressed](WiiTarget target, InputSource s) {
            const float value = source_value_pad(s, sample);
            if (is_trigger_source(s)) return value >= trigger_threshold && value > 0.0f;
            const bool was_pressed = (previous_pressed & wii_target_bit(target)) != 0u;
            return value >= kStickDirectionPress ||
                   (was_pressed && is_stick_source(s) && value >= kStickDirectionRelease);
        },
        [&roles, gyro_pointer](WiiTarget target, InputSource s) {
            if (!is_stick_source(s)) return true;
            const auto bit = static_cast<std::uint8_t>(1u << s.code);
            if ((roles.pointer & roles.other & bit) == 0u) return true;
            return is_pointer_target(target) ? !gyro_pointer : gyro_pointer;
        });
}

LogicalInput merge_logical_input(const LogicalInput& a, const LogicalInput& b) noexcept {
    const auto larger = [](float x, float y) { return std::fabs(y) > std::fabs(x) ? y : x; };
    LogicalInput out{};
    out.pressed = a.pressed | b.pressed;
    out.stick_x = larger(a.stick_x, b.stick_x);
    out.stick_y = larger(a.stick_y, b.stick_y);
    out.pointer_x = larger(a.pointer_x, b.pointer_x);
    out.pointer_y = larger(a.pointer_y, b.pointer_y);
    return out;
}

std::uint32_t mouse_bound_targets(const BindingTable& table, std::uint8_t mouse_buttons) noexcept {
    std::uint32_t mask = 0u;
    for (std::size_t t = 0; t < kWiiTargetCount; ++t) {
        for (const InputSource& source : table.targets[t]) {
            if (source.kind == SourceKind::Mouse && source.code < kMouseButtonCount &&
                ((mouse_buttons >> source.code) & 1u) != 0u) {
                mask |= wii_target_bit(static_cast<WiiTarget>(t));
            }
        }
    }
    return mask;
}

KeyList referenced_keys(const BindingTable& table) noexcept {
    std::array<bool, 256> seen{};
    for (const TargetBindings& bindings : table.targets) {
        for (const InputSource& source : bindings) {
            if (source.kind == SourceKind::Key) seen[source.code] = true;
        }
    }
    KeyList list{};
    for (std::size_t vk = 1; vk < seen.size() && list.count < list.keys.size(); ++vk) {
        if (seen[vk]) list.keys[list.count++] = static_cast<std::uint8_t>(vk);
    }
    return list;
}

std::optional<InputSource> detect_pad_capture(const PadSample& baseline, const PadSample& now) noexcept {
    if (!now.connected) return std::nullopt;
    const std::uint32_t pressed = now.buttons & ~(baseline.connected ? baseline.buttons : 0u);
    for (unsigned i = 0; i < pad::kButtonBitCount; ++i) {
        if (((pressed >> i) & 1u) != 0u) return button(i);
    }
    constexpr float kStickCapture = 0.6f;
    constexpr float kTriggerCapture = 0.5f;
    for (std::size_t i = 0; i < kPadAxisCount; ++i) {
        const auto axis = static_cast<PadAxis>(i);
        const float before = baseline.connected ? baseline.axes[i] : 0.0f;
        const float value = now.axes[i];
        if (axis == PadAxis::LeftTrigger || axis == PadAxis::RightTrigger) {
            if (value >= kTriggerCapture && before < kTriggerCapture) return axis_pos(axis);
            continue;
        }
        if (value >= kStickCapture && before < kStickCapture) return axis_pos(axis);
        if (value <= -kStickCapture && before > -kStickCapture) return axis_neg(axis);
    }
    return std::nullopt;
}

namespace {
SRWLOCK g_controls_lock = SRWLOCK_INIT;
ControlsConfig& controls_storage() {
    static ControlsConfig config = default_controls_config();
    return config;
}
std::atomic<std::uint64_t> g_controls_generation{1u};
}  // namespace

ControlsConfig get_controls_config() {
    AcquireSRWLockShared(&g_controls_lock);
    ControlsConfig copy = controls_storage();
    ReleaseSRWLockShared(&g_controls_lock);
    return copy;
}

void set_controls_config(const ControlsConfig& config) {
    const ControlsConfig sanitized = sanitize_controls_config(config);
    AcquireSRWLockExclusive(&g_controls_lock);
    controls_storage() = sanitized;
    g_controls_generation.fetch_add(1u, std::memory_order_acq_rel);
    ReleaseSRWLockExclusive(&g_controls_lock);
}

std::uint64_t controls_config_generation() noexcept {
    return g_controls_generation.load(std::memory_order_acquire);
}

}  // namespace galaxy::input
