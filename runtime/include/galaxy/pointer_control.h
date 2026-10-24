#pragma once

// One Wii Remote pointer for every host source: the mouse (absolute), pointer
// keys and the right stick (velocity), and the gyro (rotation). Whichever source
// moved last owns the pointer; another source takes it over only after the owner
// has been still for kPointerHandoffUs, so a resting stick or a controller lying
// on the desk never fights a moving mouse and the reverse.

#include <array>
#include <cstdint>

namespace galaxy::input {

enum class PointerSource : std::uint8_t { None, Mouse, Keys, Stick, Gyro };

inline constexpr std::uint64_t kPointerHandoffUs = 250'000;

struct PointerActivity {
    bool mouse = false;
    bool keys = false;
    bool stick = false;
    bool gyro = false;
};

struct PointerOwner {
    PointerSource owner = PointerSource::None;
    std::uint64_t last_active_us = 0;
    // Applies one report's activity and returns the owner. Among sources that
    // start together the controller ones win, so a mouse moved by a controller
    // mapper (Steam's desktop layout) cannot take the pointer from the stick.
    PointerSource update(const PointerActivity& activity, std::uint64_t now_us) noexcept;
};

[[nodiscard]] const char* pointer_source_name(PointerSource source) noexcept;
// Radial dead zone with the live range rescaled to 0..1 (XInput's guidance for
// thumbsticks: per-axis dead zones snap the pointer to the axes).
struct StickVector {
    float x = 0.0f;
    float y = 0.0f;
};
[[nodiscard]] StickVector apply_radial_deadzone(float x, float y, float deadzone) noexcept;

// Pointer velocity (pointer units per second, 2 units = picture width; y up
// positive) for a dead-zoned stick vector: response 0.3 s + 0.7 s^3 of the
// deflection s, so small deflections place the pointer finely and full
// deflection crosses the picture in 2 / speed seconds.
[[nodiscard]] StickVector stick_pointer_velocity(StickVector stick, float speed) noexcept;

// Stick and key pointers stay inside the picture so they always come back.
inline constexpr float kStickPointerLimit = 1.0f;
// The gyro pointer may leave the picture by this much (the game hides it, as
// a Wii Remote pointed past the screen) and returns as soon as it turns back.
inline constexpr float kGyroPointerLimit = 1.35f;
inline constexpr float kPointerVisibleLimit = 1.02f;

// As in the game's Switch release, the gyro pointer hides when it is not being used and
// comes back only with R (recenter). The controller-owned pointer, low-passed over
// kPointerHideFilterSeconds so a one-report spike does not count, is compared with an
// anchor: while it stays within `radius` of the anchor a timer runs, and after
// `hide_seconds` the pointer hides. Hand tremor and small jerks inside the radius never
// restart the timer; leaving the radius re-anchors and restarts it. A hidden gyro
// pointer (`latch`) stays hidden until `reveal` (recenter). A hidden stick pointer also
// shows again when it leaves the radius or the stick is pushed. A mouse-owned pointer
// hides by MouseRestHide instead.
//
// The default radius of 0.12 units (6 % of the picture width, 2.4 degrees of turn at
// gyro sensitivity 1) is larger than hand tremor (about 0.025 units).
inline constexpr float kPointerHideFilterSeconds = 0.1f;
inline constexpr float kDefaultPointerHideRadius = 0.12f;

inline constexpr float kDefaultPointerHideSeconds = 4.0f;
struct PointerRestHide {
    bool hidden = false;
    bool anchored = false;
    float anchor_x = 0.0f;
    float anchor_y = 0.0f;
    float filtered_x = 0.0f;
    float filtered_y = 0.0f;
    std::uint64_t since_us = 0;
    std::uint64_t last_us = 0;
    // Returns whether the pointer is hidden after this report.
    bool update(float x, float y, std::uint64_t now_us, bool controller_owned, bool reveal, bool latch,
                float hide_seconds, float radius) noexcept;
};

// Keyboard and mouse, in play: the mouse-owned pointer hides after kMouseHideSeconds without a
// mouse move or a held mouse button, and shows again with either. Any other owner resets it.
inline constexpr float kMouseHideSeconds = 4.5f;

struct MouseRestHide {
    std::uint64_t since_us = 0;
    // Returns whether the pointer is hidden after this report.
    bool update(bool mouse_owned, bool mouse_active, std::uint64_t now_us) noexcept;
};

// The recenter button (Switch R, Xbox right-stick click, keyboard R): the
// press moves the pointer to the picture centre and, while the button is
// held, the pointer stays there, visible, ignoring gyro and stick motion. On
// release the pointer resumes from the centre, so the controller's
// orientation at release becomes the new reference.
struct RecenterHold {
    bool held = false;
    struct Output {
        bool pressed = false;   // this report: move to the centre, take ownership
        bool pinned = false;    // held: keep the pointer at the centre
        bool released = false;  // this report: motion resumes from the centre
    };
    Output update(bool down) noexcept;
};

// StarPointerOnOffController::mMode: the index of the highest-priority
// pointer mode requested this frame, in updateMode's order: 0 HOME button
// menu, 1 error window, 2 pause menu, 3 logo, 4 title, 5 file select,
// 6 scenario select, 7 pause menu, 8 yes/no choice, 9 picture book,
// 10-14 demos, 15 1P invalid / 2P valid, 16-17 sphere selector, 18 launch
// star, 19 command stream, 20-21 blue star, 22 star bit target, 23 big
// bubble, 24 base, 25 game. Menus are the modes of screens with a Back action
// (0-9, 16, 17); everything else, and an unreadable mode, is play.
inline constexpr std::uint32_t kStarPointerModeCount = 26;
enum class PlayContext : std::uint8_t { Menu, Play };
[[nodiscard]] PlayContext play_context_from_star_pointer_mode(std::uint32_t mode) noexcept;

// The jump/back button: Wii A while the player controls Mario, Wii B in menus.
// The choice is made when the button goes down and kept until it is released,
// so a menu opening mid-press never turns a held jump into a back press.
struct JumpBackLatch {
    bool held = false;
    bool as_a = false;
    struct Output {
        bool a = false;
        bool b = false;
    };
    Output update(bool pressed, PlayContext context) noexcept;
};

}  // namespace galaxy::input
