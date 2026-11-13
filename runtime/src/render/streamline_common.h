// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// What DLSS is told on either backend.

#include <sl.h>
#include <sl_consts.h>
#include <sl_dlss.h>
#include <sl_dlss_d.h>

#include "galaxy/render/halton.h"

#include <algorithm>
#include <cmath>

namespace galaxy::render::streamline_common {

inline sl::DLSSMode dlss_mode(unsigned quality) {
    switch (quality) {
    case 0: return sl::DLSSMode::eDLAA;
    case 1:
    case 5: return sl::DLSSMode::eMaxQuality;  // 5 ultra quality, a mode DLSS does not have
    case 2: return sl::DLSSMode::eBalanced;
    case 3: return sl::DLSSMode::eMaxPerformance;
    default: return sl::DLSSMode::eUltraPerformance;
    }
}

// DLSS programming guide: at least 8 phases times the area ratio.
inline unsigned jitter_phases(unsigned render_width, unsigned output_width) {
    const float ratio = static_cast<float>(output_width) / static_cast<float>(std::max(render_width, 1u));
    return std::max(8u, static_cast<unsigned>(std::ceil(8.0f * ratio * ratio)));
}

inline void jitter(unsigned index, unsigned phase_count, float& x, float& y) {
    const unsigned i = phase_count != 0u ? index % phase_count + 1u : index + 1u;
    x = halton(i, 2u) - 0.5f;
    y = halton(i, 3u) - 0.5f;
}

inline void identity(sl::float4x4& m) {
    for (unsigned r = 0; r < 4u; ++r) {
        m[r] = sl::float4(r == 0u ? 1.0f : 0.0f, r == 1u ? 1.0f : 0.0f, r == 2u ? 1.0f : 0.0f, r == 3u ? 1.0f : 0.0f);
    }
}

// Motion vectors are dense (camera motion included): the matrices only describe the projection.
template <typename Inputs>
void fill_constants(sl::Constants& constants, const Inputs& in) {
    const float aspect = static_cast<float>(in.output_width) / static_cast<float>(std::max(in.output_height, 1u));
    const float y_scale = 1.0f / std::tan(0.5f * in.fov_y);
    identity(constants.cameraViewToClip);
    constants.cameraViewToClip[0].x = y_scale / aspect;
    constants.cameraViewToClip[1].y = y_scale;
    identity(constants.clipToCameraView);
    constants.clipToCameraView[0].x = aspect / y_scale;
    constants.clipToCameraView[1].y = 1.0f / y_scale;
    identity(constants.clipToLensClip);
    identity(constants.clipToPrevClip);
    identity(constants.prevClipToClip);
    constants.jitterOffset = sl::float2(in.jitter_x, in.jitter_y);
    constants.mvecScale = sl::float2(1.0f / static_cast<float>(in.render_width), 1.0f / static_cast<float>(in.render_height));
    constants.cameraPinholeOffset = sl::float2(0.0f, 0.0f);
    constants.cameraPos = sl::float3(0.0f, 0.0f, 0.0f);
    constants.cameraUp = sl::float3(0.0f, 1.0f, 0.0f);
    constants.cameraRight = sl::float3(1.0f, 0.0f, 0.0f);
    constants.cameraFwd = sl::float3(0.0f, 0.0f, 1.0f);
    if (in.view_known) {
        // The view's rows are the camera's right, up and back in world space (orthonormal for the game's cameras); its
        // position is -R^T t. Frame generation's scene change detection reads them.
        const auto& v = in.view;
        constants.cameraRight = sl::float3(v[0], v[1], v[2]);
        constants.cameraUp = sl::float3(v[4], v[5], v[6]);
        constants.cameraFwd = sl::float3(-v[8], -v[9], -v[10]);
        constants.cameraPos = sl::float3(-(v[0] * v[3] + v[4] * v[7] + v[8] * v[11]), -(v[1] * v[3] + v[5] * v[7] + v[9] * v[11]),
                                         -(v[2] * v[3] + v[6] * v[7] + v[10] * v[11]));
    }
    constants.cameraNear = in.camera_near;
    constants.cameraFar = in.camera_far;
    constants.cameraFOV = in.fov_y;
    constants.cameraAspectRatio = aspect;
    constants.depthInverted = sl::Boolean::eTrue;
    constants.cameraMotionIncluded = sl::Boolean::eTrue;
    constants.motionVectors3D = sl::Boolean::eFalse;
    constants.reset = in.reset ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    constants.orthographicProjection = sl::Boolean::eFalse;
    constants.motionVectorsDilated = sl::Boolean::eFalse;
    constants.motionVectorsJittered = sl::Boolean::eFalse;
}

// A world-to-eye matrix (rows 3x4, the eye looking down -z) as Streamline's camera space (+z ahead), for row
// vectors (v' = v M), and its inverse.
inline void camera_matrices(const float (&view)[12], sl::float4x4& world_to_view, sl::float4x4& view_to_world) {
    const float sign[3] = {1.0f, 1.0f, -1.0f};
    float m[4][4] = {};
    for (unsigned c = 0u; c < 3u; ++c) {
        for (unsigned r = 0u; r < 3u; ++r) {
            m[r][c] = sign[c] * view[c * 4u + r];
        }
        m[3][c] = sign[c] * view[c * 4u + 3u];
    }
    m[3][3] = 1.0f;
    // The inverse of a turn and a move: the turn's transpose, the move undone.
    float inv[4][4] = {};
    for (unsigned r = 0u; r < 3u; ++r) {
        for (unsigned c = 0u; c < 3u; ++c) {
            inv[r][c] = m[c][r];
        }
    }
    for (unsigned c = 0u; c < 3u; ++c) {
        inv[3][c] = -(m[3][0] * inv[0][c] + m[3][1] * inv[1][c] + m[3][2] * inv[2][c]);
    }
    inv[3][3] = 1.0f;
    for (unsigned r = 0u; r < 4u; ++r) {
        world_to_view[r] = sl::float4(m[r][0], m[r][1], m[r][2], m[r][3]);
        view_to_world[r] = sl::float4(inv[r][0], inv[r][1], inv[r][2], inv[r][3]);
    }
}

// The eye's projection for DLSS Ray Reconstruction, which reads the specular hit distance through it: Streamline's
// camera space (+z ahead), reversed depth.
template <typename Inputs>
void rr_projection(sl::Constants& constants, const Inputs& in) {
    const float aspect = static_cast<float>(in.output_width) / static_cast<float>(std::max(in.output_height, 1u));
    const float y_scale = 1.0f / std::tan(0.5f * in.fov_y);
    const float n = in.camera_near, f = in.camera_far;
    const float b = n * f / std::max(f - n, 1e-6f), a = -n / std::max(f - n, 1e-6f);
    constants.cameraViewToClip[0] = sl::float4(y_scale / aspect, 0.0f, 0.0f, 0.0f);
    constants.cameraViewToClip[1] = sl::float4(0.0f, y_scale, 0.0f, 0.0f);
    constants.cameraViewToClip[2] = sl::float4(0.0f, 0.0f, a, 1.0f);
    constants.cameraViewToClip[3] = sl::float4(0.0f, 0.0f, b, 0.0f);
    constants.clipToCameraView[0] = sl::float4(aspect / y_scale, 0.0f, 0.0f, 0.0f);
    constants.clipToCameraView[1] = sl::float4(0.0f, 1.0f / y_scale, 0.0f, 0.0f);
    constants.clipToCameraView[2] = sl::float4(0.0f, 0.0f, 0.0f, 1.0f / b);
    constants.clipToCameraView[3] = sl::float4(0.0f, 0.0f, 1.0f, -a / b);
}

}  // namespace galaxy::render::streamline_common
