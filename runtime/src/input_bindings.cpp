#include "galaxy/input_bindings.h"
#include "galaxy/pointer_control.h"
#include "galaxy/rumble.h"
#include "galaxy/c_locale_number.h"

#include "galaxy/input/key_codes.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <clocale>
#include <locale.h>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <shared_mutex>
#include <cstdlib>
#include <cstring>
#include <limits>

#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error The shared bindings engine must not include Windows.h
#endif

namespace galaxy::input {

namespace {

constexpr std::array<const char*, kGameInputCount> kTargetIds{
    "a", "b", "one", "two", "plus", "minus", "home",
    "dpad_up", "dpad_down", "dpad_left", "dpad_right",
    "stick_up", "stick_down", "stick_left", "stick_right",
    "c", "z", "shake", "nunchuk_shake",
    "pointer_up", "pointer_down", "pointer_left", "pointer_right",
    "pointer_recenter", "jump_back",
};

constexpr std::array<const char*, kGameInputCount> kTargetLabels{
    "A", "B", "1", "2", "+", "-", "HOME",
    "D-PAD UP", "D-PAD DOWN", "D-PAD LEFT", "D-PAD RIGHT",
    "STICK UP", "STICK DOWN", "STICK LEFT", "STICK RIGHT",
    "C", "Z", "SHAKE", "NUNCHUK SHAKE",
    "POINTER UP", "POINTER DOWN", "POINTER LEFT", "POINTER RIGHT",
    "RECENTER", "A IN PLAY, B IN MENUS",
};

constexpr std::array<const char*, kControlSchemeCount> kSchemeIds{
    "keyboard", "xbox", "switch_pro", "switch_pro2", "playstation", "steam", "generic"};
constexpr std::array<const char*, kControlSchemeCount> kSchemeLabels{
    "KEYBOARD", "XBOX", "SWITCH PRO", "SWITCH 2 PRO", "PLAYSTATION", "STEAM", "GAMEPAD"};

struct NamedKey {
    std::uint8_t vk;
    const char* name;
};

// Keys whose name is not a single letter or digit.
constexpr NamedKey kNamedKeys[] = {
    {keys::SPACE, "Space"}, {keys::RETURN, "Enter"}, {keys::TAB, "Tab"},
    {keys::BACK, "Backspace"}, {keys::ESCAPE, "Escape"},
    {keys::LSHIFT, "LShift"}, {keys::RSHIFT, "RShift"}, {keys::SHIFT, "Shift"},
    {keys::LCONTROL, "LCtrl"}, {keys::RCONTROL, "RCtrl"}, {keys::CONTROL, "Ctrl"},
    {keys::LMENU, "LAlt"}, {keys::RMENU, "RAlt"}, {keys::MENU, "Alt"},
    {keys::UP, "Up"}, {keys::DOWN, "Down"}, {keys::LEFT, "Left"}, {keys::RIGHT, "Right"},
    {keys::INSERT, "Insert"}, {keys::DELETE, "Delete"}, {keys::HOME, "Home"},
    {keys::END, "End"}, {keys::PRIOR, "PageUp"}, {keys::NEXT, "PageDown"},
    {keys::CAPITAL, "CapsLock"},
    {keys::OEM_MINUS, "Minus"}, {keys::OEM_PLUS, "Equals"},
    {keys::OEM_4, "LBracket"}, {keys::OEM_6, "RBracket"}, {keys::OEM_5, "Backslash"},
    {keys::OEM_1, "Semicolon"}, {keys::OEM_7, "Quote"}, {keys::OEM_COMMA, "Comma"},
    {keys::OEM_PERIOD, "Period"}, {keys::OEM_2, "Slash"}, {keys::OEM_3, "Grave"},
    {keys::ADD, "NumAdd"}, {keys::SUBTRACT, "NumSub"}, {keys::MULTIPLY, "NumMul"},
    {keys::DIVIDE, "NumDiv"}, {keys::DECIMAL, "NumDecimal"},
};

constexpr std::array<const char*, kMouseButtonCount> kMouseNames{
    "MouseLeft", "MouseRight", "MouseMiddle", "Mouse4", "Mouse5"};

constexpr std::array<const char*, pad::kButtonBitCount> kXboxButtonNames{
    "DpadUp", "DpadDown", "DpadLeft", "DpadRight", "Menu", "View", "LS", "RS",
    "LB", "RB", "Guide", "Share", "A", "B", "X", "Y", "GL", "GR", "C",
    "GL2", "GR2", "LPad", "RPad", "LeftFn", "RightFn"};
constexpr std::array<const char*, pad::kButtonBitCount> kSwitchButtonNames{
    "DpadUp", "DpadDown", "DpadLeft", "DpadRight", "Plus", "Minus", "LS", "RS",
    "L", "R", "Home", "Capture", "B", "A", "Y", "X", "GL", "GR", "C",
    "GL2", "GR2", "LPad", "RPad", "LeftFn", "RightFn"};
constexpr std::array<const char*, pad::kButtonBitCount> kPlayStationButtonNames{
    "DpadUp", "DpadDown", "DpadLeft", "DpadRight", "Options", "Share", "L3", "R3",
    "L1", "R1", "PS", "Touchpad", "Cross", "Circle", "Square", "Triangle",
    "LeftPaddle", "RightPaddle", "Mute", "GL2", "GR2", "LPad", "RPad", "LeftFn", "RightFn"};
constexpr std::array<const char*, pad::kButtonBitCount> kSteamButtonNames{
    "DpadUp", "DpadDown", "DpadLeft", "DpadRight", "Menu", "View", "LS", "RS",
    "LB", "RB", "Steam", "QAM", "A", "B", "X", "Y", "L4", "R4", "C",
    "L5", "R5", "LPad", "RPad", "LeftFn", "RightFn"};

// Generic controllers by HID button number (generic_hid.h button_bit).
constexpr std::array<const char*, pad::kButtonBitCount> kGenericButtonNames{
    "DpadUp", "DpadDown", "DpadLeft", "DpadRight", "Button10", "Button9", "Button11", "Button12",
    "Button5", "Button6", "Button13", "Button14", "Button1", "Button2", "Button3", "Button4", "Button7", "Button8",
    "Button15", "Button16", "Button17", "Button18", "Button19", "LeftFn", "RightFn"};

constexpr std::array<const char*, 4> kStickAxisNames{"LX", "LY", "RX", "RY"};

bool iequals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    const auto lower = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c; };
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (lower(static_cast<unsigned char>(a[i])) != lower(static_cast<unsigned char>(b[i]))) {
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
    switch (scheme) {
    case ControlScheme::PlayStation: return kPlayStationButtonNames;
    case ControlScheme::Steam: return kSteamButtonNames;
    case ControlScheme::Generic: return kGenericButtonNames;
    case ControlScheme::Xbox: return kXboxButtonNames;
    default: return kSwitchButtonNames;
    }
}

const char* trigger_name(ControlScheme scheme, PadAxis axis) noexcept {
    const bool left = axis == PadAxis::LeftTrigger;
    switch (scheme) {
    case ControlScheme::Xbox:
    case ControlScheme::Steam:
    case ControlScheme::Generic: return left ? "LT" : "RT";
    case ControlScheme::PlayStation: return left ? "L2" : "R2";
    default: return left ? "ZL" : "ZR";
    }
}

std::string format_key(std::uint8_t vk) {
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) {
        return std::string(1, static_cast<char>(vk));
    }
    if (vk >= keys::NUMPAD0 && vk <= keys::NUMPAD9) {
        return "Num" + std::to_string(vk - keys::NUMPAD0);
    }
    if (vk >= keys::F1 && vk <= keys::F24) {
        return "F" + std::to_string(vk - keys::F1 + 1);
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
        return static_cast<std::uint8_t>(keys::NUMPAD0 + value);
    }
    if (text.size() >= 2u && (text[0] == 'F' || text[0] == 'f') &&
        parse_number(text.substr(1), 10, value) && value >= 1 && value <= 24) {
        return static_cast<std::uint8_t>(keys::F1 + value - 1);
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

void set(BindingTable& table, GameInput target, std::initializer_list<InputSource> sources) {
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
    char* end = nullptr;
    errno = 0;
    const float value = galaxy::strtof_c(copy.c_str(), &end);
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
    galaxy::format_g9_c(text, sizeof(text), static_cast<double>(value));
    return text;
}

constexpr const char* kMotionNames[] = {"off", "remote", "nunchuk"};
static_assert(SchemeTuning{}.rumble == rumble::kDefaultStrength);

bool is_gyro_pad_scheme(ControlScheme scheme) noexcept {
    return scheme == ControlScheme::PlayStation || scheme == ControlScheme::Steam;
}

// PlayStation and Steam controllers: the Switch gyro layout with the Xbox face
// positions (South confirms and jumps, East backs out of menus).
BindingTable gyro_pad_binding_table() {
    BindingTable table{};
    using T = GameInput;
    set(table, T::A, {button(pad::kSouthBit)});
    set(table, T::JumpBack, {button(pad::kEastBit)});
    set(table, T::B, {button(pad::kLeftShoulderBit)});
    set(table, T::Plus, {button(pad::kStartBit)});
    set(table, T::Minus, {button(pad::kBackBit)});
    set(table, T::Home, {button(pad::kHomeBit)});
    set(table, T::DpadUp, {button(pad::kDpadUpBit), axis_pos(PadAxis::RightY)});
    set(table, T::DpadDown, {button(pad::kDpadDownBit), axis_neg(PadAxis::RightY)});
    set(table, T::DpadLeft, {button(pad::kDpadLeftBit), axis_neg(PadAxis::RightX)});
    set(table, T::DpadRight, {button(pad::kDpadRightBit), axis_pos(PadAxis::RightX)});
    set(table, T::StickUp, {axis_pos(PadAxis::LeftY)});
    set(table, T::StickDown, {axis_neg(PadAxis::LeftY)});
    set(table, T::StickLeft, {axis_neg(PadAxis::LeftX)});
    set(table, T::StickRight, {axis_pos(PadAxis::LeftX)});
    set(table, T::C, {button(pad::kLeftStickBit)});
    set(table, T::Z, {axis_pos(PadAxis::LeftTrigger)});
    set(table, T::Shake, {button(pad::kWestBit), button(pad::kNorthBit), axis_pos(PadAxis::RightTrigger)});
    set(table, T::PointerUp, {axis_pos(PadAxis::RightY)});
    set(table, T::PointerDown, {axis_neg(PadAxis::RightY)});
    set(table, T::PointerLeft, {axis_neg(PadAxis::RightX)});
    set(table, T::PointerRight, {axis_pos(PadAxis::RightX)});
    set(table, T::PointerRecenter, {button(pad::kRightShoulderBit)});
    return table;
}

// Generic controllers: the Xbox shape on the HID button numbers most pads
// share (1 jumps, 2 backs out of menus, 5 Star Bits, 7/8 the triggers when
// they are buttons). The scheme is newer than every migration step: all
// versions share these.
BindingTable generic_binding_table() {
    BindingTable table{};
    using T = GameInput;
    set(table, T::A, {button(pad::kSouthBit)});
    set(table, T::JumpBack, {button(pad::kEastBit)});
    set(table, T::B, {button(pad::kLeftShoulderBit)});
    set(table, T::Plus, {button(pad::kStartBit)});
    set(table, T::Minus, {button(pad::kBackBit)});
    set(table, T::Home, {button(pad::kHomeBit)});
    set(table, T::DpadUp, {button(pad::kDpadUpBit)});
    set(table, T::DpadDown, {button(pad::kDpadDownBit)});
    set(table, T::DpadLeft, {button(pad::kDpadLeftBit)});
    set(table, T::DpadRight, {button(pad::kDpadRightBit)});
    set(table, T::StickUp, {axis_pos(PadAxis::LeftY)});
    set(table, T::StickDown, {axis_neg(PadAxis::LeftY)});
    set(table, T::StickLeft, {axis_neg(PadAxis::LeftX)});
    set(table, T::StickRight, {axis_pos(PadAxis::LeftX)});
    set(table, T::C, {button(pad::kLeftStickBit)});
    set(table, T::Z, {axis_pos(PadAxis::LeftTrigger), button(pad::kGripLeftBit)});
    set(table, T::Shake, {button(pad::kWestBit), button(pad::kNorthBit), axis_pos(PadAxis::RightTrigger),
                          button(pad::kGripRightBit)});
    set(table, T::PointerUp, {axis_pos(PadAxis::RightY)});
    set(table, T::PointerDown, {axis_neg(PadAxis::RightY)});
    set(table, T::PointerLeft, {axis_neg(PadAxis::RightX)});
    set(table, T::PointerRight, {axis_pos(PadAxis::RightX)});
    set(table, T::PointerRecenter, {button(pad::kRightStickBit)});
    return table;
}

}  // namespace

BindingTable legacy_binding_table(ControlScheme scheme) {
    if (is_gyro_pad_scheme(scheme)) return gyro_pad_binding_table();
    if (scheme == ControlScheme::Generic) return generic_binding_table();
    BindingTable table{};
    using T = GameInput;
    if (scheme == ControlScheme::Keyboard) {
        set(table, T::A, {key(keys::SPACE), key(keys::RETURN), mouse(MouseButton::Left)});
        set(table, T::B, {key(keys::LSHIFT), key(keys::RSHIFT), mouse(MouseButton::Right)});
        set(table, T::One, {key('1'), key(keys::NUMPAD1)});
        set(table, T::Two, {key('2'), key(keys::NUMPAD2)});
        set(table, T::Plus, {key(keys::OEM_PLUS), key('P')});
        set(table, T::Minus, {key(keys::OEM_MINUS), key('M')});
        set(table, T::Home, {key('H')});
        set(table, T::DpadUp, {key(keys::UP)});
        set(table, T::DpadDown, {key(keys::DOWN)});
        set(table, T::DpadLeft, {key(keys::LEFT)});
        set(table, T::DpadRight, {key(keys::RIGHT)});
        set(table, T::StickUp, {key('W')});
        set(table, T::StickDown, {key('S')});
        set(table, T::StickLeft, {key('A')});
        set(table, T::StickRight, {key('D')});
        set(table, T::C, {key('C')});
        set(table, T::Z, {key('Z'), key(keys::LCONTROL), key(keys::RCONTROL)});
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

// Default bindings of controls version 2, kept for migration.
BindingTable version2_binding_table(ControlScheme scheme) {
    BindingTable table = legacy_binding_table(scheme);
    if (scheme == ControlScheme::Keyboard || scheme == ControlScheme::Generic || is_gyro_pad_scheme(scheme)) {
        return table;
    }
    using T = GameInput;
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

// Default bindings of controls version 3, kept for migration.
BindingTable version3_binding_table(ControlScheme scheme) {
    BindingTable table = version2_binding_table(scheme);
    if (scheme == ControlScheme::Keyboard || scheme == ControlScheme::Generic || is_gyro_pad_scheme(scheme)) {
        return table;
    }
    using T = GameInput;
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
        for (const auto target : {GameInput::StickUp, GameInput::StickDown, GameInput::StickLeft, GameInput::StickRight,
                                 GameInput::C, GameInput::Z, GameInput::Shake, GameInput::PointerUp,
                                 GameInput::PointerDown, GameInput::PointerLeft, GameInput::PointerRight,
                                 GameInput::PointerRecenter}) {
            for (auto& source : table[target]) {
                if (source.kind == SourceKind::Key && source.code >= 'A' && source.code <= 'Z')
                    source.kind = SourceKind::KeyPosition;
            }
        }
        return table;
    }
    if (scheme == ControlScheme::Keyboard || scheme == ControlScheme::Generic || is_gyro_pad_scheme(scheme)) {
        return table;
    }
    using T = GameInput;
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
        tuning.pointer_hide_seconds = kDefaultPointerHideSeconds;
        break;
    case ControlScheme::SwitchPro:
    case ControlScheme::SwitchPro2:
    case ControlScheme::PlayStation:
    case ControlScheme::Steam:
        tuning.stick_deadzone = 0.12f;
        tuning.pointer_deadzone = 0.12f;
        tuning.pointer_speed = 2.4f;
        tuning.motion_shake = MotionShake::Remote;
        tuning.shake_sensitivity = 3;
        tuning.pointer_hide_seconds = kDefaultPointerHideSeconds;
        break;
    case ControlScheme::Generic:
        // Uncalibrated sticks: a wider centre than the calibrated pads.
        tuning.stick_deadzone = 0.15f;
        tuning.pointer_deadzone = 0.15f;
        tuning.pointer_speed = 2.4f;
        tuning.pointer_hide_seconds = kDefaultPointerHideSeconds;
        break;
    }
    return tuning;
}

SchemeTuning default_scheme_tuning(ControlScheme scheme) {
    SchemeTuning tuning = legacy_scheme_tuning(scheme);
    if (scheme_has_motion(scheme)) {
        tuning.gyro_pointer = true;
    }
    if (scheme_has_touch(scheme)) {
        tuning.touch_pointer = true;
    }
    if (scheme_is_gamepad(scheme)) {
        // Still-pointer hiding (pointer_control.h kDefaultPointerHide*).
        tuning.pointer_hide_seconds = kDefaultPointerHideSeconds;
        tuning.pointer_hide_radius = 0.12f;
    }
    return tuning;
}

unsigned migrate_legacy_controls(ControlsConfig& config, unsigned version) noexcept {
    if (version >= kControlsVersion) return 0u;
    unsigned changed = 0u;
    for (std::size_t s = 0; s < kControlSchemeCount; ++s) {
        const auto scheme = static_cast<ControlScheme>(s);
        const BindingTable legacy = legacy_binding_table(scheme);
        const BindingTable version2 = version2_binding_table(scheme);
        const BindingTable version3 = version3_binding_table(scheme);
        const BindingTable current = default_binding_table(scheme);
        for (std::size_t t = 0; t < kGameInputCount; ++t) {
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
        if (version < 5u && scheme_is_gamepad(scheme)) {
            const bool old_seconds =
                (version < 4u && tuning.pointer_hide_seconds == kVersion3PointerHideSeconds) ||
                tuning.pointer_hide_seconds == kVersion4PointerHideSeconds;
            if (old_seconds && tuning.pointer_hide_seconds != current_tuning.pointer_hide_seconds) {
                tuning.pointer_hide_seconds = current_tuning.pointer_hide_seconds;
                ++changed;
            }
            if (version < 4u && tuning.pointer_hide_radius == kVersion3PointerHideRadius) {
                tuning.pointer_hide_radius = current_tuning.pointer_hide_radius;
                ++changed;
            }
            if (tuning.rumble == kVersion4Rumble && tuning.rumble != current_tuning.rumble) {
                tuning.rumble = current_tuning.rumble;
                ++changed;
            }
        }
        if (version < 7u && scheme_is_gamepad(scheme) && tuning.pointer_hide_seconds == kVersion6PointerHideSeconds &&
            current_tuning.pointer_hide_seconds != kVersion6PointerHideSeconds) {
            tuning.pointer_hide_seconds = current_tuning.pointer_hide_seconds;
            ++changed;
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
const char* game_input_id(GameInput target) noexcept {
    return kTargetIds[static_cast<std::size_t>(target) % kGameInputCount];
}
const char* game_input_label(GameInput target) noexcept {
    return kTargetLabels[static_cast<std::size_t>(target) % kGameInputCount];
}

std::optional<ControlScheme> parse_control_scheme(std::string_view id) noexcept {
    for (std::size_t i = 0; i < kSchemeIds.size(); ++i) {
        if (iequals(id, kSchemeIds[i])) return static_cast<ControlScheme>(i);
    }
    return std::nullopt;
}

std::optional<GameInput> parse_game_input(std::string_view id) noexcept {
    for (std::size_t i = 0; i < kTargetIds.size(); ++i) {
        if (iequals(id, kTargetIds[i])) return static_cast<GameInput>(i);
    }
    return std::nullopt;
}

std::string format_input_source(ControlScheme scheme, InputSource source) {
    switch (source.kind) {
    case SourceKind::None:
        return "None";
    case SourceKind::Key:
        return format_key(source.code);
    case SourceKind::KeyPosition:
        return source.code >= 'A' && source.code <= 'Z' ? "Position." + format_key(source.code) : "None";
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
        constexpr std::string_view prefix = "Position.";
        if (text.size() == prefix.size() + 1u && iequals(text.substr(0, prefix.size()), prefix)) {
            auto letter = static_cast<std::uint8_t>(text.back());
            if (letter >= 'a' && letter <= 'z') letter = static_cast<std::uint8_t>(letter - 'a' + 'A');
            if (letter >= 'A' && letter <= 'Z') return InputSource{SourceKind::KeyPosition, letter};
            return std::nullopt;
        }
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
    if (scheme == ControlScheme::PlayStation && iequals(text, "Create")) return button(pad::kBackBit);
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

bool binding_source_allowed(ControlScheme scheme, GameInput target, InputSource source) noexcept {
    switch (source.kind) {
    case SourceKind::None:
        return false;
    case SourceKind::Key:
        return scheme == ControlScheme::Keyboard && source.code != 0u;
    case SourceKind::KeyPosition:
        return scheme == ControlScheme::Keyboard && source.code >= 'A' && source.code <= 'Z';
    case SourceKind::Mouse:
        return scheme == ControlScheme::Keyboard && source.code < kMouseButtonCount &&
               !game_input_is_analog(target);
    case SourceKind::Button:
        return source.code < pad::kButtonBitCount &&
               (scheme_button_mask(scheme) & pad::bit(source.code)) != 0u;
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
        const auto target = parse_game_input(name);
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
    } else if (name == "touch_pointer" && parse_bool(value, flag)) {
        tuning.touch_pointer = flag;
    } else if (name == "touch_sensitivity" && parse_float_c(value, number)) {
        tuning.touch_sensitivity = number;
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
    } else if (name == "vibration" && parse_bool(value, flag)) {
        tuning.vibration = flag;
    } else if (name == "rumble" && parse_float_c(value, number)) {
        // 0 was "off" before controls version 5.
        if (number <= 0.0f) {
            tuning.vibration = false;
        } else {
            tuning.rumble = rumble::snap_strength(number);
        }
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
        tuning.touch_sensitivity = clamp_or(tuning.touch_sensitivity, 0.2f, 5.0f, defaults.touch_sensitivity);
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
        if (!scheme_has_touch(static_cast<ControlScheme>(s))) {
            tuning.touch_pointer = false;
            tuning.touch_sensitivity = defaults.touch_sensitivity;
        }
    }
    return config;
}

std::string format_controls_settings(const ControlsConfig& config) {
    std::string text;
    for (std::size_t s = 0; s < kControlSchemeCount; ++s) {
        const auto scheme = static_cast<ControlScheme>(s);
        const std::string prefix = std::string(control_scheme_id(scheme)) + ".";
        for (std::size_t t = 0; t < kGameInputCount; ++t) {
            text += "bind." + prefix + kTargetIds[t] + "=" +
                    format_target_bindings(scheme, config.tables[s].targets[t]) + "\n";
        }
        const SchemeTuning& tuning = config.tuning[s];
        if (scheme_is_gamepad(scheme)) {
            text += "tune." + prefix + "stick_deadzone=" + format_float_c(tuning.stick_deadzone) + "\n";
            text += "tune." + prefix + "pointer_deadzone=" + format_float_c(tuning.pointer_deadzone) + "\n";
            text += "tune." + prefix + "pointer_hide_seconds=" + format_float_c(tuning.pointer_hide_seconds) + "\n";
            text += "tune." + prefix + "pointer_hide_radius=" + format_float_c(tuning.pointer_hide_radius) + "\n";
            text += "tune." + prefix + "vibration=" + (tuning.vibration ? "1" : "0") + "\n";
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
        if (scheme_has_touch(scheme)) {
            text += "tune." + prefix + "touch_pointer=" + (tuning.touch_pointer ? "1" : "0") + "\n";
            text += "tune." + prefix + "touch_sensitivity=" + format_float_c(tuning.touch_sensitivity) + "\n";
        }
    }
    return text;
}

namespace {

float source_value_keyboard(InputSource source, const KeyboardMouseLevels& levels) noexcept {
    switch (source.kind) {
    case SourceKind::Key:
        return levels.key(source.code) ? 1.0f : 0.0f;
    case SourceKind::KeyPosition:
        return levels.position(source.code) ? 1.0f : 0.0f;
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

}  // namespace

float keyboard_source_value(InputSource source, const KeyboardMouseLevels& levels) noexcept {
    return source_value_keyboard(source, levels);
}

float pad_source_value(InputSource source, const PadSample& sample) noexcept {
    return source_value_pad(source, sample);
}

namespace {

bool is_trigger_source(InputSource source) noexcept {
    return source.kind == SourceKind::AxisPositive &&
           (source.code == static_cast<std::uint8_t>(PadAxis::LeftTrigger) ||
            source.code == static_cast<std::uint8_t>(PadAxis::RightTrigger));
}

bool is_stick_source(InputSource source) noexcept {
    return (source.kind == SourceKind::AxisPositive || source.kind == SourceKind::AxisNegative) &&
           source.code <= static_cast<std::uint8_t>(PadAxis::RightY);
}

bool is_pointer_target(GameInput target) noexcept {
    return target >= GameInput::PointerUp && target <= GameInput::PointerRight;
}

// Bit per stick axis (code) bound to a pointer direction / to anything else.
struct StickRoles {
    std::uint8_t pointer = 0;
    std::uint8_t other = 0;
};

StickRoles stick_roles(const BindingTable& table) noexcept {
    StickRoles roles{};
    for (std::size_t t = 0; t < kGameInputCount; ++t) {
        for (const InputSource& source : table.targets[t]) {
            if (!is_stick_source(source)) continue;
            const auto bit = static_cast<std::uint8_t>(1u << source.code);
            (is_pointer_target(static_cast<GameInput>(t)) ? roles.pointer : roles.other) |= bit;
        }
    }
    return roles;
}

template <typename ValueOf, typename PressedOf, typename Usable>
LogicalInput evaluate(const BindingTable& table, ValueOf&& value_of, PressedOf&& pressed_of,
                      Usable&& usable) noexcept {
    LogicalInput out{};
    std::array<float, kGameInputCount> analog{};
    for (std::size_t t = 0; t < kGameInputCount; ++t) {
        const auto target = static_cast<GameInput>(t);
        const bool is_analog = game_input_is_analog(target);
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
        if (pressed) out.pressed |= game_input_bit(target);
    }
    const auto at = [&analog](GameInput t) { return analog[static_cast<std::size_t>(t)]; };
    out.stick_x = at(GameInput::StickRight) - at(GameInput::StickLeft);
    out.stick_y = at(GameInput::StickUp) - at(GameInput::StickDown);
    out.pointer_x = at(GameInput::PointerRight) - at(GameInput::PointerLeft);
    out.pointer_y = at(GameInput::PointerUp) - at(GameInput::PointerDown);
    return out;
}

}  // namespace

LogicalInput evaluate_keyboard_bindings(const BindingTable& table, const KeyboardMouseLevels& levels) noexcept {
    return evaluate(
        table,
        [&levels](InputSource s) { return source_value_keyboard(s, levels); },
        [&levels](GameInput, InputSource s) { return source_value_keyboard(s, levels) >= 0.5f; },
        [](GameInput, InputSource) { return true; });
}

LogicalInput evaluate_pad_bindings(const BindingTable& table, const PadSample& sample,
                                   float trigger_threshold) noexcept {
    return evaluate_pad_bindings(table, sample, trigger_threshold, 0u, false);
}
LogicalInput evaluate_pad_bindings(const BindingTable& table, const PadSample& sample,
                                   float trigger_threshold, std::uint32_t previous_pressed,
                                   bool sensor_pointing) noexcept {
    if (!sample.connected) return {};
    const StickRoles roles = stick_roles(table);
    return evaluate(
        table,
        [&sample](InputSource s) { return source_value_pad(s, sample); },
        [&sample, trigger_threshold, previous_pressed](GameInput target, InputSource s) {
            const float value = source_value_pad(s, sample);
            if (is_trigger_source(s)) return value >= trigger_threshold && value > 0.0f;
            const bool was_pressed = (previous_pressed & game_input_bit(target)) != 0u;
            return value >= kStickDirectionPress ||
                   (was_pressed && is_stick_source(s) && value >= kStickDirectionRelease);
        },
        [&roles, sensor_pointing](GameInput target, InputSource s) {
            if (!is_stick_source(s)) return true;
            const auto bit = static_cast<std::uint8_t>(1u << s.code);
            if ((roles.pointer & roles.other & bit) == 0u) return true;
            return is_pointer_target(target) ? !sensor_pointing : sensor_pointing;
        });
}

std::uint32_t shared_binding_targets(const BindingTable& table) noexcept {
    std::uint32_t shared = 0u;
    for (std::size_t t = 0; t < kGameInputCount; ++t) {
        for (std::size_t u = t + 1u; u < kGameInputCount; ++u) {
            const auto first = static_cast<GameInput>(t);
            const auto second = static_cast<GameInput>(u);
            for (const InputSource& a : table.targets[t]) {
                if (a.empty()) continue;
                const bool together = !is_stick_source(a) || is_pointer_target(first) == is_pointer_target(second);
                for (const InputSource& b : table.targets[u]) {
                    if (together && a == b) shared |= game_input_bit(first) | game_input_bit(second);
                }
            }
        }
    }
    return shared;
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
    for (std::size_t t = 0; t < kGameInputCount; ++t) {
        for (const InputSource& source : table.targets[t]) {
            if (source.kind == SourceKind::Mouse && source.code < kMouseButtonCount &&
                ((mouse_buttons >> source.code) & 1u) != 0u) {
                mask |= game_input_bit(static_cast<GameInput>(t));
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

KeyList referenced_key_positions(const BindingTable& table) noexcept {
    std::array<bool, 26> seen{};
    for (const auto& bindings : table.targets) {
        for (const auto& source : bindings) {
            if (source.kind == SourceKind::KeyPosition && source.code >= 'A' && source.code <= 'Z')
                seen[source.code - 'A'] = true;
        }
    }
    KeyList list;
    for (std::size_t i = 0; i < seen.size(); ++i) {
        if (seen[i]) list.keys[list.count++] = static_cast<std::uint8_t>('A' + i);
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
std::shared_mutex g_controls_lock;
ControlsConfig& controls_storage() {
    static ControlsConfig config = default_controls_config();
    return config;
}
std::atomic<std::uint64_t> g_controls_generation{1u};
}  // namespace

ControlsConfig get_controls_config() {
    g_controls_lock.lock_shared();
    ControlsConfig copy = controls_storage();
    g_controls_lock.unlock_shared();
    return copy;
}

void set_controls_config(const ControlsConfig& config) {
    const ControlsConfig sanitized = sanitize_controls_config(config);
    g_controls_lock.lock();
    controls_storage() = sanitized;
    g_controls_generation.fetch_add(1u, std::memory_order_acq_rel);
    g_controls_lock.unlock();
}

std::uint64_t controls_config_generation() noexcept {
    return g_controls_generation.load(std::memory_order_acquire);
}

namespace {

struct GlyphPlace {
    ButtonGlyph glyph = ButtonGlyph::Keep;
    std::uint8_t place = 0u;
};

GlyphPlace glyph_of(InputSource source) noexcept {
    if (source.kind == SourceKind::AxisPositive || source.kind == SourceKind::AxisNegative) {
        const bool positive = source.kind == SourceKind::AxisPositive;
        switch (static_cast<PadAxis>(source.code)) {
        case PadAxis::LeftTrigger: return positive ? GlyphPlace{ButtonGlyph::Trigger, 0u} : GlyphPlace{};
        case PadAxis::RightTrigger: return positive ? GlyphPlace{ButtonGlyph::Trigger, 1u} : GlyphPlace{};
        case PadAxis::LeftY: return {ButtonGlyph::StickAxis, positive ? std::uint8_t{0u} : std::uint8_t{1u}};
        case PadAxis::LeftX: return {ButtonGlyph::StickAxis, positive ? std::uint8_t{3u} : std::uint8_t{2u}};
        case PadAxis::RightY: return {ButtonGlyph::StickAxis, positive ? std::uint8_t{4u} : std::uint8_t{5u}};
        case PadAxis::RightX: return {ButtonGlyph::StickAxis, positive ? std::uint8_t{7u} : std::uint8_t{6u}};
        }
        return {};
    }
    if (source.kind != SourceKind::Button) return {};
    switch (source.code) {
    case pad::kSouthBit: return {ButtonGlyph::Face, 0u};
    case pad::kEastBit: return {ButtonGlyph::Face, 1u};
    case pad::kWestBit: return {ButtonGlyph::Face, 2u};
    case pad::kNorthBit: return {ButtonGlyph::Face, 3u};
    case pad::kLeftShoulderBit: return {ButtonGlyph::Shoulder, 0u};
    case pad::kRightShoulderBit: return {ButtonGlyph::Shoulder, 1u};
    case pad::kLeftStickBit: return {ButtonGlyph::Stick, 0u};
    case pad::kRightStickBit: return {ButtonGlyph::Stick, 1u};
    case pad::kStartBit: return {ButtonGlyph::Start, 0u};
    case pad::kBackBit: return {ButtonGlyph::Back, 0u};
    case pad::kHomeBit: return {ButtonGlyph::Home, 0u};
    case pad::kDpadUpBit: return {ButtonGlyph::Dpad, 0u};
    case pad::kDpadDownBit: return {ButtonGlyph::Dpad, 1u};
    case pad::kDpadLeftBit: return {ButtonGlyph::Dpad, 2u};
    case pad::kDpadRightBit: return {ButtonGlyph::Dpad, 3u};
    case pad::kCaptureBit: return {ButtonGlyph::Capture, 0u};
    case pad::kChatBit: return {ButtonGlyph::Chat, 0u};
    case pad::kGripLeftBit: return {ButtonGlyph::Grip, 0u};
    case pad::kGripRightBit: return {ButtonGlyph::Grip, 1u};
    case pad::kGripLeft2Bit: return {ButtonGlyph::Grip, 2u};
    case pad::kGripRight2Bit: return {ButtonGlyph::Grip, 3u};
    case pad::kLeftPadBit: return {ButtonGlyph::Trackpad, 0u};
    case pad::kRightPadBit: return {ButtonGlyph::Trackpad, 1u};
    case pad::kFnLeftBit: return {ButtonGlyph::Grip, 4u};
    case pad::kFnRightBit: return {ButtonGlyph::Grip, 5u};
    default: return {};
    }
}

// The layout keys in key icon order: US ; = , - . / ` [ \ ] ', OEM 8 and the ISO key.
constexpr std::array<std::uint8_t, kKeyIconLayoutCount> kLayoutKeys{
    keys::OEM_1, keys::OEM_PLUS, keys::OEM_COMMA, keys::OEM_MINUS, keys::OEM_PERIOD, keys::OEM_2, keys::OEM_3,
    keys::OEM_4, keys::OEM_5, keys::OEM_6, keys::OEM_7, keys::OEM_8, keys::OEM_102};

// Named keys' icons in order from Space (the game API's NEBULA_KEY_SPACE .. NEBULA_KEY_RIGHT_WIN).
constexpr std::uint8_t kKeyIconSpace = 75;
constexpr std::array<std::uint8_t, 32> kNamedKeyIcons{
    keys::SPACE, keys::RETURN, keys::TAB, keys::BACK, keys::ESCAPE, keys::SHIFT, keys::LSHIFT, keys::RSHIFT, keys::CONTROL, keys::LCONTROL,
    keys::RCONTROL, keys::MENU, keys::LMENU, keys::RMENU, keys::UP, keys::DOWN, keys::LEFT, keys::RIGHT, keys::INSERT, keys::DELETE, keys::HOME,
    keys::END, keys::PRIOR, keys::NEXT, keys::CAPITAL, keys::NUMLOCK, keys::SCROLL, keys::PAUSE, keys::SNAPSHOT, keys::APPS, keys::LWIN, keys::RWIN};
static_assert(kKeyIconSpace + kNamedKeyIcons.size() == kKeyIconLayoutFirst);

}  // namespace

std::optional<std::uint8_t> key_icon(InputSource source) noexcept {
    using Icon = std::uint8_t;
    if (source.kind == SourceKind::Mouse) {
        if (source.code >= kMouseButtonCount) return std::nullopt;
        return static_cast<Icon>(kKeyIconMouseLeft + source.code);
    }
    if (source.kind != SourceKind::Key && source.kind != SourceKind::KeyPosition) return std::nullopt;
    const std::uint8_t vk = source.code;
    if (vk >= 'A' && vk <= 'Z') return static_cast<Icon>(vk - 'A');
    if (vk >= '0' && vk <= '9') return static_cast<Icon>(26 + vk - '0');
    if (vk >= keys::F1 && vk <= keys::F24) return static_cast<Icon>(36 + vk - keys::F1);
    if (vk >= keys::NUMPAD0 && vk <= keys::NUMPAD9) return static_cast<Icon>(60 + vk - keys::NUMPAD0);
    switch (vk) {
    case keys::ADD: return Icon{70};
    case keys::SUBTRACT: return Icon{71};
    case keys::MULTIPLY: return Icon{72};
    case keys::DIVIDE: return Icon{73};
    case keys::DECIMAL: return Icon{74};
    default: break;
    }
    for (std::size_t i = 0; i < kNamedKeyIcons.size(); ++i) {
        if (kNamedKeyIcons[i] == vk) return static_cast<std::uint8_t>(kKeyIconSpace + i);
    }
    for (std::size_t i = 0; i < kLayoutKeys.size(); ++i) {
        if (kLayoutKeys[i] == vk) return static_cast<std::uint8_t>(kKeyIconLayoutFirst + i);
    }
    return kKeyIconOther;
}

std::array<char, 8> key_icon_name(std::uint8_t vk) noexcept {
    struct Name {
        std::uint8_t vk;
        const char* name;
    };
    static constexpr Name kNames[] = {
        {keys::VOLUME_MUTE, "MUTE"}, {keys::VOLUME_DOWN, "VOL -"}, {keys::VOLUME_UP, "VOL +"}, {keys::MEDIA_NEXT_TRACK, "NEXT"},
        {keys::MEDIA_PREV_TRACK, "PREV"}, {keys::MEDIA_STOP, "STOP"}, {keys::MEDIA_PLAY_PAUSE, "PLAY"}, {keys::BROWSER_BACK, "BACK"},
        {keys::BROWSER_FORWARD, "FWD"}, {keys::BROWSER_REFRESH, "RELOAD"}, {keys::BROWSER_STOP, "STOP"},
        {keys::BROWSER_SEARCH, "SEARCH"}, {keys::BROWSER_FAVORITES, "FAV"}, {keys::BROWSER_HOME, "WEB"}, {keys::LAUNCH_MAIL, "MAIL"},
        {keys::LAUNCH_MEDIA_SELECT, "MEDIA"}, {keys::LAUNCH_APP1, "APP 1"}, {keys::LAUNCH_APP2, "APP 2"}, {keys::SLEEP, "SLEEP"},
        {keys::HELP, "HELP"}, {keys::SELECT, "SELECT"}, {keys::PRINT, "PRINT"}, {keys::EXECUTE, "EXEC"}, {keys::CLEAR, "CLEAR"},
        {keys::SEPARATOR, "SEP"}, {keys::CANCEL, "BREAK"}, {keys::KANA, "KANA"}, {keys::JUNJA, "JUNJA"}, {keys::FINAL, "FINAL"},
        {keys::KANJI, "KANJI"}, {keys::CONVERT, "CONV"}, {keys::NONCONVERT, "NCONV"}, {keys::ACCEPT, "ACCEPT"},
        {keys::MODECHANGE, "MODE"}, {keys::OEM_ATTN, "EISU"}, {keys::OEM_FINISH, "KATA"}, {keys::OEM_COPY, "HIRA"},
        {keys::OEM_AUTO, "HANKAK"}, {keys::OEM_ENLW, "ZENKAK"}, {keys::OEM_BACKTAB, "ROMAJI"}, {keys::ATTN, "ATTN"},
        {keys::PLAY, "PLAY"}, {keys::ZOOM, "ZOOM"}, {keys::OEM_CLEAR, "CLEAR"},
    };
    std::array<char, 8> out{};
    for (const Name& name : kNames) {
        if (name.vk == vk) {
            std::snprintf(out.data(), out.size(), "%s", name.name);
            return out;
        }
    }
    std::snprintf(out.data(), out.size(), "VK%02X", static_cast<unsigned>(vk));
    return out;
}

std::uint8_t key_icon_layout_key(std::size_t index) noexcept {
    return index < kLayoutKeys.size() ? kLayoutKeys[index] : 0u;
}

namespace {

// The keyboard's icons: each game button's input as the player reaches for it there (B's in menus JumpBack's first),
// HOME's, and the Control Pad's four keys. A in menus and B everywhere show their first mouse button where they have one
// (the pointer chooses in menus; B shoots Star Bits where the pointer aims: Space and the left click are both A, Shift
// and the right click both B); every other icon, and A in play, its first key, else its first mouse button.
ButtonIconSpec keyboard_icon_spec(const BindingTable& table, bool menu) noexcept {
    ButtonIconSpec spec;
    spec.style = ButtonIconStyle::Keyboard;
    const auto first = [&table](GameInput target, bool mouse_first = false) -> std::optional<InputSource> {
        for (const SourceKind kind : {mouse_first ? SourceKind::Mouse : SourceKind::Key,
                                      mouse_first ? SourceKind::Key : SourceKind::Mouse}) {
            for (const InputSource source : table[target]) {
                const bool matches = source.kind == kind || (kind == SourceKind::Key && source.kind == SourceKind::KeyPosition);
                if (matches && key_icon(source).has_value()) return source;
            }
        }
        return std::nullopt;
    };
    static constexpr GameInput kTargets[static_cast<std::size_t>(ButtonIcon::Pad)] = {
        GameInput::A, GameInput::B, GameInput::C, GameInput::Z, GameInput::One, GameInput::Two, GameInput::Plus,
        GameInput::Minus, GameInput::Home};
    for (std::size_t i = 0; i < std::size(kTargets); ++i) {
        const bool b = i == static_cast<std::size_t>(ButtonIcon::B);
        const bool mouse_first = b || (i == static_cast<std::size_t>(ButtonIcon::A) && menu);
        std::optional<InputSource> input = b && menu ? first(GameInput::JumpBack, mouse_first) : std::nullopt;
        if (!input) input = first(kTargets[i], mouse_first);
        const std::optional<std::uint8_t> icon = input ? key_icon(*input) : std::nullopt;
        spec.glyphs[i] = icon ? ButtonGlyph::Key : ButtonGlyph::Keep;
        spec.places[i] = icon.value_or(0u);
        if (icon == kKeyIconOther) spec.other[i] = key_icon_name(input->code);
    }
    static constexpr GameInput kPad[4] = {GameInput::DpadUp, GameInput::DpadDown, GameInput::DpadLeft,
                                          GameInput::DpadRight};
    bool any = false;
    for (std::size_t i = 0; i < 4u; ++i) {
        const std::optional<InputSource> input = first(kPad[i]);
        const std::optional<std::uint8_t> icon = input ? key_icon(*input) : std::nullopt;
        spec.pad_keys[i] = icon.value_or(kKeyIconNone);
        any = any || icon.has_value();
    }
    spec.glyphs[static_cast<std::size_t>(ButtonIcon::Pad)] = any ? ButtonGlyph::Dpad : ButtonGlyph::Keep;
    return spec;
}

}  // namespace

ButtonIconSpec button_icon_spec(ButtonIconStyle style, const BindingTable& table, bool menu,
                                ButtonIconModel model) noexcept {
    if (style == ButtonIconStyle::Keyboard) return keyboard_icon_spec(table, menu);
    ButtonIconSpec spec;
    spec.style = style;
    if (style == ButtonIconStyle::Game) return spec;
    if ((model == ButtonIconModel::Sixaxis && style == ButtonIconStyle::PlayStation) ||
        (model == ButtonIconModel::DualSenseEdge && style == ButtonIconStyle::PlayStation) ||
        (model == ButtonIconModel::GameCube && style == ButtonIconStyle::Nintendo) ||
        (model == ButtonIconModel::Steam && style == ButtonIconStyle::Xbox)) {
        spec.model = model;
    }
    // The Control Pad looks alike on every controller: the game's own stays (Keep). HOME is resolved after these.
    static constexpr GameInput kTargets[static_cast<std::size_t>(ButtonIcon::Home)] = {
        GameInput::A, GameInput::B, GameInput::C, GameInput::Z, GameInput::One, GameInput::Two, GameInput::Plus,
        GameInput::Minus};
    // The Fn buttons (grip places 4 and 5) have a cell on the Edge model alone; elsewhere the game's own icon stays.
    const bool fn_cells = spec.model == ButtonIconModel::DualSenseEdge;
    const auto first = [&table, fn_cells](GameInput target) {
        for (const InputSource source : table[target]) {
            const GlyphPlace found = glyph_of(source);
            if (found.glyph == ButtonGlyph::Keep) continue;
            if (found.glyph == ButtonGlyph::Grip && found.place >= 4u && !fn_cells) continue;
            return found;
        }
        return GlyphPlace{};
    };
    for (std::size_t i = 0; i < std::size(kTargets); ++i) {
        GlyphPlace found = i == static_cast<std::size_t>(ButtonIcon::B) && menu ? first(GameInput::JumpBack) : GlyphPlace{};
        if (found.glyph == ButtonGlyph::Keep) found = first(kTargets[i]);
        spec.glyphs[i] = found.glyph;
        spec.places[i] = found.place;
    }
    // HOME shows the input it is bound to. On a controller's own home button the game's HOME stays (every pad's reads as
    // one), but for the Steam model's Steam button.
    const GlyphPlace home = first(GameInput::Home);
    if (home.glyph != ButtonGlyph::Home || spec.model == ButtonIconModel::Steam) {
        spec.glyphs[static_cast<std::size_t>(ButtonIcon::Home)] = home.glyph;
        spec.places[static_cast<std::size_t>(ButtonIcon::Home)] = home.place;
    }
    return spec;
}

}  // namespace galaxy::input
