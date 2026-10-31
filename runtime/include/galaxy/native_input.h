#pragma once

// A player's controller as the game reads it (PlayerInput: nebula_game_api.h's NebulaInputSample), the virtual
// remote's pointer, stick and motion, and the mouse pointer projection.

#include "galaxy/content_viewport.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace galaxy::input {

// An integer field of the input scripts: ASCII digits only, no sign or
// whitespace, and overflow fails instead of wrapping. `value` is changed only
// on success.
[[nodiscard]] bool parse_native_input_script_u64(
    std::string_view text,
    std::uint64_t& value) noexcept;

// `anchor_vi` plus the script offset `offset_text`, failing on overflow.
// `start_vi` is changed only on success.
[[nodiscard]] bool resolve_native_input_script_start_vi(
    std::uint64_t anchor_vi,
    std::string_view offset_text,
    std::uint64_t& start_vi) noexcept;

struct ButtonState {
    bool left = false;
    bool right = false;
    bool down = false;
    bool up = false;
    bool plus = false;
    bool two = false;
    bool one = false;
    bool b = false;
    bool a = false;
    bool minus = false;
    bool home = false;
    bool c = false;  // the Nunchuk's
    bool z = false;
};

// The buttons as nebula_game_api.h's NEBULA_BUTTON_* bits (the host checks the two agree).
enum : std::uint32_t {
    kButtonA = 1u << 0,
    kButtonB = 1u << 1,
    kButton1 = 1u << 2,
    kButton2 = 1u << 3,
    kButtonPlus = 1u << 4,
    kButtonMinus = 1u << 5,
    kButtonHome = 1u << 6,
    kButtonUp = 1u << 7,
    kButtonDown = 1u << 8,
    kButtonLeft = 1u << 9,
    kButtonRight = 1u << 10,
    kButtonC = 1u << 11,
    kButtonZ = 1u << 12,
};
[[nodiscard]] std::uint32_t button_bits(const ButtonState& buttons) noexcept;

using Gravity = std::array<float, 3>;

// The virtual remote's distance from the screen (sensor bar units): what a remote pointing from the usual place reads,
// 0.2 / 0.383864 (the bar's lights' spacing over the camera's field) over 0.3125 (their spacing in its picture).
inline constexpr float kVirtualPointerDistance = (0.2f / 0.383864f) / 0.3125f;

// One moment of a player's controller.
struct PlayerInput {
    ButtonState buttons{};
    // The Nunchuk stick after its response (nunchuk_stick), -1..1: right, up.
    float stick_x = 0.0f;
    float stick_y = 0.0f;
    // Where the remote points, -1..1 across the picture (right, down), while pointer_on.
    bool pointer_on = false;
    float pointer_x = 0.0f;
    float pointer_y = 0.0f;
    // Its roll as the screen sees it (a unit vector, (1, 0) level) and its distance.
    float roll_x = 1.0f;
    float roll_y = 0.0f;
    float distance = kVirtualPointerDistance;
    // Acceleration in g, x out of the remote's left side, y toward its expansion port, z out of its A face: (0, 0, 1)
    // for one lying still face up. The Nunchuk's in the same axes.
    Gravity gravity{0.0f, 0.0f, 1.0f};
    Gravity nunchuk_gravity{0.0f, 0.0f, 1.0f};
    // Shaken (a shake binding held, or a controller's motion shake).
    bool shake = false;
    bool nunchuk_shake = false;
};

// The pointer at (x, y) (clamped to the picture) while `active`.
void set_pointer(PlayerInput& input, float x, float y, bool active) noexcept;

// A remote sees the screen's sensor bar only while it points within about 45 degrees of level: held upright (the Star
// Ball) or flat on its back it has no pointer. True when `gravity` allows one.
[[nodiscard]] bool remote_faces_screen(const Gravity& gravity) noexcept;

// A stick (-1..1 each, after its own dead zone) as the Nunchuk's: per axis nothing up to 15/101 of the travel, all
// from 71/101, linear between; then no longer than 1. The game's stick thresholds are tuned to this response.
struct StickValue {
    float x = 0.0f;
    float y = 0.0f;
};
[[nodiscard]] StickValue nunchuk_stick(float x, float y) noexcept;
// The same from a Nunchuk's own counts away from its resting place (about 101 to the edge).
[[nodiscard]] StickValue nunchuk_stick_counts(float x, float y) noexcept;

[[nodiscard]] float normalize_xinput_axis(
    std::int16_t raw,
    std::int16_t deadzone) noexcept;

// The picture inside the window client: letterbox and pillarbox pixels are
// not pointer coordinates.
using NativePointerViewport = galaxy::ContentViewport;

struct NativeMousePointerSample {
    // Not used by the projection, which follows the cursor over the content
    // viewport while the window is unfocused too.
    bool window_focused = false;
    bool absolute_valid = false;
    bool inside_client = false;
    int client_x = 0;
    int client_y = 0;
    int client_width = 0;
    int client_height = 0;
    NativePointerViewport viewport{};
    float x_scale = 1.0f;
    float y_scale = 1.0f;
};

struct NativeMousePointerProjection {
    float x = 0.0f;
    float y = 0.0f;
    bool visible = false;
};

// One client-area cursor sample as pointer coordinates, -1..1 across the
// content viewport. A sample outside the client or the viewport is not
// visible; holding the pointer through a lost sample is the caller's choice.
// Scales below 1 lower the gain in the centre band (native_input.cpp).
// Follows Dusklight's menu pointer (CC0, src/dusk/menu_pointer.cpp).
[[nodiscard]] NativeMousePointerProjection project_native_mouse_pointer(
    const NativeMousePointerSample& sample) noexcept;

}  // namespace galaxy::input
