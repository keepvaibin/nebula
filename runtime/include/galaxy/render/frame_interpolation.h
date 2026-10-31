// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Interpolation of one game frame's transform state toward the next, for the
// frames presented between two game frames (and their motion vectors).
//
// Eye-space transforms are split into the game camera V (world to eye) and the
// object's world transform W = V^-1 M. The camera moves as one rigid pose
// (rotation slerp, eye position lerp); each object's W is decomposed and
// interpolated on its own; the sub-frame transform is V_t W_t. A static object
// under a moving camera therefore stays exactly in place, and a moving object
// keeps its own path. Orthographic (screen-space) draws interpolate M directly.
//
// A draw's matrices and lights as one palette of words in the draw_rows numbering (draw_matrices.h): position
// places 0-9 at rows 0-29, texture matrices rows 30-63, normal matrices from row 64 (place k at 64 + 3k),
// post-transform matrices from row 104, lights from row 168 (16 words each).
//
// No Windows/D3D12 includes (unit-tested offline).

#include "galaxy/render/draw_matrices.h"
#include "galaxy/render/transform_history.h"
#include "galaxy/render/uber_constants.h"

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
// Normal matrix (3x3 in the first three columns of an Affine34) pair: the
// rotation is taken out of the camera's like the position matrix's.
struct NormalMotion {
    AffineMotion world{};
    bool split = false;
    bool still = false;
};
void normal_motion(const Affine34* a, const Affine34& b, const CameraMotion& camera,
                   NormalMotion& out) noexcept;

[[nodiscard]] Affine34 normal_at(const NormalMotion& m, const CameraMotion& camera,
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
// A regular texgen (XF 0x1040 + i word) whose source row is the position,
// normal or a binormal.
[[nodiscard]] bool texgen_spatial(std::uint32_t texgen) noexcept;

inline constexpr unsigned kPaletteWords = draw_rows::kRows * 4u;
inline constexpr unsigned kNormalBase = draw_rows::kNormal * 4u;
inline constexpr unsigned kPostBase = draw_rows::kPost * 4u;
inline constexpr unsigned kLightBase = draw_rows::kLight * 4u;

// A palette read by rows: its table (draw_rows::kRows row indices) into its rows (4 words a row), as an
// interpolation scene keeps a draw's blocks. Empty: no palette. A palette held as words reads through the identity table.
struct RowView {
    const std::uint32_t* table = nullptr;
    const std::uint32_t* rows = nullptr;
    RowView() noexcept = default;
    RowView(const std::uint32_t* table_in, const std::uint32_t* rows_in) noexcept : table(table_in), rows(rows_in) {}
    RowView(const std::uint32_t* words) noexcept;  // NOLINT(google-explicit-constructor): words are a view too
    [[nodiscard]] bool empty() const noexcept { return rows == nullptr; }
    [[nodiscard]] const std::uint32_t* row(unsigned r) const noexcept { return rows + 4u * table[r]; }
    // `count` rows from row `first` as words (4 * count into out).
    void gather(unsigned first, unsigned count, std::uint32_t* out) const noexcept;
};
// Whether rows first .. first + count - 1 hold the same words in a and b.
[[nodiscard]] bool same_rows(RowView a, RowView b, unsigned first, unsigned count) noexcept;

// One XF palette snapshot pair (a may be empty: no previous snapshot).
struct PaletteMotion {
    std::array<EyeMotion, 10> position{};
    std::array<NormalMotion, 10> normal{};
    std::array<std::uint8_t, 10> position_changed{};
    std::array<std::uint8_t, 10> normal_changed{};
    bool tex_changed = false;        // rows 30-63 differ
    bool post_changed = false;       // 0x500-0x5FF differ
    std::uint8_t light_changed = 0;  // bit per light
    bool any_changed = false;
    bool split = false;              // lights interpolate in world space
    std::uint32_t snapped = 0;
};
// Only the PNMTX slots in `position_mask`, the normal matrices in
// `normal_mask` (bit k: slot k) and the lights in `light_mask` (bit n: GX
// light n) are interpolated: the parts its draws can read.
inline constexpr std::uint16_t kAllPaletteSlots = 0x3FFu;
void palette_motion(RowView a, RowView b, const CameraMotion& camera,
                    bool orthographic, PaletteMotion& out, std::uint16_t position_mask = kAllPaletteSlots,
                    std::uint16_t normal_mask = kAllPaletteSlots, std::uint8_t light_mask = 0xFFu) noexcept;
// The GX lights a draw with these VS constants evaluates: the light masks of
// its lit colour and alpha channels and the lights of its emboss texgens.
[[nodiscard]] std::uint8_t used_lights(const VertexConstants& constants) noexcept;

// Writes the palette at t: b's words with every changed part interpolated.
void palette_at(const PaletteMotion& m, RowView a, RowView b,
                const CameraMotion& camera, const Affine34& camera_t, float t,
                std::uint32_t* out) noexcept;
// Pixel-stage colours of one draw state between two frames: TEV registers,
// konstant colours, alpha-test references, fog colour (and range when its
// shift is unchanged) and indirect matrices (rows of equal scale). Only when
// the TEV program, texture setup and indirect commands are the same; any
// other difference is a discrete change and shows b.
[[nodiscard]] bool ps_colours_move(const PixelConstants& a, const PixelConstants& b) noexcept;
void ps_colours_at(const PixelConstants& a, const PixelConstants& b, float t, PixelConstants& out) noexcept;
// Channel ambient and material colours (vertex stage).
[[nodiscard]] bool channel_colours_move(const VertexConstants& a, const VertexConstants& b) noexcept;

void channel_colours_at(const VertexConstants& a, const VertexConstants& b, float t, VertexConstants& out) noexcept;
// One GX light (16 XF words) at t: color and attenuation lerped; position and
// direction interpolated in world space when `split` (else in eye space). A
// null `a` (no previous snapshot) is b's light held still in the world.
void light_at(const std::uint32_t* a, const std::uint32_t* b, const CameraMotion& camera,
              const Affine34& camera_t, float t, bool split, std::uint32_t* out) noexcept;
}  // namespace galaxy::render
