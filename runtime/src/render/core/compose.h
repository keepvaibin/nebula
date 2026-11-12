// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// World matrices into the rows the vertex stage reads: six per draw matrix, three of its place and three of its normal.

#include <nebula_frame_list.h>
#include <nebula_model.h>

#include <cstdint>

namespace galaxy::render::core {

inline constexpr std::uint32_t kRowsPerMatrix = 6u;

// What compose reads of a registered model; the game's block stays mapped while it is registered.
struct ModelTables {
    const NebulaModelJoint* joints = nullptr;
    const NebulaModelDrawMatrix* draw_matrices = nullptr;
    const NebulaModelEnvelope* envelopes = nullptr;
    const NebulaModelMix* mixes = nullptr;
    const float (*inverse_binds)[4] = nullptr;  // three rows per joint
    std::uint32_t draw_matrix_count = 0u;
    std::uint32_t joint_count = 0u;
    bool has_billboard = false;
    bool has_weighted = false;  // a draw matrix mixes joints (an envelope)
};

struct ComposeInput {
    const float (*joints)[4] = nullptr;  // three rows per joint
    const std::uint32_t* scale_flags = nullptr;
    const ModelTables* model = nullptr;  // null: each joint is one draw matrix
    std::uint32_t joint_count = 0u;
    const float (*view)[4] = nullptr;    // null: no camera over the joints
    const float (*base)[4] = nullptr;    // calc mode 2
    const float (*world_view)[4] = nullptr; // eye -> world for joints already in eye space
    const float* normal_scale = nullptr;
    // Envelopes' scratch (null: none): three rows and a byte per model joint, the bytes zeroed by the caller once for the
    // joints. Each joint's world rows times its inverse bind is made at its first mix and read by every later one, in this
    // call and the next with the same joints (an envelope mixes those products alone: the bits are the same either way).
    float (*skin)[4] = nullptr;
    std::uint8_t* skin_made = nullptr;
};

[[nodiscard]] inline std::uint32_t draw_matrices_of(const ComposeInput& in) noexcept {
    return in.model != nullptr ? in.model->draw_matrix_count : in.joint_count;
}

// `out` holds draw_matrices_of(in) * kRowsPerMatrix rows of four floats.
void compose_rows(const ComposeInput& in, float (*out)[4]) noexcept;

// The same matrices without the camera: what coordinates generated from world position and normal read.
void compose_world_rows(const ComposeInput& in, float (*out)[4]) noexcept;

// dst = a * b, a and b three rows of four, last row of each taken as 0 0 0 1.
void concat34(const float (*a)[4], const float (*b)[4], float (*dst)[4]) noexcept;

}  // namespace galaxy::render::core
