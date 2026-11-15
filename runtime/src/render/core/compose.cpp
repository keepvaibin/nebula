// SPDX-License-Identifier: GPL-3.0-only
#include "render/core/compose.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(_M_X64) || defined(__x86_64__)
#include <emmintrin.h>
#define NEBULA_COMPOSE_SSE 1
#elif defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#define NEBULA_COMPOSE_NEON 1
#endif

namespace galaxy::render::core {

namespace {

// The rule the rows have always been computed by: the three products sum left to right, translation last. Lanes add in the
// same order and nothing fuses, so every path gives the same bits.
inline void concat_row(const float* a, const float (*b)[4], float* out) noexcept {
#if NEBULA_COMPOSE_SSE
    const __m128 sum = _mm_add_ps(_mm_add_ps(_mm_mul_ps(_mm_set1_ps(a[0]), _mm_loadu_ps(b[0])),
                                             _mm_mul_ps(_mm_set1_ps(a[1]), _mm_loadu_ps(b[1]))),
                                  _mm_mul_ps(_mm_set1_ps(a[2]), _mm_loadu_ps(b[2])));
    _mm_storeu_ps(out, sum);
    out[3] += a[3];
#elif NEBULA_COMPOSE_NEON
    const float32x4_t sum = vaddq_f32(vaddq_f32(vmulq_n_f32(vld1q_f32(b[0]), a[0]), vmulq_n_f32(vld1q_f32(b[1]), a[1])),
                                      vmulq_n_f32(vld1q_f32(b[2]), a[2]));
    vst1q_f32(out, sum);
    out[3] += a[3];
#else
    const float a0 = a[0], a1 = a[1], a2 = a[2], a3 = a[3];
    for (unsigned c = 0; c < 3u; ++c) {
        out[c] = a0 * b[0][c] + a1 * b[1][c] + a2 * b[2][c];
    }
    out[3] = a0 * b[0][3] + a1 * b[1][3] + a2 * b[2][3] + a3;
#endif
}

inline void inverse_transpose(const float (*src)[4], float (*dst)[4]) noexcept {
    const float c00 = src[1][1] * src[2][2] - src[2][1] * src[1][2];
    const float c01 = src[1][2] * src[2][0] - src[2][2] * src[1][0];
    const float c02 = src[1][0] * src[2][1] - src[1][1] * src[2][0];
    const float c10 = src[2][1] * src[0][2] - src[0][1] * src[2][2];
    const float c11 = src[2][2] * src[0][0] - src[0][2] * src[2][0];
    const float c12 = src[0][1] * src[2][0] - src[0][0] * src[2][1];
    const float c20 = src[0][1] * src[1][2] - src[1][1] * src[0][2];
    const float c21 = src[0][2] * src[1][0] - src[1][2] * src[0][0];
    const float c22 = src[0][0] * src[1][1] - src[0][1] * src[1][0];
    const float det = src[0][0] * c00 + src[1][0] * c10 + src[2][0] * c20;
    if (det == 0.0f) {
        return;
    }
    const float inv = 1.0f / det;
    dst[0][0] = c00 * inv;
    dst[0][1] = c01 * inv;
    dst[0][2] = c02 * inv;
    dst[1][0] = c10 * inv;
    dst[1][1] = c11 * inv;
    dst[1][2] = c12 * inv;
    dst[2][0] = c20 * inv;
    dst[2][1] = c21 * inv;
    dst[2][2] = c22 * inv;
}

inline void place_as_normal(const float (*place)[4], float (*normal)[4]) noexcept {
    for (unsigned r = 0; r < 3u; ++r) {
        normal[r][0] = place[r][0];
        normal[r][1] = place[r][1];
        normal[r][2] = place[r][2];
        normal[r][3] = 0.0f;
    }
}

inline void billboard(float (*m)[4]) noexcept {
    float x = m[0][0] * m[0][0] + m[1][0] * m[1][0] + m[2][0] * m[2][0];
    float y = m[0][1] * m[0][1] + m[1][1] * m[1][1] + m[2][1] * m[2][1];
    float z = m[0][2] * m[0][2] + m[1][2] * m[1][2] + m[2][2] * m[2][2];
    if (x > 0.0f) x = std::sqrt(x);
    if (y > 0.0f) y = std::sqrt(y);
    if (z > 0.0f) z = std::sqrt(z);
    m[0][0] = x;
    m[0][1] = 0.0f;
    m[0][2] = 0.0f;
    m[2][0] = 0.0f;
    m[2][1] = 0.0f;
    m[1][0] = 0.0f;
    m[1][1] = y;
    m[1][2] = 0.0f;
    m[2][2] = z;
}

inline void billboard_y(float (*m)[4]) noexcept {
    float x = m[0][0] * m[0][0] + m[1][0] * m[1][0] + m[2][0] * m[2][0];
    float z = m[0][2] * m[0][2] + m[1][2] * m[1][2] + m[2][2] * m[2][2];
    if (x > 0.0f) x = std::sqrt(x);
    if (z > 0.0f) z = std::sqrt(z);
    float vy = -m[2][1];
    float vz = m[1][1];
    float r = 1.0f / std::sqrt((0.0f * 0.0f + vz * vz) + vy * vy);
    if (!std::isfinite(r)) {
        // A Y column along X has no direction in the Y-Z plane: the Z axis stands in.
        r = 1.0f;
        vy = 0.0f;
        vz = 1.0f;
    }
    m[0][0] = x;
    m[0][2] = 0.0f;
    m[1][0] = 0.0f;
    m[1][2] = (vy * r) * z;
    m[2][0] = 0.0f;
    m[2][2] = (vz * r) * z;
}

// An envelope: the mix of its joints' world matrices over their inverse binds, and whether all of them are scale free.
// With `skin` (ComposeInput::skin), a joint's product is made once and read by every envelope after.
inline bool weighted_sum(const ModelTables& model, std::uint32_t envelope, const float (*joints)[4],
                         const std::uint32_t* scale_flags, float (*skin)[4], std::uint8_t* skin_made,
                         float (*sum)[4]) noexcept {
    std::memset(sum, 0, 12u * sizeof(float));
    bool flag = true;
    const NebulaModelEnvelope& e = model.envelopes[envelope];
    for (std::uint32_t i = 0; i < e.mix_count; ++i) {
        const NebulaModelMix& mix = model.mixes[e.first_mix + i];
        const float (*inv)[4] = model.inverse_binds + 3u * mix.joint;
        const float (*world)[4] = joints + 3u * mix.joint;
        const float weight = mix.weight;
        // This joint's rows: its world rows times the inverse bind, translation last.
        float own[3][4];
        float (*product)[4] = own;
        bool make = true;
        if (skin != nullptr) {
            product = skin + 3u * mix.joint;
            make = skin_made[mix.joint] == 0u;
            skin_made[mix.joint] = 1u;
        }
        if (make) {
            for (unsigned r = 0; r < 3u; ++r) {
                concat_row(world[r], inv, product[r]);
            }
        }
        for (unsigned r = 0; r < 3u; ++r) {
            const float* row = product[r];
#if NEBULA_COMPOSE_SSE
            _mm_storeu_ps(sum[r], _mm_add_ps(_mm_loadu_ps(sum[r]), _mm_mul_ps(_mm_loadu_ps(row), _mm_set1_ps(weight))));
#elif NEBULA_COMPOSE_NEON
            vst1q_f32(sum[r], vaddq_f32(vld1q_f32(sum[r]), vmulq_n_f32(vld1q_f32(row), weight)));
#else
            for (unsigned c = 0; c < 4u; ++c) {
                sum[r][c] += row[c] * weight;
            }
#endif
        }
        flag = flag && (scale_flags[mix.joint >> 5u] >> (mix.joint & 31u) & 1u) != 0u;
    }
    return flag;
}

inline void scale_normal(float (*normal)[4], const float* scale) noexcept {
    for (unsigned r = 0; r < 3u; ++r) {
        normal[r][0] *= scale[0];
        normal[r][1] *= scale[1];
        normal[r][2] *= scale[2];
    }
}

void compose(const ComposeInput& in, const float (*view)[4], const float (*base)[4], bool turn, float (*out)[4]) noexcept {
    float view_base[3][4];
    if (base != nullptr) {
        if (view != nullptr) {
            concat34(view, base, view_base);
            view = view_base;
        } else {
            view = base;
        }
    }
    const std::uint32_t count = draw_matrices_of(in);
    for (std::uint32_t d = 0; d < count; ++d) {
        float (*place)[4] = out + kRowsPerMatrix * d;
        float (*normal)[4] = place + 3u;
        const bool weighted = in.model != nullptr && in.model->draw_matrices[d].weighted != 0u;
        const std::uint32_t index = in.model != nullptr ? in.model->draw_matrices[d].index : d;
        bool flag;
        if (weighted) {
            float sum[3][4];
            flag = weighted_sum(*in.model, index, in.joints, in.scale_flags, in.skin, in.skin_made, sum);
            if (view != nullptr) {
                concat34(view, sum, place);
            } else {
                std::memcpy(place, sum, sizeof(sum));
            }
        } else {
            const float (*joint)[4] = in.joints + 3u * index;
            flag = (in.scale_flags[index >> 5u] >> (index & 31u) & 1u) != 0u;
            if (view != nullptr) {
                concat34(view, joint, place);
            } else {
                std::memcpy(place, joint, 12u * sizeof(float));
            }
        }
        if (flag) {
            place_as_normal(place, normal);
        } else {
            std::memset(normal, 0, 12u * sizeof(float));
            inverse_transpose(place, normal);
        }
        if (turn && !weighted && in.model != nullptr && in.model->has_billboard) {
            const std::uint8_t kind = in.model->joints[index].billboard;
            if (kind == 1u) {
                billboard(place);
                std::memset(normal, 0, 12u * sizeof(float));
                normal[0][0] = 1.0f / place[0][0];
                normal[1][1] = 1.0f / place[1][1];
                normal[2][2] = 1.0f / place[2][2];
            } else if (kind == 2u) {
                billboard_y(place);
                inverse_transpose(place, normal);
            }
        }
        if (in.normal_scale != nullptr && (in.normal_scale[0] != 0.0f || in.normal_scale[1] != 0.0f ||
                                           in.normal_scale[2] != 0.0f)) {
            scale_normal(normal, in.normal_scale);
        }
    }
}

}  // namespace

void concat34(const float (*a)[4], const float (*b)[4], float (*dst)[4]) noexcept {
    float result[3][4];
    concat_row(a[0], b, result[0]);
    concat_row(a[1], b, result[1]);
    concat_row(a[2], b, result[2]);
    std::memcpy(dst, result, sizeof(result));
}

void compose_rows(const ComposeInput& in, float (*out)[4]) noexcept {
    compose(in, in.view, in.base, true, out);
}

void compose_world_rows(const ComposeInput& in, float (*out)[4]) noexcept {
    compose(in, in.world_view, in.base, false, out);
}

}  // namespace galaxy::render::core
