// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/gx/interp_gpu.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace galaxy::gx::interp_gpu {
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
// theta inv_sin_theta.
constexpr std::uint32_t kRigidWords = 34u;

Affine34 transform_at(const std::uint32_t* p, std::uint32_t flags, float t) noexcept {
    Affine34 out{};
    if ((flags & kFlagStill) != 0u) {
        for (unsigned i = 0; i < 12u; ++i) out[i] = word_float(p[i]);
        return out;
    }
    if ((flags & kFlagRigid) == 0u) {
        for (unsigned i = 0; i < 12u; ++i) {
            const float a = word_float(p[i]);
            out[i] = a + (word_float(p[12u + i]) - a) * t;
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

}  // namespace

void Program::push(OpKind kind, std::uint32_t flags, std::uint32_t count, std::uint32_t dst, std::uint32_t at) {
    ops.push_back({control(kind, flags | flags_, count), dst, at, 0u});
}

std::uint32_t Program::append(const float* values, std::uint32_t count) {
    const auto at = static_cast<std::uint32_t>(payload.size());
    for (std::uint32_t i = 0; i < count; ++i) payload.push_back(float_word(values[i]));
    return at;
}

void Program::transform(OpKind kind, std::uint32_t dst, const AffineMotion& m, bool split, bool still) {
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
    push(kind, flags, kind == OpKind::Normal ? 9u : 12u, dst, at);
}

void Program::eye(std::uint32_t dst, const EyeMotion& m) {
    transform(OpKind::Affine, dst, m.world, m.split, m.still);
}

void Program::normal(std::uint32_t dst, const NormalMotion& m) {
    transform(OpKind::Normal, dst, m.world, m.split, m.still);
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
    float from[12];
    std::memcpy(from, a, sizeof(from));
    if (!spatial) {
        for (unsigned i = 0; i < 12u; ++i) {
            if (a[i] == b[i]) continue;
            if ((i & 3u) == 3u) {
                const float d = b[i] - from[i];
                if (std::fabs(d) > 0.5f) from[i] += std::round(d);
                if (std::fabs(b[i] - from[i]) > kTexStepLimit) return;
            } else if (std::fabs(b[i] - a[i]) > 1.0f) {
                return;
            }
        }
    }
    lerp(dst, from, b, 12u);
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
    if (a[3] != b[3]) {
        color8(dst + 3u, a[3], b[3]);
    }
    float from[6], to[6];
    bool attenuation_moves = false;
    for (unsigned i = 0; i < 6u; ++i) {
        const float fa = word_float(a[4u + i]), fb = word_float(b[4u + i]);
        from[i] = std::isfinite(fa) && std::isfinite(fb) ? fa : fb;
        to[i] = fb;
        attenuation_moves = attenuation_moves || from[i] != to[i];
    }
    if (attenuation_moves) {
        lerp(dst + 4u, from, to, 6u);
    }
    const float pa[3]{word_float(a[10]), word_float(a[11]), word_float(a[12])};
    const float pb[3]{word_float(b[10]), word_float(b[11]), word_float(b[12])};
    const float da[3]{word_float(a[13]), word_float(a[14]), word_float(a[15])};
    const float db[3]{word_float(b[13]), word_float(b[14]), word_float(b[15])};
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
    p[12] = std::sqrt(da[0] * da[0] + da[1] * da[1] + da[2] * da[2]);
    p[13] = std::sqrt(db[0] * db[0] + db[1] * db[1] + db[2] * db[2]);
    const std::uint32_t at = append(p, 14u);
    push(OpKind::Light, split ? kFlagLightSplit : 0u, 6u, dst + 10u, at);
}

void Program::palette(std::uint32_t dst, const PaletteMotion& m, const std::uint32_t* a,
                      const std::uint32_t* b, const CameraMotion& camera) {
    for (unsigned k = 0; k < 10u; ++k) {
        if (m.position_changed[k] != 0u) {
            eye(dst + k * 12u, m.position[k]);
        }
        if (m.normal_changed[k] != 0u) {
            normal(dst + kXfNormalBase + k * 9u, m.normal[k]);
        }
    }
    if (a == nullptr) {
        for (unsigned l = 0; l < 8u; ++l) {
            if ((m.light_changed & (1u << l)) != 0u) {
                const unsigned base = kXfLightBase + l * 16u;
                light(dst + base, nullptr, b + base, camera, m.split);
            }
        }
        return;
    }
    // Texture matrices TEXMTX0-9 (rows 30-59) and post matrices by block; a
    // block whose rotation or scale changes is taken as spatial.
    const auto blocks = [&](unsigned first, unsigned count) {
        for (unsigned k = 0; k < count; ++k) {
            const unsigned base = first + 12u * k;
            if (std::memcmp(a + base, b + base, 12u * 4u) == 0) continue;
            float fa[12], fb[12];
            bool spatial = false;
            for (unsigned i = 0; i < 12u; ++i) {
                fa[i] = word_float(a[base + i]);
                fb[i] = word_float(b[base + i]);
                spatial = spatial || ((i & 3u) != 3u && fa[i] != fb[i]);
            }
            tex_matrix(dst + base, fa, fb, spatial);
        }
    };
    if (m.tex_changed) {
        blocks(120u, 10u);
    }
    if (m.post_changed) {
        blocks(kXfPostBase, 20u);
    }
    for (unsigned l = 0; l < 8u; ++l) {
        if ((m.light_changed & (1u << l)) != 0u) {
            const unsigned base = kXfLightBase + l * 16u;
            light(dst + base, a + base, b + base, camera, m.split);
        }
    }
}

void Program::palette_positions(std::uint32_t dst, const PaletteMotion& m) {
    for (unsigned k = 0; k < 10u; ++k) {
        if (m.position_changed[k] != 0u) {
            eye(dst + k * 12u, m.position[k]);
        }
    }
}

void Program::channel_colours(std::uint32_t dst, const GxVsConstants& a, const GxVsConstants& b) {
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

void Program::projection(std::uint32_t dst, const GxVsConstants& a, const GxVsConstants& b) {
    float from[16], to[16];
    const float* x = &a.projection[0][0];
    const float* y = &b.projection[0][0];
    for (unsigned i = 0; i < 16u; ++i) {
        from[i] = std::isfinite(x[i]) && std::isfinite(y[i]) ? x[i] : y[i];
        to[i] = y[i];
    }
    if (std::memcmp(from, to, sizeof(from)) != 0) lerp(dst + kVsProjection, from, to, 16u);
}

void Program::projection_params(std::uint32_t dst, const GxVsConstants& a, const GxVsConstants& b) {
    // fill_vs_projection: A and C on the diagonal, B and D in column 2
    // (perspective) or 3 (orthographic).
    const auto params = [](const GxVsConstants& c, float out[4]) {
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

void Program::ps_colours(std::uint32_t dst, const GxPsConstants& a, const GxPsConstants& b) {
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
    const Affine34 rotation_now = rotation_only(times.camera);
    const Affine34 rotation_prev = rotation_only(times.camera_prev);
    for (const Op& op : program.ops) {
        const std::uint32_t* p = program.payload.data() + op.payload;
        const std::uint32_t flags = op.control & 0xFF00u;
        const bool previous = (flags & kFlagPrev) != 0u;
        if (times.previous_only && !previous && static_cast<OpKind>(op_kind(op.control)) != OpKind::Store) {
            continue;
        }
        const float t = previous ? times.t_prev : times.t;
        const Affine34& camera_t = previous ? times.camera_prev : times.camera;
        const Affine34& rotation_t = previous ? rotation_prev : rotation_now;
        const std::uint32_t count = op_count(op.control);
        std::uint32_t* const dst = out + op.dst;
        switch (static_cast<OpKind>(op_kind(op.control))) {
        case OpKind::Affine: {
            Affine34 m = transform_at(p, flags, t);
            if ((flags & kFlagSplit) != 0u) m = affine_multiply(camera_t, m);
            for (unsigned i = 0; i < 12u; ++i) dst[i] = float_word(m[i]);
            break;
        }
        case OpKind::Normal: {
            Affine34 n = transform_at(p, flags, t);
            n[3] = n[7] = n[11] = 0.0f;
            if ((flags & kFlagSplit) != 0u) n = affine_multiply(rotation_t, n);
            for (unsigned r = 0; r < 3u; ++r) {
                for (unsigned c = 0; c < 3u; ++c) dst[r * 3u + c] = float_word(n[r * 4u + c]);
            }
            break;
        }
        case OpKind::Lerp:
            // Equal ends (also infinite ones) stay as they are.
            for (unsigned i = 0; i < count; ++i) {
                const float a = word_float(p[i]);
                const float b = word_float(p[count + i]);
                dst[i] = float_word(a == b ? b : a + (b - a) * t);
            }
            break;
        case OpKind::LerpRound:
            for (unsigned i = 0; i < count; ++i) {
                const float a = word_float(p[i]);
                const float v = a + (word_float(p[count + i]) - a) * t;
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
            float w[3], v[3], pos[3], dir[3];
            for (unsigned i = 0; i < 3u; ++i) {
                w[i] = word_float(p[i]) + (word_float(p[3u + i]) - word_float(p[i])) * t;
                v[i] = word_float(p[6u + i]) + (word_float(p[9u + i]) - word_float(p[6u + i])) * t;
            }
            if ((flags & kFlagLightSplit) != 0u) {
                affine_transform_point(camera_t, w, pos);
                affine_transform_point(rotation_t, v, dir);
            } else {
                std::memcpy(pos, w, sizeof(w));
                std::memcpy(dir, v, sizeof(v));
            }
            const float la = word_float(p[12]), lb = word_float(p[13]);
            const float ld = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
            if (ld > 1e-20f) {
                const float scale = (la + (lb - la) * t) / ld;
                for (float& x : dir) x *= scale;
            }
            for (unsigned i = 0; i < 3u; ++i) {
                dst[i] = float_word(pos[i]);
                dst[3u + i] = float_word(dir[i]);
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

}  // namespace galaxy::gx::interp_gpu
