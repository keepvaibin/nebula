#pragma once

// Tilt sections (Star Ball, ray surfing): the game steers them with the Wii
// Remote accelerometer only (SphereAccelSensorController::clacXY 0x80330230,
// SurfRay::updateRotate 0x803327C4, both through MR::getCorePadAcceleration;
// SpherePadController's stick path is overridden), so a controller without
// that sensor cannot play them. While one is active the virtual remote's
// accelerometer reports a gravity direction built from the tilt of the
// controller in use when it has motion sensors (Pro Controller, Pro
// Controller 2) or else from the Nunchuk stick (Xbox left stick, keyboard
// WASD); elsewhere it stays at rest.
//
// The game's vector is the remote's calibrated raw reading
// (WPadAcceleration::update 0x803AA9D0: (-kpad.x, kpad.z, -kpad.y)), the
// gravity reaction in the remote frame: +X out of the left side, +Y toward the
// expansion port, +Z out of the A face (Wiibrew calibration poses).
// - Star Ball: held upright like a joystick (TamakoroTutorial waits for
//   (0,-1,0) within 30 degrees, 0x8033B460). clacXY's neutral leans the top
//   10 degrees away from the face; X and Y are the lean beyond 5 degrees,
//   full at 25, right and forward positive (calcDirSphereMove: X camera
//   right, Y camera forward).
// - Ray: held flat, pointing at the screen, twisted to steer
//   (updateRotate uses X and Z; Z <= -0.7 with |X| <= 0.15 is no turn).
// The controller's normal hold (captured when the section starts, or with
// the recenter button) maps to the section's neutral; tilting the controller
// right or forward is the same gesture on the remote.

#include <array>
#include <cstdint>

namespace galaxy::input::tilt {

enum class Section : std::uint8_t { None, StarBall, Ray };
[[nodiscard]] const char* section_name(Section section) noexcept;

using Vec3 = std::array<float, 3>;

// The rotation that takes `neutral_up` to (0, 1, 0), applied to `up`: the
// controller's tilt relative to its neutral hold, as if the neutral were
// flat (SDL frame: +X right, +Y out of the face, +Z toward the player).
[[nodiscard]] Vec3 relative_up(const Vec3& up, const Vec3& neutral_up) noexcept;

// A virtual controller tilted from flat by `roll` (right side down positive)
// and `pitch` (far edge down positive), in degrees.
[[nodiscard]] Vec3 up_from_angles(float roll_degrees, float pitch_degrees) noexcept;

// Stick (x right, y up = forward, -1..1) to the tilt it stands for in a
// section: Star Ball full deflection = 25 degrees (clacXY's full range),
// ray full deflection = 50 degrees of twist.
inline constexpr float kStarBallStickDegrees = 25.0f;
inline constexpr float kRayStickDegrees = 50.0f;
[[nodiscard]] Vec3 up_from_stick(Section section, float x, float y) noexcept;

// The remote's reading (game vector, unit gravity reaction) for a relative
// controller up vector in a section.
inline constexpr float kStarBallLeanDegrees = 10.0f;
[[nodiscard]] Vec3 remote_gravity(Section section, const Vec3& relative) noexcept;

// 10-bit accelerometer counts for the snapshot: the inverse of
// synthetic_kpad_motion.h (100 counts per g, rest (0x1FC, 0x204, 0x264) for
// the remote lying flat).
struct Counts {
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint16_t z = 0;
};
[[nodiscard]] Counts accel_counts(const Vec3& game_vector) noexcept;

// Section state: captures the neutral when a section starts (or on
// recenter) and produces the reading per report.
struct Tilt {
    Section section = Section::None;
    Vec3 neutral{0.0f, 1.0f, 0.0f};
    bool neutral_valid = false;
    // Inputs: the active section, the up vector of the controller in use when
    // it has motion sensors (then only its tilt steers), the Nunchuk stick
    // (steers otherwise), whether the recenter button was pressed this
    // report. Inactive outside a section (the caller keeps the rest reading).
    struct Output {
        bool active = false;
        bool from_stick = false;
        Vec3 game{};
    };
    Output update(Section active, const Vec3* controller_up, float stick_x, float stick_y,
                  bool recenter) noexcept;
};
// A stick counts as steering above this deflection.
inline constexpr float kStickSteer = 0.15f;

}  // namespace galaxy::input::tilt
