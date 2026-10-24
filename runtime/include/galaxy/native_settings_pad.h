// The settings overlay's controller chord and navigation (native_host.cpp routes every pad through it).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace galaxy::input {

// Controller access to the native settings overlay. View+Menu (XInput Back+Start) toggles it.
// While it is open, the pad drives it: the D-pad or left stick moves and changes, A changes (or chooses in the HOME Menu),
// Y applies, X reverts, LB/RB switch pages, and B or Menu closes it. The game sees a neutral pad.
// Buttons held across a toggle, or while the overlay is open, stay hidden from the game until
// released, so the chord never reaches the pause menu and the closing press never reaches the
// game. Output keys are the overlay's keyboard keys (Win32 virtual-key codes).
namespace settings_pad {
inline constexpr std::uint16_t kUp = 0x0001u;
inline constexpr std::uint16_t kDown = 0x0002u;
inline constexpr std::uint16_t kLeft = 0x0004u;
inline constexpr std::uint16_t kRight = 0x0008u;
inline constexpr std::uint16_t kStart = 0x0010u;
inline constexpr std::uint16_t kBack = 0x0020u;
inline constexpr std::uint16_t kShoulderLeft = 0x0100u;
inline constexpr std::uint16_t kShoulderRight = 0x0200u;
inline constexpr std::uint16_t kA = 0x1000u;
inline constexpr std::uint16_t kB = 0x2000u;
inline constexpr std::uint16_t kX = 0x4000u;
inline constexpr std::uint16_t kY = 0x8000u;
inline constexpr std::uint16_t kDirections = kUp | kDown | kLeft | kRight;
inline constexpr std::uint16_t kChord = kBack | kStart;

inline constexpr std::uint8_t kKeyToggle = 0x70u;        // VK_F1
inline constexpr std::uint8_t kKeyApply = 0x0Du;         // VK_RETURN
inline constexpr std::uint8_t kKeyRevert = 0x08u;        // VK_BACK
inline constexpr std::uint8_t kKeyClose = 0x1Bu;         // VK_ESCAPE
inline constexpr std::uint8_t kKeyPreviousPage = 0x21u;  // VK_PRIOR
inline constexpr std::uint8_t kKeyNextPage = 0x22u;      // VK_NEXT
inline constexpr std::uint8_t kKeyLeft = 0x25u;
inline constexpr std::uint8_t kKeyUp = 0x26u;
inline constexpr std::uint8_t kKeyRight = 0x27u;
inline constexpr std::uint8_t kKeyDown = 0x28u;
// A: no keyboard key has it (VK_EXECUTE). The settings menu reads it as Right (it changes the setting); the HOME
// Menu's question as choosing, while the D-pad's left and right move between its answers.
inline constexpr std::uint8_t kKeyAccept = 0x2Bu;

inline constexpr std::uint64_t kRepeatDelayMs = 400u;
inline constexpr std::uint64_t kRepeatIntervalMs = 110u;
inline constexpr float kStickPress = 0.6f;
}  // namespace settings_pad

struct NativeSettingsPadState {
    std::uint16_t previous{};  // buttons plus stick directions at the last poll
    std::uint16_t hidden{};    // hidden from the game until released
    std::uint16_t repeat{};    // the held direction that auto-repeats
    std::uint64_t repeat_at_ms{};
};

struct NativeSettingsPadStep {
    std::array<std::uint8_t, 4> keys{};
    std::size_t key_count{};
    std::uint16_t game_buttons{};  // the buttons the game sees
    bool game_neutral{};           // sticks and triggers neutral as well
};

[[nodiscard]] constexpr std::uint8_t native_settings_pad_direction_key(
    std::uint16_t direction) noexcept {
    switch (direction) {
    case settings_pad::kUp: return settings_pad::kKeyUp;
    case settings_pad::kDown: return settings_pad::kKeyDown;
    case settings_pad::kLeft: return settings_pad::kKeyLeft;
    default: return settings_pad::kKeyRight;
    }
}

// One controller poll. `overlay_visible` is the overlay's published state;
// emitted keys reach it asynchronously, and the hidden set covers that gap.
[[nodiscard]] inline NativeSettingsPadStep native_settings_pad_step(
    NativeSettingsPadState& state,
    std::uint16_t buttons,
    float stick_x,
    float stick_y,
    bool overlay_visible,
    std::uint64_t now_ms) noexcept {
    using namespace settings_pad;
    NativeSettingsPadStep step{};
    const auto emit = [&step](std::uint8_t key) {
        if (step.key_count < step.keys.size()) {
            step.keys[step.key_count++] = key;
        }
    };
    std::uint16_t held = buttons;
    if (stick_y >= kStickPress) held |= kUp;
    if (stick_y <= -kStickPress) held |= kDown;
    if (stick_x <= -kStickPress) held |= kLeft;
    if (stick_x >= kStickPress) held |= kRight;
    const auto pressed = static_cast<std::uint16_t>(held & ~state.previous);
    state.previous = held;

    if ((held & kChord) == kChord && (pressed & kChord) != 0u) {
        emit(kKeyToggle);
        state.hidden = buttons;
        state.repeat = 0u;
        step.game_neutral = true;
        return step;
    }
    if (overlay_visible) {
        if ((pressed & (kB | kStart)) != 0u) {
            emit(kKeyClose);
            state.repeat = 0u;
        } else {
            if ((pressed & kShoulderLeft) != 0u) emit(kKeyPreviousPage);
            if ((pressed & kShoulderRight) != 0u) emit(kKeyNextPage);
            if ((pressed & kA) != 0u) emit(kKeyAccept);
            if ((pressed & kY) != 0u) emit(kKeyApply);
            if ((pressed & kX) != 0u) emit(kKeyRevert);
            const auto direction = static_cast<std::uint16_t>(pressed & kDirections);
            if (direction != 0u) {
                state.repeat = static_cast<std::uint16_t>(direction & (~direction + 1u));
                state.repeat_at_ms = now_ms + kRepeatDelayMs;
                emit(native_settings_pad_direction_key(state.repeat));
            } else if (state.repeat != 0u && (held & state.repeat) != 0u) {
                if (now_ms >= state.repeat_at_ms) {
                    emit(native_settings_pad_direction_key(state.repeat));
                    state.repeat_at_ms = now_ms + kRepeatIntervalMs;
                }
            } else {
                state.repeat = 0u;
            }
        }
        state.hidden = buttons;
        step.game_neutral = true;
        return step;
    }
    state.repeat = 0u;
    state.hidden = static_cast<std::uint16_t>(state.hidden & buttons);
    step.game_buttons = static_cast<std::uint16_t>(buttons & ~state.hidden);
    return step;
}

}  // namespace galaxy::input
