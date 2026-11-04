// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// What DLSS is told on either backend.

#include <sl.h>
#include <sl_consts.h>
#include <sl_dlss.h>

#include <algorithm>
#include <cmath>

namespace galaxy::render::streamline_common {

inline sl::DLSSMode dlss_mode(unsigned quality) {
    switch (quality) {
    case 0: return sl::DLSSMode::eDLAA;
    case 1: return sl::DLSSMode::eMaxQuality;
    case 2: return sl::DLSSMode::eBalanced;
    case 3: return sl::DLSSMode::eMaxPerformance;
    default: return sl::DLSSMode::eUltraPerformance;
    }
}

inline float halton(unsigned index, unsigned base) {
    float result = 0.0f;
    float fraction = 1.0f / static_cast<float>(base);
    while (index > 0u) {
        result += fraction * static_cast<float>(index % base);
        index /= base;
        fraction /= static_cast<float>(base);
    }
    return result;
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

}  // namespace galaxy::render::streamline_common
