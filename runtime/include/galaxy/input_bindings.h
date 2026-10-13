#pragma once

// Remappable controls: one model for every scheme (keyboard+mouse, Xbox via
// XInput, Switch Pro Controller, Switch 2 Pro Controller, PlayStation
// controllers, Steam Controller). A scheme is a controller family: DualShock 4,
// DualSense and DualSense Edge share `playstation`. Each Wii input (target)
// holds up to kBindingSlots physical inputs (sources). Persisted in
// runtime_settings.ini as one `bind.<scheme>.<target>=<source>, ...` line per
// target and `tune.<scheme>.<name>=<value>` lines for stick/motion tuning.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace galaxy::input {

enum class ControlScheme : std::uint8_t {
    Keyboard = 0,
    Xbox = 1,
    SwitchPro = 2,
    SwitchPro2 = 3,
    PlayStation = 4,
    Steam = 5,
};
inline constexpr std::size_t kControlSchemeCount = 6;

enum class WiiTarget : std::uint8_t {
    A,
    B,
    One,
    Two,
    Plus,
    Minus,
    Home,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    StickUp,
    StickDown,
    StickLeft,
    StickRight,
    C,
    Z,
    Shake,
    NunchukShake,
    PointerUp,
    PointerDown,
    PointerLeft,
    PointerRight,
    PointerRecenter,
    // Wii A while the player controls Mario, Wii B in menus (pointer_control.h).
    JumpBack,
};
inline constexpr std::size_t kWiiTargetCount = 25;

[[nodiscard]] constexpr std::uint32_t wii_target_bit(WiiTarget target) noexcept {
    return 1u << static_cast<unsigned>(target);
}

// Stick and pointer directions take an analog magnitude; every other target
// is a level.
[[nodiscard]] constexpr bool wii_target_is_analog(WiiTarget target) noexcept {
    return (target >= WiiTarget::StickUp && target <= WiiTarget::StickRight) ||
           (target >= WiiTarget::PointerUp && target <= WiiTarget::PointerRight);
}

enum class SourceKind : std::uint8_t {
    None = 0,
    Key = 1,           // code: Win32 virtual-key
    Mouse = 2,         // code: MouseButton
    Button = 3,        // code: unified pad button bit index (pad::k*Bit)
    AxisPositive = 4,  // code: PadAxis
    AxisNegative = 5,  // code: PadAxis (sticks only)
};

enum class MouseButton : std::uint8_t { Left, Right, Middle, X1, X2 };
inline constexpr std::size_t kMouseButtonCount = 5;

struct InputSource {
    SourceKind kind = SourceKind::None;
    std::uint8_t code = 0;
    [[nodiscard]] constexpr bool empty() const noexcept {
        return kind == SourceKind::None;
    }
    friend constexpr bool operator==(InputSource, InputSource) noexcept = default;
};

inline constexpr std::size_t kBindingSlots = 4;
using TargetBindings = std::array<InputSource, kBindingSlots>;

struct BindingTable {
    std::array<TargetBindings, kWiiTargetCount> targets{};
    [[nodiscard]] TargetBindings& operator[](WiiTarget t) noexcept {
        return targets[static_cast<std::size_t>(t)];
    }
    [[nodiscard]] const TargetBindings& operator[](WiiTarget t) const noexcept {
        return targets[static_cast<std::size_t>(t)];
    }
    friend bool operator==(const BindingTable&, const BindingTable&) = default;
};

// Unified gamepad state. Bits 0..15 are the XInput wButtons layout, so an
// XINPUT_GAMEPAD word is a valid button mask; the two reserved XInput bits
// carry Home/Capture and bits 16..22 the extra buttons of the Pro Controller 2,
// the DualSense family and the Steam Controller. Face buttons are positional:
// South is Xbox A, Nintendo B and PlayStation Cross.
namespace pad {
inline constexpr unsigned kDpadUpBit = 0;
inline constexpr unsigned kDpadDownBit = 1;
inline constexpr unsigned kDpadLeftBit = 2;
inline constexpr unsigned kDpadRightBit = 3;
inline constexpr unsigned kStartBit = 4;   // Xbox Menu, Nintendo Plus
inline constexpr unsigned kBackBit = 5;    // Xbox View, Nintendo Minus
inline constexpr unsigned kLeftStickBit = 6;
inline constexpr unsigned kRightStickBit = 7;
inline constexpr unsigned kLeftShoulderBit = 8;   // LB / L
inline constexpr unsigned kRightShoulderBit = 9;  // RB / R
inline constexpr unsigned kHomeBit = 10;     // Guide, Home, PS button, Steam button
inline constexpr unsigned kCaptureBit = 11;  // Capture, touchpad click, quick access button
inline constexpr unsigned kSouthBit = 12;
inline constexpr unsigned kEastBit = 13;
inline constexpr unsigned kWestBit = 14;
inline constexpr unsigned kNorthBit = 15;
inline constexpr unsigned kGripLeftBit = 16;   // Pro Controller 2 GL, Edge left paddle, Steam L4
inline constexpr unsigned kGripRightBit = 17;  // Pro Controller 2 GR, Edge right paddle, Steam R4
inline constexpr unsigned kChatBit = 18;       // Pro Controller 2 C, DualSense mute
inline constexpr unsigned kGripLeft2Bit = 19;   // Steam L5
inline constexpr unsigned kGripRight2Bit = 20;  // Steam R5
inline constexpr unsigned kLeftPadBit = 21;     // Steam left trackpad click
inline constexpr unsigned kRightPadBit = 22;    // Steam right trackpad click
inline constexpr unsigned kButtonBitCount = 23;
[[nodiscard]] constexpr std::uint32_t bit(unsigned index) noexcept {
    return 1u << index;
}
}  // namespace pad

enum class PadAxis : std::uint8_t {
    LeftX,
    LeftY,   // up positive
    RightX,
    RightY,  // up positive
    LeftTrigger,   // 0..1
    RightTrigger,  // 0..1
};
inline constexpr std::size_t kPadAxisCount = 6;

struct PadSample {
    bool connected = false;
    ControlScheme scheme = ControlScheme::Xbox;
    std::uint32_t buttons = 0;
    // Sticks -1..1 after the scheme's deadzone; triggers 0..1.
    std::array<float, kPadAxisCount> axes{};
};

// Keyboard levels for all 256 virtual keys plus mouse buttons.
struct KeyboardMouseLevels {
    std::array<std::uint64_t, 4> keys{};
    std::uint8_t mouse = 0;  // bit per MouseButton
    void set_key(std::uint8_t vk, bool down) noexcept {
        const std::uint64_t mask = std::uint64_t{1} << (vk & 63u);
        if (down) keys[vk >> 6u] |= mask; else keys[vk >> 6u] &= ~mask;
    }
    [[nodiscard]] bool key(std::uint8_t vk) const noexcept {
        return ((keys[vk >> 6u] >> (vk & 63u)) & 1u) != 0u;
    }
    [[nodiscard]] bool mouse_button(MouseButton b) const noexcept {
        return ((mouse >> static_cast<unsigned>(b)) & 1u) != 0u;
    }
};

enum class MotionShake : std::uint8_t { Off, Remote, Nunchuk };
inline constexpr std::uint8_t kShakeSensitivitySteps = 5;

struct SchemeTuning {
    float stick_deadzone = 0.0f;    // fraction of full deflection
    float pointer_deadzone = 0.0f;  // fraction of full deflection
    float pointer_speed = 1.6f;     // pointer units per second at full input
    bool invert_pointer_y = false;
    MotionShake motion_shake = MotionShake::Off;
    std::uint8_t shake_sensitivity = 3;  // 1 (strong shake) .. 5 (light)
    bool gyro_pointer = false;
    float gyro_sensitivity = 1.0f;  // 1: a picture width per 40 degrees of turn
    // A controller pointer that stays within pointer_hide_radius (pointer
    // units) of where it settled for pointer_hide_seconds disappears; a gyro
    // pointer comes back with recenter, a stick pointer also when moved;
    // 0 seconds never hides (pointer_control.h PointerRestHide).
    float pointer_hide_seconds = 0.0f;
    float pointer_hide_radius = 0.12f;
    // Controllers: vibration on/off and strength in percent, 10..100 in steps
    // of 10 (rumble.h).
    bool vibration = true;
    std::uint8_t rumble = 50;
    friend bool operator==(const SchemeTuning&, const SchemeTuning&) = default;
};

struct ControlsConfig {
    std::array<BindingTable, kControlSchemeCount> tables{};
    std::array<SchemeTuning, kControlSchemeCount> tuning{};
    [[nodiscard]] BindingTable& table(ControlScheme s) noexcept {
        return tables[static_cast<std::size_t>(s)];
    }
    [[nodiscard]] const BindingTable& table(ControlScheme s) const noexcept {
        return tables[static_cast<std::size_t>(s)];
    }
    [[nodiscard]] SchemeTuning& tune(ControlScheme s) noexcept {
        return tuning[static_cast<std::size_t>(s)];
    }
    [[nodiscard]] const SchemeTuning& tune(ControlScheme s) const noexcept {
        return tuning[static_cast<std::size_t>(s)];
    }
    friend bool operator==(const ControlsConfig&, const ControlsConfig&) = default;
};

[[nodiscard]] constexpr bool scheme_is_gamepad(ControlScheme s) noexcept {
    return s != ControlScheme::Keyboard;
}
[[nodiscard]] constexpr bool scheme_has_motion(ControlScheme s) noexcept {
    return s == ControlScheme::SwitchPro || s == ControlScheme::SwitchPro2 ||
           s == ControlScheme::PlayStation || s == ControlScheme::Steam;
}
// Switch, PlayStation and Steam controllers are read from their HID reports
// (controller_hid.h); Xbox controllers through XInput.
[[nodiscard]] constexpr bool scheme_uses_hid(ControlScheme s) noexcept {
    return s != ControlScheme::Keyboard && s != ControlScheme::Xbox;
}
// The unified button bits a scheme's controllers have; the others never bind.
// Xbox has no readable Guide/Share (XInput), the Pro Controller has no grips.
[[nodiscard]] constexpr std::uint32_t scheme_button_mask(ControlScheme s) noexcept {
    constexpr std::uint32_t kBase = (1u << pad::kHomeBit) - 1u;
    constexpr std::uint32_t kHomeCapture = pad::bit(pad::kHomeBit) | pad::bit(pad::kCaptureBit);
    constexpr std::uint32_t kFace = 0xf000u;
    constexpr std::uint32_t kGrips = pad::bit(pad::kGripLeftBit) | pad::bit(pad::kGripRightBit);
    switch (s) {
    case ControlScheme::Keyboard: return 0u;
    case ControlScheme::Xbox: return kBase | kFace;
    case ControlScheme::SwitchPro: return kBase | kHomeCapture | kFace;
    case ControlScheme::SwitchPro2: return kBase | kHomeCapture | kFace | kGrips | pad::bit(pad::kChatBit);
    case ControlScheme::PlayStation: return kBase | kHomeCapture | kFace | kGrips | pad::bit(pad::kChatBit);
    case ControlScheme::Steam:
        return kBase | kHomeCapture | kFace | kGrips | pad::bit(pad::kGripLeft2Bit) |
               pad::bit(pad::kGripRight2Bit) | pad::bit(pad::kLeftPadBit) | pad::bit(pad::kRightPadBit);
    }
    return 0u;
}

[[nodiscard]] BindingTable default_binding_table(ControlScheme scheme);
// The defaults of controls versions 1, 2 and 3, kept for migration.
[[nodiscard]] BindingTable legacy_binding_table(ControlScheme scheme);
[[nodiscard]] BindingTable version2_binding_table(ControlScheme scheme);
[[nodiscard]] BindingTable version3_binding_table(ControlScheme scheme);
[[nodiscard]] SchemeTuning legacy_scheme_tuning(ControlScheme scheme);
// runtime_settings.ini `controls_version`; files without it are version 1.
inline constexpr unsigned kControlsVersion = 5;
// Version 3 still-pointer defaults (pads), replaced in version 4.
inline constexpr float kVersion3PointerHideSeconds = 4.0f;
inline constexpr float kVersion3PointerHideRadius = 0.03f;
// Version 4 still-pointer time and vibration strength, replaced in version 5.
inline constexpr float kVersion4PointerHideSeconds = 2.0f;
inline constexpr std::uint8_t kVersion4Rumble = 100;
// For a file of `version`: every binding list that still equals an older
// version's default takes the current default, the gyro switch of a version-1
// file, the still-pointer timing of a version-3 or 4 file and the vibration
// strength of a version-4 file their new defaults; values the player changed
// stay. Returns the number of entries changed.
unsigned migrate_legacy_controls(ControlsConfig& config, unsigned version) noexcept;
[[nodiscard]] SchemeTuning default_scheme_tuning(ControlScheme scheme);
[[nodiscard]] ControlsConfig default_controls_config();

// Stable ini identifiers and overlay labels.
[[nodiscard]] const char* control_scheme_id(ControlScheme scheme) noexcept;
[[nodiscard]] const char* control_scheme_label(ControlScheme scheme) noexcept;
[[nodiscard]] const char* wii_target_id(WiiTarget target) noexcept;
[[nodiscard]] const char* wii_target_label(WiiTarget target) noexcept;
[[nodiscard]] std::optional<ControlScheme> parse_control_scheme(std::string_view id) noexcept;
[[nodiscard]] std::optional<WiiTarget> parse_wii_target(std::string_view id) noexcept;

// Textual source names are per scheme: "A" on a Switch scheme is the east
// face button, on Xbox the south one. Keyboard names are key names
// ("Space", "LShift", "W", "Num1") and "MouseLeft".."Mouse5".
[[nodiscard]] std::string format_input_source(ControlScheme scheme, InputSource source);
[[nodiscard]] std::optional<InputSource> parse_input_source(
    ControlScheme scheme, std::string_view text);
// "None" for an empty list.
[[nodiscard]] std::string format_target_bindings(
    ControlScheme scheme, const TargetBindings& bindings);
// Comma-separated; unknown names fail the whole line (bindings unchanged).
[[nodiscard]] bool parse_target_bindings(
    ControlScheme scheme, std::string_view text, TargetBindings& bindings);

// Whether `source` may be bound to `target` in `scheme` (device class, analog
// targets refuse mouse buttons, Xbox has no readable Guide/Share, only the
// Pro Controller 2 has GL/GR/C).
[[nodiscard]] bool binding_source_allowed(
    ControlScheme scheme, WiiTarget target, InputSource source) noexcept;
// Appends `source` unless already present; a full list drops its last slot.
void add_binding(TargetBindings& bindings, InputSource source) noexcept;

// `key` is the full ini key (e.g. "bind.xbox.a", "tune.switch_pro.motion").
// Returns false for keys this model does not own. Malformed values keep the
// previous value.
bool apply_controls_setting(ControlsConfig& config, std::string_view key, std::string_view value);
// All `bind.*` and `tune.*` lines, '\n'-terminated, C locale.
[[nodiscard]] std::string format_controls_settings(const ControlsConfig& config);
[[nodiscard]] ControlsConfig sanitize_controls_config(ControlsConfig config) noexcept;

// What a scheme's physical inputs ask of the Wii Remote + Nunchuk this report.
struct LogicalInput {
    std::uint32_t pressed = 0;  // wii_target_bit per active target
    float stick_x = 0.0f;
    float stick_y = 0.0f;       // up positive
    float pointer_x = 0.0f;
    float pointer_y = 0.0f;     // up positive
    [[nodiscard]] bool held(WiiTarget target) const noexcept {
        return (pressed & wii_target_bit(target)) != 0u;
    }
};

inline constexpr float kStickDirectionPress = 0.5f;
// Digital targets fed by a stick release below this once pressed, so a stick
// resting near kStickDirectionPress cannot chatter (a held D-pad is one press).
inline constexpr float kStickDirectionRelease = 0.35f;

[[nodiscard]] LogicalInput evaluate_keyboard_bindings(
    const BindingTable& table, const KeyboardMouseLevels& levels) noexcept;
// `trigger_threshold` (0..1) is the trigger press level for digital targets.
[[nodiscard]] LogicalInput evaluate_pad_bindings(
    const BindingTable& table, const PadSample& sample, float trigger_threshold) noexcept;
// `previous_pressed` (the last report's `pressed`) gives stick-fed digital
// targets their release hysteresis. A stick axis bound both to a pointer
// direction and to another target is the pointer when `gyro_pointer` is false
// and the other target when it is true (Switch: right stick = camera while the
// gyro points).
[[nodiscard]] LogicalInput evaluate_pad_bindings(
    const BindingTable& table, const PadSample& sample, float trigger_threshold,
    std::uint32_t previous_pressed, bool gyro_pointer) noexcept;
// Buttons OR; each analog axis keeps the larger magnitude.
[[nodiscard]] LogicalInput merge_logical_input(const LogicalInput& a, const LogicalInput& b) noexcept;
// Wii targets driven only by mouse buttons in `levels` (for the gated
// mouse-button path).
[[nodiscard]] std::uint32_t mouse_bound_targets(
    const BindingTable& table, std::uint8_t mouse_buttons) noexcept;

// Distinct virtual keys a keyboard table reads, in ascending order.
struct KeyList {
    std::array<std::uint8_t, kWiiTargetCount * kBindingSlots> keys{};
    std::size_t count = 0;
};
[[nodiscard]] KeyList referenced_keys(const BindingTable& table) noexcept;

// The overlay chord/navigation word (native_settings_pad.h, XInput layout) for
// a unified button mask. Nintendo controllers swap the face buttons so their
// labelled A changes and B closes, as on the console.
[[nodiscard]] constexpr std::uint16_t settings_pad_buttons(
    ControlScheme scheme, std::uint32_t buttons) noexcept {
    std::uint32_t word = buttons & 0xffffu;
    if (scheme == ControlScheme::SwitchPro || scheme == ControlScheme::SwitchPro2) {
        const std::uint32_t south = (word >> pad::kSouthBit) & 1u;
        const std::uint32_t east = (word >> pad::kEastBit) & 1u;
        const std::uint32_t west = (word >> pad::kWestBit) & 1u;
        const std::uint32_t north = (word >> pad::kNorthBit) & 1u;
        word &= ~(pad::bit(pad::kSouthBit) | pad::bit(pad::kEastBit) |
                  pad::bit(pad::kWestBit) | pad::bit(pad::kNorthBit));
        word |= (east << pad::kSouthBit) | (south << pad::kEastBit) |
                (north << pad::kWestBit) | (west << pad::kNorthBit);
    }
    return static_cast<std::uint16_t>(word);
}
// Inverse of settings_pad_buttons (the swap is its own inverse).
[[nodiscard]] constexpr std::uint32_t unified_from_settings_pad(
    ControlScheme scheme, std::uint16_t word, std::uint32_t extra_bits) noexcept {
    return static_cast<std::uint32_t>(settings_pad_buttons(scheme, word)) |
           (extra_bits & ~0xffffu);
}

// Binding capture: the first input that became active between `baseline`
// and `now` (button press, stick past 0.6 in a direction, trigger past 0.5).
[[nodiscard]] std::optional<InputSource> detect_pad_capture(
    const PadSample& baseline, const PadSample& now) noexcept;

// Process-wide controls (runtime_settings.cpp loads and saves them with the
// rest of runtime_settings.ini). The generation changes on every set so a
// consumer can cache a copy and refresh it with one atomic load per report.
[[nodiscard]] ControlsConfig get_controls_config();
void set_controls_config(const ControlsConfig& config);
[[nodiscard]] std::uint64_t controls_config_generation() noexcept;

}  // namespace galaxy::input
