// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Camera, transform and light motion between two game frames, for the frames presented between them and their motion
// vectors. An eye-space transform splits into the camera V (world to eye) and the object's world transform W = V^-1 M: the
// camera moves as one rigid pose, each W on its own, and the transform at t is V_t W_t. No Windows or GPU includes
// (unit-tested offline).

#include "galaxy/render/affine.h"

#include <array>
#include <cstdint>

namespace galaxy::render {

// The game camera between two frames.
struct CameraMotion {
    Affine34 a{};             // world -> eye at t = 0
    Affine34 b{};             // at t = 1
    Affine34 a_inverse{};     // eye -> world
    Affine34 b_inverse{};
    float qa[4]{};            // rotation of a, and of b on qa's hemisphere
    float qb[4]{};
    float eye_a[3]{};         // camera position in world space
    float eye_b[3]{};
    float theta = 0.0f;       // slerp angle; 0 lerps the quaternions
    float inv_sin_theta = 0.0f;
    float turn_degrees = 0.0f;
    float moved = 0.0f;       // eye distance between a and b
    bool rigid = false;       // both decompose to a rotation: pose interpolation
    bool known = false;       // a and b are both valid cameras
    bool cut = false;         // a and b are not one frame of camera motion
};

// Thresholds of a camera cut (a teleport of the view between two frames).
struct CameraCutLimits {
    float max_turn_degrees = 60.0f;
    // The eye moved further than this many units, and further than
    // `relative` times the previous frame's movement plus `slack`.
    float max_move_units = 1500.0f;
    float relative = 6.0f;
    float slack = 150.0f;
};

// `previous_move` is the eye movement of the frame before (0 if unknown).
void camera_motion(const Affine34* a, const Affine34* b, float previous_move,
                   const CameraCutLimits& limits, CameraMotion& out) noexcept;
[[nodiscard]] Affine34 camera_at(const CameraMotion& m, float t) noexcept;

// One eye-space transform pair (M at t = 0 and t = 1) under the camera.
struct EyeMotion {
    AffineMotion world{};     // W pair (or the M pair when !split)
    bool split = false;       // W = V^-1 M; false: M interpolated directly
    bool still = false;       // W (or M) did not change: V_t W_b
    bool snapped = false;     // implausible as one frame of motion: shows b
    bool flipped = false;     // turned too far (a sprite flip): b's rotation,
                              // position and scale still interpolate
};
// `a` may be null (no match): the object is taken as still in the world.
void eye_motion(const Affine34* a, const Affine34& b, const CameraMotion& camera,
                bool orthographic, EyeMotion& out) noexcept;

[[nodiscard]] Affine34 eye_at(const EyeMotion& m, const CameraMotion& camera,
                              const Affine34& camera_t, float t) noexcept;
// A texture-coordinate translation that changed by more than half a repeat
// wrapped and is unwrapped first; one that still moves by more than this many
// repeats is a discrete step (an atlas cell) and shows b.
inline constexpr float kTexStepLimit = 0.25f;
// One texture matrix (3 rows of 4) at t, decided as a whole. `spatial`
// (generated from position, normal or binormals: projection, environment
// and shadow maps): every element lerps. Otherwise (fed by texture
// coordinates) translations unwrap by whole repeats and the matrix lerps,
// unless any element steps (an atlas cell, a scale jump): then it shows b.
void tex_matrix_at(const float* a, const float* b, bool spatial, float t, float* out) noexcept;

// One frame of an object's motion: its origin moved less than half its distance from the eye plus 200 units, no axis
// scaled by more than 8x, and it turned less than 150 degrees. A larger turn alone is a flip of an object that stays.
enum class Plausibility : std::uint8_t { motion, flip, teleport };
[[nodiscard]] Plausibility plausibility(const AffineMotion& m, float eye_distance) noexcept;
// The motion keeps b's rotation: scale and translation still interpolate.
void take_rotation_of_b(AffineMotion& m) noexcept;

// One light (16 floats, NebulaFrameLight) at t: colour and attenuation lerped; position and direction interpolated in world
// space when `split` (else in eye space). A null `a` is b's light held still in the world.
void light_at(const float* a, const float* b, const CameraMotion& camera, const Affine34& camera_t, float t, bool split,
              float* out) noexcept;

// A unit eye-space direction (three floats) expressed under another view, unit again (sky/ground lighting on generated
// frames); `direction` itself when `from_view` has no inverse or the result has no length.
void eye_direction_at(const float* direction, const Affine34& from_view, const Affine34& to_view, float* out) noexcept;

}  // namespace galaxy::render
