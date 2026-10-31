// SPDX-License-Identifier: GPL-3.0-only
// Interpolation programs and their CPU reference evaluator (interp_gpu.h).
#include "galaxy/render/interp_gpu.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace galaxy::render::interp_gpu {
namespace {

constexpr std::uint32_t control(OpKind kind, std::uint32_t flags, std::uint32_t count) noexcept {
    return static_cast<std::uint32_t>(kind) | flags | (count << 16u);
}

float word_float(std::uint32_t w) noexcept { return std::bit_cast<float>(w); }
std::uint32_t float_word(float f) noexcept { return std::bit_cast<std::uint32_t>(f); }

bool finite_all(const float* v, unsigned count) noexcept {
    for (unsigned i = 0; i < count; ++i) {
        if (!std::isfinite(v[i])) return false;
    }
    return true;
}

Affine34 rotation_only(const Affine34& m) noexcept {
    Affine34 r = m;
    r[3] = r[7] = r[11] = 0.0f;
    return r;
}

// Payload layouts (floats): still b[12]; element-wise a[12] b[12]; rigid
// qa[4] qb[4] stretch_a[9] stretch_b[9] translation_a[3] translation_b[3]
// theta inv_sin_theta; then the endpoint values (kFlagEndA, kFlagEndB).
constexpr std::uint32_t kRigidWords = 34u;
constexpr std::uint32_t kLightWords = 14u;

[[nodiscard]] std::uint32_t transform_words(std::uint32_t flags) noexcept {
    return (flags & kFlagStill) != 0u ? 12u : (flags & kFlagRigid) != 0u ? kRigidWords : 24u;
}

Affine34 transform_at(const std::uint32_t* p, std::uint32_t flags, float t) noexcept {
    Affine34 out{};
    if ((flags & kFlagStill) != 0u) {
        for (unsigned i = 0; i < 12u; ++i) out[i] = word_float(p[i]);
        return out;
    }
    if ((flags & kFlagRigid) == 0u) {
        for (unsigned i = 0; i < 12u; ++i) {
            out[i] = word_float(p[i]) * (1.0f - t) + word_float(p[12u + i]) * t;
        }
        return out;
    }
    const float theta = word_float(p[32]);
    const float inv_sin = word_float(p[33]);
    float wa = 1.0f - t, wb = t;
    if (theta != 0.0f) {
        wa = std::sin((1.0f - t) * theta) * inv_sin;
        wb = std::sin(t * theta) * inv_sin;
    }
    float q[4];
    float norm = 0.0f;
    for (unsigned i = 0; i < 4u; ++i) {
        q[i] = word_float(p[i]) * wa + word_float(p[4u + i]) * wb;
        norm += q[i] * q[i];
    }
    norm = 1.0f / std::sqrt(norm);
    for (float& v : q) v *= norm;
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float rot[3][3] = {
        {1.0f - 2.0f * (y * y + z * z), 2.0f * (x * y - z * w), 2.0f * (x * z + y * w)},
        {2.0f * (x * y + z * w), 1.0f - 2.0f * (x * x + z * z), 2.0f * (y * z - x * w)},
        {2.0f * (x * z - y * w), 2.0f * (y * z + x * w), 1.0f - 2.0f * (x * x + y * y)}};
    float s[9];
    for (unsigned i = 0; i < 9u; ++i) {
        const float a = word_float(p[8u + i]);
        s[i] = a + (word_float(p[17u + i]) - a) * t;
    }
    for (unsigned r = 0; r < 3u; ++r) {
        for (unsigned c = 0; c < 3u; ++c) {
            out[r * 4u + c] = rot[r][0] * s[c] + rot[r][1] * s[3u + c] + rot[r][2] * s[6u + c];
        }
        const float a = word_float(p[26u + r]);
        out[r * 4u + 3u] = a + (word_float(p[29u + r]) - a) * t;
    }
    return out;
}

// An Affine or Normal op's value at t with the camera at t, before its endpoint terms.
Affine34 transform_value(const std::uint32_t* p, std::uint32_t flags, OpKind kind, float t,
                         const Affine34& camera) noexcept {
    Affine34 m = transform_at(p, flags, t);
    if (kind == OpKind::Normal) {
        m[3] = m[7] = m[11] = 0.0f;
    }
    if ((flags & kFlagSplit) != 0u) {
        m = affine_multiply(kind == OpKind::Normal ? rotation_only(camera) : camera, m);
    }
    return m;
}

// A light op's position and direction at t (6 floats).
void light_value(const std::uint32_t* p, std::uint32_t flags, float t, const Affine34& camera, float* out) noexcept {
    float w[3], v[3], pos[3], dir[3];
    for (unsigned i = 0; i < 3u; ++i) {
        w[i] = word_float(p[i]) * (1.0f - t) + word_float(p[3u + i]) * t;
        v[i] = word_float(p[6u + i]) * (1.0f - t) + word_float(p[9u + i]) * t;
    }
    if ((flags & kFlagLightSplit) != 0u) {
        affine_transform_point(camera, w, pos);
        affine_transform_point(rotation_only(camera), v, dir);
    } else {
        std::memcpy(pos, w, sizeof(w));
        std::memcpy(dir, v, sizeof(v));
    }
    const float la = word_float(p[12]), lb = word_float(p[13]);
    const float ld = vector_length(dir);
    if (ld > 1e-20f) {
        const float scale = (la * (1.0f - t) + lb * t) / ld;
        for (float& x : dir) x *= scale;
    }
    std::memcpy(out, pos, sizeof(pos));
    std::memcpy(out + 3, dir, sizeof(dir));
}

// value + (1 - t)(A - value at 0) + t(B - value at 1), each term only where it is finite.
float endpoint_exact(float value, float t, bool has_a, float a, float at_0, bool has_b, float b,
                     float at_1) noexcept {
    const float da = has_a ? a - at_0 : 0.0f;
    const float db = has_b ? b - at_1 : 0.0f;
    return value + (1.0f - t) * (std::isfinite(da) ? da : 0.0f) + t * (std::isfinite(db) ? db : 0.0f);
}

}  // namespace

void Program::clear() {
    ops.clear();
    payload.assign(kHeaderWords, 0u);
    payload[kHeaderOne] = float_word(1.0f);
    const Affine34 identity{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    set_endpoint_cameras(identity, identity);
    endpoint_cameras_ = false;
    flags_ = 0u;
}

void Program::set_endpoint_cameras(const Affine34& at_0, const Affine34& at_1) {
    for (unsigned i = 0; i < 12u; ++i) {
        payload[kHeaderCamera0 + i] = float_word(at_0[i]);
        payload[kHeaderCamera1 + i] = float_word(at_1[i]);
    }
    endpoint_cameras_ = true;
}

void Program::push(OpKind kind, std::uint32_t flags, std::uint32_t count, std::uint32_t dst, std::uint32_t at) {
    ops.push_back({control(kind, flags | flags_, count), dst, at, 0u});
}

std::uint32_t Program::append(const float* values, std::uint32_t count) {
    const auto at = static_cast<std::uint32_t>(payload.size());
    for (std::uint32_t i = 0; i < count; ++i) payload.push_back(float_word(values[i]));
    return at;
}

void Program::transform(OpKind kind, std::uint32_t dst, const AffineMotion& m, bool split, bool still,
                        const float* end_a, const float* end_b) {
    std::uint32_t flags = split ? kFlagSplit : 0u;
    std::uint32_t at = 0u;
    if (still) {
        flags |= kFlagStill;
        at = append(m.b.data(), 12u);
    } else if (m.rigid) {
        flags |= kFlagRigid;
        float p[kRigidWords];
        std::memcpy(p, m.qa, sizeof(m.qa));
        std::memcpy(p + 4, m.qb, sizeof(m.qb));
        std::memcpy(p + 8, m.stretch_a, sizeof(m.stretch_a));
        std::memcpy(p + 17, m.stretch_b, sizeof(m.stretch_b));
        for (unsigned r = 0; r < 3u; ++r) {
            p[26u + r] = m.a[r * 4u + 3u];
            p[29u + r] = m.b[r * 4u + 3u];
        }
        p[32] = m.theta;
        p[33] = m.inv_sin_theta;
        at = append(p, kRigidWords);
    } else {
        at = append(m.a.data(), 12u);
        append(m.b.data(), 12u);
    }
    // A still op that is not split is b itself; an element-wise lerp is exact at both ends already.
    // A split op's ends need the cameras at t = 0 and 1 (set_endpoint_cameras).
    const bool exact = ((flags & kFlagSplit) != 0u && endpoint_cameras_) ||
        ((flags & kFlagSplit) == 0u && (flags & kFlagRigid) != 0u);
    const std::uint32_t count = kind == OpKind::Normal ? 9u : 12u;
    if (exact && end_a != nullptr && (flags & kFlagStill) == 0u) {
        flags |= kFlagEndA;
        append(end_a, count);
    } else if (exact && end_a != nullptr && (flags & kFlagSplit) != 0u) {
        // Still in the world: the camera moves it.
        flags |= kFlagEndA;
        append(end_a, count);
    }
    if (exact && end_b != nullptr) {
        flags |= kFlagEndB;
        append(end_b, count);
    }
    push(kind, flags, count, dst, at);
}

void Program::eye(std::uint32_t dst, const EyeMotion& m, const float* end_a, const float* end_b) {
    // A snapped jump or a sprite flip shows b's value at t = 0 by design.
    transform(OpKind::Affine, dst, m.world, m.split, m.still, m.snapped || m.flipped ? nullptr : end_a, end_b);
}

void Program::normal(std::uint32_t dst, const NormalMotion& m, const float* end_a, const float* end_b) {
    transform(OpKind::Normal, dst, m.world, m.split, m.still, end_a, end_b);
}

void Program::lerp(std::uint32_t dst, const float* from, const float* to, std::uint32_t count, bool round) {
    const std::uint32_t at = append(from, count);
    append(to, count);
    push(round ? OpKind::LerpRound : OpKind::Lerp, 0u, count, dst, at);
}

void Program::color8(std::uint32_t dst, std::uint32_t a, std::uint32_t b) {
    const auto at = static_cast<std::uint32_t>(payload.size());
    payload.push_back(a);
    payload.push_back(b);
    push(OpKind::Color8, 0u, 1u, dst, at);
}

void Program::store(std::uint32_t dst, const std::uint32_t* words, std::uint32_t count) {
    const auto at = static_cast<std::uint32_t>(payload.size());
    payload.insert(payload.end(), words, words + count);
    push(OpKind::Store, 0u, count, dst, at);
}

void Program::tex_matrix(std::uint32_t dst, const float* a, const float* b, bool spatial) {
    if (std::memcmp(a, b, 12u * sizeof(float)) == 0 || !finite_all(a, 12u) || !finite_all(b, 12u)) {
        return;
    }
    // A scrolling translation that wrapped is unwrapped on b's side (whole repeats show the same texels), so t = 0
    // is the frame before exactly.
    float to[12];
    std::memcpy(to, b, sizeof(to));
    if (!spatial) {
        for (unsigned i = 0; i < 12u; ++i) {
            if (a[i] == b[i]) continue;
            if ((i & 3u) == 3u) {
                const float d = b[i] - a[i];
                if (std::fabs(d) > 0.5f) to[i] -= std::round(d);
                if (std::fabs(to[i] - a[i]) > kTexStepLimit) return;
            } else if (std::fabs(b[i] - a[i]) > 1.0f) {
                return;
            }
        }
    }
    lerp(dst, a, to, 12u);
}

void Program::light(std::uint32_t dst, const std::uint32_t* a, const std::uint32_t* b,
                    const CameraMotion& camera, bool split) {
    if (camera.known && camera.cut) {
        return;
    }
    split = split && camera.known;
    const bool held = a == nullptr;
    if (held) {
        if (!split) {
            return;
        }
        a = b;
    }
    const std::uint32_t* const end_a = held ? nullptr : a;
    constexpr unsigned kColor = draw_rows::kLightColorWord, kAngle = draw_rows::kLightAngleWord,
                       kPosition = draw_rows::kLightPositionWord, kDirection = draw_rows::kLightDirectionWord;
    static_assert(kDirection == kPosition + 3u);  // the Light op writes position then direction
    if (a[kColor] != b[kColor]) {
        color8(dst + kColor, a[kColor], b[kColor]);
    }
    float from[6], to[6];
    bool attenuation_moves = false;
    for (unsigned i = 0; i < 6u; ++i) {
        const float fa = word_float(a[kAngle + i]), fb = word_float(b[kAngle + i]);
        from[i] = std::isfinite(fa) && std::isfinite(fb) ? fa : fb;
        to[i] = fb;
        attenuation_moves = attenuation_moves || from[i] != to[i];
    }
    if (attenuation_moves) {
        lerp(dst + kAngle, from, to, 6u);
    }
    const float pa[3]{word_float(a[kPosition]), word_float(a[kPosition + 1u]), word_float(a[kPosition + 2u])};
    const float pb[3]{word_float(b[kPosition]), word_float(b[kPosition + 1u]), word_float(b[kPosition + 2u])};
    const float da[3]{word_float(a[kDirection]), word_float(a[kDirection + 1u]), word_float(a[kDirection + 2u])};
    const float db[3]{word_float(b[kDirection]), word_float(b[kDirection + 1u]), word_float(b[kDirection + 2u])};
    if (!finite_all(pa, 3u) || !finite_all(pb, 3u) || !finite_all(da, 3u) || !finite_all(db, 3u)) {
        return;
    }
    // wa wb va vb la lb
    float p[14];
    if (split) {
        const Affine34& to_world_a = held ? camera.b_inverse : camera.a_inverse;
        affine_transform_point(to_world_a, pa, p);
        affine_transform_point(camera.b_inverse, pb, p + 3);
        affine_transform_point(rotation_only(to_world_a), da, p + 6);
        affine_transform_point(rotation_only(camera.b_inverse), db, p + 9);
    } else {
        std::memcpy(p, pa, sizeof(pa));
        std::memcpy(p + 3, pb, sizeof(pb));
        std::memcpy(p + 6, da, sizeof(da));
        std::memcpy(p + 9, db, sizeof(db));
    }
    p[12] = vector_length(da);
    p[13] = vector_length(db);
    const std::uint32_t at = append(p, kLightWords);
    std::uint32_t flags = split ? kFlagLightSplit : 0u;
    if (split && !endpoint_cameras_) {
        push(OpKind::Light, flags, 6u, dst + kPosition, at);
        return;
    }
    // The ends as the frames have them: position then direction.
    const auto ends = [&](const std::uint32_t* light) {
        const float values[6]{word_float(light[kPosition]), word_float(light[kPosition + 1u]),
                              word_float(light[kPosition + 2u]), word_float(light[kDirection]),
                              word_float(light[kDirection + 1u]), word_float(light[kDirection + 2u])};
        append(values, 6u);
    };
    if (end_a != nullptr) {
        flags |= kFlagEndA;
        ends(end_a);
    }
    flags |= kFlagEndB;
    ends(b);
    push(OpKind::Light, flags, 6u, dst + kPosition, at);
}

void Program::palette(std::uint32_t dst, const PaletteMotion& m, RowView a, RowView b,
                      const CameraMotion& camera) {
    std::uint32_t wa[16], wb[16];
    const auto floats = [](const std::uint32_t* words, unsigned count, float* out) {
        for (unsigned i = 0; i < count; ++i) out[i] = word_float(words[i]);
    };
    for (unsigned k = 0; k < 10u; ++k) {
        if (m.position_changed[k] != 0u) {
            float end_a[12], end_b[12];
            b.gather(3u * k, 3u, wb);
            floats(wb, 12u, end_b);
            if (!a.empty()) {
                a.gather(3u * k, 3u, wa);
                floats(wa, 12u, end_a);
            }
            eye(dst + k * 12u, m.position[k], !a.empty() ? end_a : nullptr, end_b);
        }
        if (m.normal_changed[k] != 0u) {
            // Nine values, x y z of three rows (the eval writes value i to row i / 3, column i % 3).
            float end_a[9], end_b[9];
            const unsigned row = kNormalBase / 4u + 3u * k;
            b.gather(row, 3u, wb);
            if (!a.empty()) a.gather(row, 3u, wa);
            for (unsigned i = 0; i < 9u; ++i) {
                const unsigned word = (i / 3u) * 4u + i % 3u;
                end_a[i] = !a.empty() ? word_float(wa[word]) : 0.0f;
                end_b[i] = word_float(wb[word]);
            }
            normal(dst + kNormalBase + k * 12u, m.normal[k], !a.empty() ? end_a : nullptr, end_b);
        }
    }
    if (a.empty()) {
        for (unsigned l = 0; l < 8u; ++l) {
            if ((m.light_changed & (1u << l)) != 0u) {
                b.gather(kLightBase / 4u + 4u * l, 4u, wb);
                light(dst + kLightBase + l * 16u, nullptr, wb, camera, m.split);
            }
        }
        return;
    }
    // Texture matrices TEXMTX0-9 (rows 30-59) and post matrices by block; a
    // block whose rotation or scale changes is taken as spatial.
    const auto blocks = [&](unsigned first_row, unsigned count) {
        for (unsigned k = 0; k < count; ++k) {
            const unsigned row = first_row + 3u * k;
            if (same_rows(a, b, row, 3u)) continue;
            a.gather(row, 3u, wa);
            b.gather(row, 3u, wb);
            float fa[12], fb[12];
            bool spatial = false;
            for (unsigned i = 0; i < 12u; ++i) {
                fa[i] = word_float(wa[i]);
                fb[i] = word_float(wb[i]);
                spatial = spatial || ((i & 3u) != 3u && fa[i] != fb[i]);
            }
            tex_matrix(dst + 4u * row, fa, fb, spatial);
        }
    };
    if (m.tex_changed) {
        blocks(30u, 10u);
    }
    if (m.post_changed) {
        blocks(kPostBase / 4u, 20u);
    }
    for (unsigned l = 0; l < 8u; ++l) {
        if ((m.light_changed & (1u << l)) != 0u) {
            a.gather(kLightBase / 4u + 4u * l, 4u, wa);
            b.gather(kLightBase / 4u + 4u * l, 4u, wb);
            light(dst + kLightBase + l * 16u, wa, wb, camera, m.split);
        }
    }
}

void Program::palette_positions(std::uint32_t dst, const PaletteMotion& m, RowView a, RowView b) {
    std::uint32_t wa[12], wb[12];
    for (unsigned k = 0; k < 10u; ++k) {
        if (m.position_changed[k] != 0u) {
            float end_a[12], end_b[12];
            b.gather(3u * k, 3u, wb);
            if (!a.empty()) a.gather(3u * k, 3u, wa);
            for (unsigned i = 0; i < 12u; ++i) {
                end_a[i] = !a.empty() ? word_float(wa[i]) : 0.0f;
                end_b[i] = word_float(wb[i]);
            }
            eye(dst + k * 12u, m.position[k], !a.empty() ? end_a : nullptr, end_b);
        }
    }
}

void Program::channel_colours(std::uint32_t dst, const VertexConstants& a, const VertexConstants& b) {
    const auto block = [&](std::uint32_t at, const float (*x)[4], const float (*y)[4]) {
        float from[8], to[8];
        bool moves = false;
        for (unsigned ch = 0; ch < kMaxColorChannels; ++ch) {
            for (unsigned c = 0; c < 4u; ++c) {
                const float fx = x[ch][c], fy = y[ch][c];
                from[ch * 4u + c] = std::isfinite(fx) && std::isfinite(fy) ? fx : fy;
                to[ch * 4u + c] = fy;
                moves = moves || from[ch * 4u + c] != fy;
            }
        }
        if (moves) lerp(dst + at, from, to, 4u * kMaxColorChannels);
    };
    block(kVsAmbient, a.chan_ambient, b.chan_ambient);
    block(kVsMaterial, a.chan_material, b.chan_material);
}

void Program::projection(std::uint32_t dst, const VertexConstants& a, const VertexConstants& b) {
    float from[16], to[16];
    const float* x = &a.projection[0][0];
    const float* y = &b.projection[0][0];
    for (unsigned i = 0; i < 16u; ++i) {
        from[i] = std::isfinite(x[i]) && std::isfinite(y[i]) ? x[i] : y[i];
        to[i] = y[i];
    }
    if (std::memcmp(from, to, sizeof(from)) != 0) lerp(dst + kVsProjection, from, to, 16u);
}

void Program::projection_params(std::uint32_t dst, const VertexConstants& a, const VertexConstants& b) {
    // fill_vs_projection: A and C on the diagonal, B and D in column 2
    // (perspective) or 3 (orthographic).
    const auto params = [](const VertexConstants& c, float out[4]) {
        const unsigned column = c.projection[3][3] != 0.0f ? 3u : 2u;
        out[0] = c.projection[0][0];
        out[1] = c.projection[0][column];
        out[2] = c.projection[1][1];
        out[3] = c.projection[1][column];
    };
    float from[4], to[4];
    params(a, from);
    params(b, to);
    for (unsigned i = 0; i < 4u; ++i) {
        if (!std::isfinite(from[i]) || !std::isfinite(to[i])) from[i] = to[i];
    }
    if (std::memcmp(from, to, sizeof(from)) != 0) lerp(dst + kVsMotionPrevProj, from, to, 4u);
}

void Program::ps_colours(std::uint32_t dst, const PixelConstants& a, const PixelConstants& b) {
    const auto range = [&](std::uint32_t at, const float* x, const float* y, unsigned count) {
        float from[16], to[16];
        bool moves = false;
        for (unsigned i = 0; i < count; ++i) {
            from[i] = std::isfinite(x[i]) && std::isfinite(y[i]) ? x[i] : y[i];
            to[i] = y[i];
            moves = moves || from[i] != to[i];
        }
        if (moves) lerp(dst + at, from, to, count);
    };
    range(kPsTevRegs, &a.tev_regs[0][0], &b.tev_regs[0][0], 16u);
    range(kPsKonst, &a.konst[0][0], &b.konst[0][0], 16u);
    range(kPsAlphaRefs, a.alpha_refs, b.alpha_refs, 2u);
    range(kPsAlphaRefs + 3u, a.alpha_refs + 3, b.alpha_refs + 3, 1u);
    range(kPsFog + 4u, a.fog_params + 4, b.fog_params + 4, 3u);
    if (a.fog_params[2] == b.fog_params[2]) {
        range(kPsFog, a.fog_params, b.fog_params, 2u);
        range(kPsFog + 3u, a.fog_params + 3, b.fog_params + 3, 1u);
    }
    for (unsigned r = 0; r < 6u; ++r) {
        if (a.ind_mtx[r][3] != b.ind_mtx[r][3] ||
            std::memcmp(a.ind_mtx[r], b.ind_mtx[r], 3u * sizeof(std::int32_t)) == 0) {
            continue;
        }
        float from[3], to[3];
        for (unsigned c = 0; c < 3u; ++c) {
            from[c] = static_cast<float>(a.ind_mtx[r][c]);
            to[c] = static_cast<float>(b.ind_mtx[r][c]);
        }
        lerp(dst + kPsIndMtx + 4u * r, from, to, 3u, true);
    }
}

void evaluate(const Program& program, const FrameTimes& times, std::uint32_t* out) {
    const std::uint32_t* const header = program.payload.data();
    Affine34 camera_0{}, camera_1{};
    for (unsigned i = 0; i < 12u; ++i) {
        camera_0[i] = word_float(header[kHeaderCamera0 + i]);
        camera_1[i] = word_float(header[kHeaderCamera1 + i]);
    }
    const float zero = word_float(header[kHeaderZero]);
    const float one = word_float(header[kHeaderOne]);
    for (const Op& op : program.ops) {
        const std::uint32_t* p = program.payload.data() + op.payload;
        const std::uint32_t flags = op.control & 0xFF00u;
        const bool previous = (flags & kFlagPrev) != 0u;
        if (times.previous_only && !previous && static_cast<OpKind>(op_kind(op.control)) != OpKind::Store) {
            continue;
        }
        const float t = previous ? times.t_prev : times.t;
        const Affine34& camera_t = previous ? times.camera_prev : times.camera;
        const std::uint32_t count = op_count(op.control);
        std::uint32_t* const dst = out + op.dst;
        const bool has_a = (flags & kFlagEndA) != 0u;
        const bool has_b = (flags & kFlagEndB) != 0u;
        const auto kind = static_cast<OpKind>(op_kind(op.control));
        switch (kind) {
        case OpKind::Affine:
        case OpKind::Normal: {
            const Affine34 m = transform_value(p, flags, kind, t, camera_t);
            Affine34 m0{}, m1{};
            if (has_a || has_b) {
                m0 = transform_value(p, flags, kind, zero, camera_0);
                m1 = transform_value(p, flags, kind, one, camera_1);
            }
            const std::uint32_t* const end_a = p + transform_words(flags);
            const std::uint32_t* const end_b = end_a + (has_a ? count : 0u);
            for (unsigned i = 0; i < count; ++i) {
                const unsigned e = kind == OpKind::Normal ? (i / 3u) * 4u + i % 3u : i;
                // A normal matrix's rows are aligned: value i at row i / 3, column i % 3, as in the palette.
                dst[e] = float_word(endpoint_exact(m[e], t, has_a, has_a ? word_float(end_a[i]) : 0.0f, m0[e],
                                                   has_b, has_b ? word_float(end_b[i]) : 0.0f, m1[e]));
            }
            break;
        }
        case OpKind::Lerp:
            // Equal ends (also infinite ones) stay as they are.
            for (unsigned i = 0; i < count; ++i) {
                const float a = word_float(p[i]);
                const float b = word_float(p[count + i]);
                dst[i] = float_word(a == b ? b : a * (1.0f - t) + b * t);
            }
            break;
        case OpKind::LerpRound:
            for (unsigned i = 0; i < count; ++i) {
                const float v = word_float(p[i]) * (1.0f - t) + word_float(p[count + i]) * t;
                dst[i] = static_cast<std::uint32_t>(static_cast<std::int32_t>(std::lround(v)));
            }
            break;
        case OpKind::Color8: {
            std::uint32_t color = 0u;
            for (unsigned shift = 0; shift < 32u; shift += 8u) {
                const float ca = static_cast<float>((p[0] >> shift) & 0xFFu);
                const float cb = static_cast<float>((p[1] >> shift) & 0xFFu);
                const float c = std::clamp(ca + (cb - ca) * t, 0.0f, 255.0f);
                color |= static_cast<std::uint32_t>(std::lround(c)) << shift;
            }
            dst[0] = color;
            break;
        }
        case OpKind::Light: {
            float v[6], v0[6], v1[6];
            light_value(p, flags, t, camera_t, v);
            if (has_a || has_b) {
                light_value(p, flags, zero, camera_0, v0);
                light_value(p, flags, one, camera_1, v1);
            }
            const std::uint32_t* const end_a = p + kLightWords;
            const std::uint32_t* const end_b = end_a + (has_a ? 6u : 0u);
            for (unsigned i = 0; i < 6u; ++i) {
                dst[i] = float_word(endpoint_exact(v[i], t, has_a, has_a ? word_float(end_a[i]) : 0.0f, v0[i], has_b,
                                                   has_b ? word_float(end_b[i]) : 0.0f, v1[i]));
            }
            break;
        }
        case OpKind::Store:
            std::memcpy(dst, p, count * sizeof(std::uint32_t));
            break;
        }
    }
}

void evaluate(const Program& program, const Affine34& camera_t, float t, std::uint32_t* out) {
    FrameTimes times{};
    times.camera = camera_t;
    times.t = t;
    times.camera_prev = camera_t;
    times.t_prev = t;
    evaluate(program, times, out);
}
}  // namespace galaxy::render::interp_gpu
