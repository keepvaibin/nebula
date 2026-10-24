// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// GPU evaluation of interpolated sub-frame constants.
//
// Once per sequence the classified motion of the playing scene (frame_interpolation.h)
// becomes a program: base copies of the moving constant blocks into one output
// buffer, then one op per moving value (a decomposed transform, a light, a lerp of
// colours or texture matrices, a stored word). Per sub-frame a compute pass runs
// the ops at t; the replay, recorded once per sequence, reads the output. The
// CPU evaluator below is the reference the shader follows (renderer_d3d12.cpp
// kInterpEvalCs).
//
// No Windows/D3D12 includes (unit-tested offline).

#include "galaxy/gx/frame_interpolation.h"
#include "galaxy/gx/uber_constants.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace galaxy::gx::interp_gpu {

enum class OpKind : std::uint32_t {
    Affine = 0,     // 3x4 matrix (eye_at)
    Normal = 1,     // 3x3 normal matrix (normal_at)
    Lerp = 2,       // `count` floats
    LerpRound = 3,  // `count` int32 values rounded half away from zero
    Color8 = 4,     // one RGBA8 word, byte by byte
    Light = 5,      // light position and direction (light_at), 6 floats
    Store = 6,      // `count` words as given
};

// Affine and Normal: the camera at t is applied (world-space interpolation).
inline constexpr std::uint32_t kFlagSplit = 1u << 8u;
// The transform shows b (payload: b).
inline constexpr std::uint32_t kFlagStill = 1u << 9u;
// Polar decomposed (payload: rotations, stretches, translations, slerp angle);
// otherwise element-wise (payload: a, b).
inline constexpr std::uint32_t kFlagRigid = 1u << 10u;
// Light: position and direction interpolate in world space.
inline constexpr std::uint32_t kFlagLightSplit = 1u << 11u;
// At the frame presented before, with its t and camera (motion vector fields).
inline constexpr std::uint32_t kFlagPrev = 1u << 12u;
// Endpoint-exact (Affine, Normal, Light): the payload ends with the value the op must give at t = 0 (EndA) and at
// t = 1 (EndB), as the two game frames have them. The op adds (1 - t)(A - f(0)) + t(B - f(1)) to its value f(t), so
// the rebuilt rotation, the world-space round trip and float rounding leave nothing at either end: at t = 0 a
// sub-frame is the frame before bit for bit, and at t = 1 it meets the next one. Without EndA the op shows b's value
// at t = 0 on purpose (a jump snapped to b, a sprite flip, no frame before).
inline constexpr std::uint32_t kFlagEndA = 1u << 13u;
inline constexpr std::uint32_t kFlagEndB = 1u << 14u;

// The program's first payload words: the camera at t = 0 and at t = 1 (row-major 3x4, for the endpoint values of
// world-space ops), then 0 and 1 as data (the shader evaluates f(0) and f(1) with the same instructions as f(t)).
inline constexpr std::uint32_t kHeaderCamera0 = 0u;
inline constexpr std::uint32_t kHeaderCamera1 = 12u;
inline constexpr std::uint32_t kHeaderZero = 24u;
inline constexpr std::uint32_t kHeaderOne = 25u;
inline constexpr std::uint32_t kHeaderWords = 26u;

[[nodiscard]] constexpr std::uint32_t op_kind(std::uint32_t control) noexcept { return control & 0xFFu; }
[[nodiscard]] constexpr std::uint32_t op_count(std::uint32_t control) noexcept { return control >> 16u; }

struct Op {
    std::uint32_t control = 0;  // kind | flags | count << 16
    std::uint32_t dst = 0;      // word offset in the output buffer
    std::uint32_t payload = 0;  // word offset in Program::payload
    std::uint32_t reserved = 0;
};
static_assert(sizeof(Op) == 16u);

enum class Source : std::uint32_t { Constants = 0, Matrices = 1 };

// A block copied from a ring into the output before the first evaluation of a
// sequence.
struct Copy {
    std::uint32_t source = 0;  // Source
    std::uint32_t src = 0;     // byte offset in the source ring
    std::uint32_t dst = 0;     // byte offset in the output buffer
    std::uint32_t words = 0;
};
static_assert(sizeof(Copy) == 16u);

inline constexpr std::uint32_t kPaletteWords = kXfPaletteWords;
inline constexpr std::uint32_t kPaletteBytes = kPaletteWords * 4u;
inline constexpr std::uint32_t kVsBytes = sizeof(GxVsConstants);
inline constexpr std::uint32_t kPsBytes = sizeof(GxPsConstants);
// The rows a motion vector reads from a palette at the frame presented before
// (position and texture matrices, rows 0-63).
inline constexpr std::uint32_t kPrevPaletteWords = 256u;
inline constexpr std::uint32_t kPrevPaletteBytes = kPrevPaletteWords * 4u;
static_assert(kPaletteBytes % 256u == 0u && kVsBytes % 256u == 0u && kPsBytes % 256u == 0u &&
              kPrevPaletteBytes % 256u == 0u);

// Word offsets inside the blocks.
inline constexpr std::uint32_t kVsProjection = offsetof(GxVsConstants, projection) / 4u;
inline constexpr std::uint32_t kVsAmbient = offsetof(GxVsConstants, chan_ambient) / 4u;
inline constexpr std::uint32_t kVsMaterial = offsetof(GxVsConstants, chan_material) / 4u;
inline constexpr std::uint32_t kVsPaletteBase = offsetof(GxVsConstants, mtx_palette_base) / 4u;
inline constexpr std::uint32_t kVsPosition = offsetof(GxVsConstants, inline_pos_matrix) / 4u;
inline constexpr std::uint32_t kVsTex = offsetof(GxVsConstants, inline_tex_matrices) / 4u;
inline constexpr std::uint32_t kVsPost = offsetof(GxVsConstants, inline_post_matrices) / 4u;
inline constexpr std::uint32_t kVsMotionPrevProj = offsetof(GxVsConstants, motion_prev_proj) / 4u;
inline constexpr std::uint32_t kVsMotionPrevPos = offsetof(GxVsConstants, motion_prev_pos) / 4u;
inline constexpr std::uint32_t kVsMotionCtrl = offsetof(GxVsConstants, motion_ctrl) / 4u;
inline constexpr std::uint32_t kPsTevRegs = offsetof(GxPsConstants, tev_regs) / 4u;
inline constexpr std::uint32_t kPsKonst = offsetof(GxPsConstants, konst) / 4u;
inline constexpr std::uint32_t kPsAlphaRefs = offsetof(GxPsConstants, alpha_refs) / 4u;
inline constexpr std::uint32_t kPsFog = offsetof(GxPsConstants, fog_params) / 4u;
inline constexpr std::uint32_t kPsIndMtx = offsetof(GxPsConstants, ind_mtx) / 4u;

// Output buffer: VS blocks, palettes, the palettes' rows at the frame presented
// before (motion vectors), then PS blocks (each 256-byte aligned).
struct Layout {
    std::uint32_t vs_count = 0;
    std::uint32_t palette_count = 0;
    std::uint32_t ps_count = 0;
    std::uint32_t prev_palette_count = 0;  // 0, or palette_count with motion vectors
    [[nodiscard]] std::uint32_t vs_offset(std::uint32_t slot) const noexcept { return slot * kVsBytes; }
    [[nodiscard]] std::uint32_t palette_base() const noexcept { return vs_count * kVsBytes; }
    [[nodiscard]] std::uint32_t palette_offset(std::uint32_t slot) const noexcept {
        return palette_base() + slot * kPaletteBytes;
    }
    [[nodiscard]] std::uint32_t prev_palette_offset(std::uint32_t slot) const noexcept {
        return palette_base() + palette_count * kPaletteBytes + slot * kPrevPaletteBytes;
    }
    [[nodiscard]] std::uint32_t ps_base() const noexcept {
        return palette_base() + palette_count * kPaletteBytes + prev_palette_count * kPrevPaletteBytes;
    }
    [[nodiscard]] std::uint32_t ps_offset(std::uint32_t slot) const noexcept { return ps_base() + slot * kPsBytes; }
    [[nodiscard]] std::uint32_t bytes() const noexcept { return ps_base() + ps_count * kPsBytes; }
};

class Program {
public:
    Program() { clear(); }
    void clear();
    // Ops added while set are evaluated at the frame presented before (kFlagPrev).
    void at_previous(bool previous) noexcept { flags_ = previous ? kFlagPrev : 0u; }
    // The camera of world-space ops at t = 0 and t = 1 (camera_at of the sequence's camera).
    void set_endpoint_cameras(const Affine34& at_0, const Affine34& at_1);

    // `end_a`, `end_b`: the eye-space matrix (3x4) of the frame before and of this one, when known.
    void eye(std::uint32_t dst, const EyeMotion& m, const float* end_a = nullptr, const float* end_b = nullptr);
    // `end_a`, `end_b`: 3x3 normal matrices.
    void normal(std::uint32_t dst, const NormalMotion& m, const float* end_a = nullptr,
                const float* end_b = nullptr);
    // from/to: `count` floats (count <= 16); `round` stores int32.
    void lerp(std::uint32_t dst, const float* from, const float* to, std::uint32_t count, bool round = false);
    void color8(std::uint32_t dst, std::uint32_t a, std::uint32_t b);
    void store(std::uint32_t dst, const std::uint32_t* words, std::uint32_t count);

    // One texture matrix (3 rows of 4) as tex_matrix_at for 0 < t < 1.
    void tex_matrix(std::uint32_t dst, const float* a, const float* b, bool spatial);
    // The changed parts of one palette as palette_at (dst: the block's first word).
    void palette(std::uint32_t dst, const PaletteMotion& m, const std::uint32_t* a, const std::uint32_t* b,
                 const CameraMotion& camera);
    // Only its changed position matrices.
    void palette_positions(std::uint32_t dst, const PaletteMotion& m, const std::uint32_t* a,
                           const std::uint32_t* b);
    // One GX light (16 words at dst) as light_at; a may be null.
    void light(std::uint32_t dst, const std::uint32_t* a, const std::uint32_t* b, const CameraMotion& camera,
               bool split);
    // Channel colours of a VS block (dst: the block's first word) as channel_colours_at.
    void channel_colours(std::uint32_t dst, const GxVsConstants& a, const GxVsConstants& b);
    // Projection of a VS block as the sub-frame lerp of render_interp_subframe.
    void projection(std::uint32_t dst, const GxVsConstants& a, const GxVsConstants& b);
    // Its GX parameters A..D (motion_prev_proj) the same way.
    void projection_params(std::uint32_t dst, const GxVsConstants& a, const GxVsConstants& b);
    // Pixel-stage colours of a PS block (dst: its first word) as ps_colours_at.
    void ps_colours(std::uint32_t dst, const GxPsConstants& a, const GxPsConstants& b);

    std::vector<Op> ops;
    std::vector<std::uint32_t> payload;

private:
    void transform(OpKind kind, std::uint32_t dst, const AffineMotion& m, bool split, bool still,
                   const float* end_a, const float* end_b);
    void push(OpKind kind, std::uint32_t flags, std::uint32_t count, std::uint32_t dst, std::uint32_t at);
    std::uint32_t append(const float* values, std::uint32_t count);
    std::uint32_t flags_ = 0;
    bool endpoint_cameras_ = false;  // world-space ops take endpoint terms only with them
};

// One presented frame: t and camera_at(camera, t) (row-major 3x4), and the
// frame presented before it for ops flagged kFlagPrev. `previous_only`: only
// those and the stores (the frame at t = 1, whose other values are the base
// copies; stores redirect blocks to the output and hold at every t).
struct FrameTimes {
    Affine34 camera{};
    float t = 0.0f;
    Affine34 camera_prev{};
    float t_prev = 0.0f;
    bool previous_only = false;
};

// The program into `out` (words of the output buffer; base copies done).
void evaluate(const Program& program, const FrameTimes& times, std::uint32_t* out);

// Every op at t.
void evaluate(const Program& program, const Affine34& camera_t, float t, std::uint32_t* out);
}  // namespace galaxy::gx::interp_gpu
