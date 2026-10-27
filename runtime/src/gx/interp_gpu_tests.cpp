// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/gx/interp_gpu.h"

#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

using namespace galaxy::gx;
using namespace galaxy::gx::interp_gpu;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

bool near_word(std::uint32_t a, std::uint32_t b, float eps = 2e-5f) {
    if (a == b) return true;
    const float x = std::bit_cast<float>(a), y = std::bit_cast<float>(b);
    return std::fabs(x - y) <= eps * (1.0f + std::fabs(y));
}

std::mt19937 rng(20261013u);

float uniform(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); }

Affine34 rotation(float yaw, float pitch, float roll, float sx, float sy, float sz, float tx, float ty, float tz) {
    const float cy = std::cos(yaw), sy_ = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    const float cr = std::cos(roll), sr = std::sin(roll);
    const float r[3][3] = {{cy * cr + sy_ * sp * sr, -cy * sr + sy_ * sp * cr, sy_ * cp},
                           {cp * sr, cp * cr, -sp},
                           {-sy_ * cr + cy * sp * sr, sr * sy_ + cy * sp * cr, cy * cp}};
    return {r[0][0] * sx, r[0][1] * sy, r[0][2] * sz, tx, r[1][0] * sx, r[1][1] * sy, r[1][2] * sz, ty,
            r[2][0] * sx, r[2][1] * sy, r[2][2] * sz, tz};
}

Affine34 random_object() {
    return rotation(uniform(-3.0f, 3.0f), uniform(-1.4f, 1.4f), uniform(-3.0f, 3.0f), uniform(0.5f, 2.0f),
                    uniform(0.5f, 2.0f), uniform(0.5f, 2.0f), uniform(-500.0f, 500.0f), uniform(-200.0f, 200.0f),
                    uniform(-2000.0f, -100.0f));
}

Affine34 nudge(const Affine34& m, float amount) {
    Affine34 n = m;
    const Affine34 d = rotation(uniform(-amount, amount), uniform(-amount, amount), uniform(-amount, amount),
                                1.0f, 1.0f, 1.0f, uniform(-20.0f, 20.0f) * amount, uniform(-20.0f, 20.0f) * amount,
                                uniform(-20.0f, 20.0f) * amount);
    return affine_multiply(d, n);
}

Affine34 view(float yaw, float x, float y, float z) {
    Affine34 to_world = rotation(yaw, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, x, y, z);
    Affine34 out{};
    check(affine_inverse(to_world, out), "view inverse");
    return out;
}

void test_transforms() {
    for (int round = 0; round < 400; ++round) {
        const Affine34 va = view(uniform(-1.0f, 1.0f), uniform(-50.0f, 50.0f), 100.0f, 500.0f);
        const Affine34 vb = nudge(va, 0.05f);
        CameraMotion camera;
        camera_motion(&va, &vb, 0.0f, CameraCutLimits{}, camera);
        const Affine34 wa = random_object();
        // Envelope-like blends (not orthogonal) take the polar path.
        Affine34 wb = nudge(wa, round % 3 == 0 ? 0.3f : 0.05f);
        if (round % 5 == 0) wb[1] += 0.2f;
        const Affine34 ma = affine_multiply(va, wa);
        const Affine34 mb = affine_multiply(vb, wb);
        const bool orthographic = round % 7 == 0;
        EyeMotion eye;
        eye_motion(round % 11 == 0 ? nullptr : &ma, mb, camera, orthographic, eye);
        NormalMotion normal;
        normal_motion(round % 13 == 0 ? nullptr : &ma, mb, camera, normal);
        Program program;
        program.eye(0u, eye);
        program.normal(12u, normal);
        for (float t : {0.25f, 0.5f, 0.75f}) {
            const Affine34 camera_t = camera_at(camera, t);
            std::uint32_t out[21]{};
            evaluate(program, camera_t, t, out);
            const Affine34 want = eye_at(eye, camera, camera_t, t);
            const Affine34 want_n = normal_at(normal, camera, camera_t, t);
            bool same = true;
            for (unsigned i = 0; i < 12u; ++i) same = same && near_word(out[i], std::bit_cast<std::uint32_t>(want[i]));
            for (unsigned r = 0; r < 3u; ++r) {
                for (unsigned c = 0; c < 3u; ++c) {
                    same = same && near_word(out[12u + r * 3u + c], std::bit_cast<std::uint32_t>(want_n[r * 4u + c]));
                }
            }
            check(same, "affine and normal ops match eye_at and normal_at");
        }
    }
}

void random_palette(std::vector<std::uint32_t>& words, const Affine34& view_matrix, float scale) {
    words.assign(kXfPaletteWords, 0u);
    for (unsigned k = 0; k < 10u; ++k) {
        const Affine34 m = affine_multiply(view_matrix, random_object());
        for (unsigned i = 0; i < 12u; ++i) words[k * 12u + i] = std::bit_cast<std::uint32_t>(m[i]);
        for (unsigned r = 0; r < 3u; ++r) {
            for (unsigned c = 0; c < 3u; ++c) {
                words[kXfNormalBase + k * 9u + r * 3u + c] = std::bit_cast<std::uint32_t>(m[r * 4u + c] / scale);
            }
        }
    }
    for (unsigned w = 120u; w < 256u; ++w) words[w] = std::bit_cast<std::uint32_t>(uniform(-2.0f, 2.0f));
    for (unsigned w = kXfPostBase; w < kXfLightBase; ++w) words[w] = std::bit_cast<std::uint32_t>(uniform(-2.0f, 2.0f));
    for (unsigned l = 0; l < 8u; ++l) {
        std::uint32_t* light = words.data() + kXfLightBase + l * 16u;
        light[3] = static_cast<std::uint32_t>(rng());
        for (unsigned i = 4u; i < 10u; ++i) light[i] = std::bit_cast<std::uint32_t>(uniform(-1.0f, 1.0f));
        for (unsigned i = 10u; i < 13u; ++i) light[i] = std::bit_cast<std::uint32_t>(uniform(-1000.0f, 1000.0f));
        for (unsigned i = 13u; i < 16u; ++i) light[i] = std::bit_cast<std::uint32_t>(uniform(-1.0f, 1.0f));
    }
}

void test_palettes() {
    for (int round = 0; round < 120; ++round) {
        const Affine34 va = view(uniform(-1.0f, 1.0f), uniform(-50.0f, 50.0f), 100.0f, 500.0f);
        const Affine34 vb = nudge(va, 0.04f);
        CameraMotion camera;
        camera_motion(&va, &vb, 0.0f, CameraCutLimits{}, camera);
        std::vector<std::uint32_t> a, b;
        random_palette(a, va, 1.0f);
        b = a;
        // b: the same objects moved a little, some texture and light changes.
        for (unsigned k = 0; k < 10u; ++k) {
            Affine34 m{};
            for (unsigned i = 0; i < 12u; ++i) m[i] = std::bit_cast<float>(a[k * 12u + i]);
            Affine34 w{};
            Affine34 va_inv{};
            check(affine_inverse(va, va_inv), "palette view inverse");
            w = affine_multiply(va_inv, m);
            const Affine34 moved = affine_multiply(vb, nudge(w, 0.03f));
            for (unsigned i = 0; i < 12u; ++i) b[k * 12u + i] = std::bit_cast<std::uint32_t>(moved[i]);
        }
        for (unsigned w = 120u; w < 256u; w += 7u) b[w] = std::bit_cast<std::uint32_t>(std::bit_cast<float>(b[w]) + uniform(-0.1f, 0.1f));
        for (unsigned l = 0; l < 8u; l += 2u) {
            std::uint32_t* light = b.data() + kXfLightBase + l * 16u;
            light[3] ^= 0x10203040u;
            light[11] = std::bit_cast<std::uint32_t>(std::bit_cast<float>(light[11]) + 10.0f);
        }
        const bool no_before = round % 9 == 0;
        PaletteMotion motion;
        palette_motion(no_before ? nullptr : a.data(), b.data(), camera, round % 10 == 0, motion);
        Program program;
        program.palette(0u, motion, no_before ? nullptr : a.data(), b.data(), camera);
        for (float t : {0.2f, 0.5f, 0.9f}) {
            const Affine34 camera_t = camera_at(camera, t);
            std::vector<std::uint32_t> got = b;
            evaluate(program, camera_t, t, got.data());
            std::vector<std::uint32_t> want(kXfPaletteWords);
            palette_at(motion, no_before ? nullptr : a.data(), b.data(), camera, camera_t, t, want.data());
            bool same = true;
            for (unsigned w = 0; w < kXfPaletteWords; ++w) {
                const bool colour = w >= kXfLightBase && ((w - kXfLightBase) & 15u) == 3u;
                same = same && (colour ? got[w] == want[w] : near_word(got[w], want[w], 1e-4f));
            }
            check(same, "palette ops match palette_at");
        }
    }
}

void test_texture_matrices() {
    const float scroll_a[12]{1, 0, 0, 0.95f, 0, 1, 0, 0.25f, 0, 0, 1, 0};
    const float scroll_b[12]{1, 0, 0, 0.05f, 0, 1, 0, 0.30f, 0, 0, 1, 0};
    const float step_b[12]{1, 0, 0, 0.55f, 0, 1, 0, 0.25f, 0, 0, 1, 0};
    for (bool spatial : {false, true}) {
        for (const float* b : {scroll_b, step_b}) {
            Program program;
            program.tex_matrix(0u, scroll_a, b, spatial);
            for (float t : {0.25f, 0.5f}) {
                std::uint32_t out[12];
                std::memcpy(out, b, sizeof(out));
                evaluate(program, affine_identity(), t, out);
                float want[12];
                tex_matrix_at(scroll_a, b, spatial, t, want);
                bool same = true;
                for (unsigned i = 0; i < 12u; ++i) same = same && near_word(out[i], std::bit_cast<std::uint32_t>(want[i]));
                check(same, "texture matrix op matches tex_matrix_at");
            }
        }
    }
}

void test_colours() {
    GxPsConstants a{}, b{};
    for (unsigned r = 0; r < 4u; ++r) {
        for (unsigned c = 0; c < 4u; ++c) {
            a.tev_regs[r][c] = uniform(-1.0f, 1.0f);
            b.tev_regs[r][c] = uniform(-1.0f, 1.0f);
            a.konst[r][c] = uniform(0.0f, 1.0f);
            b.konst[r][c] = r == 2u ? a.konst[r][c] : uniform(0.0f, 1.0f);
        }
    }
    a.alpha_refs[0] = 0.2f; b.alpha_refs[0] = 0.6f; a.alpha_refs[3] = 1.0f; b.alpha_refs[3] = 0.5f;
    a.fog_params[2] = b.fog_params[2] = 3.0f; a.fog_params[0] = 1.0f; b.fog_params[0] = 2.0f;
    a.fog_params[4] = 0.1f; b.fog_params[4] = 0.9f;
    a.ind_mtx[1][0] = 100; b.ind_mtx[1][0] = 300; a.ind_mtx[1][3] = b.ind_mtx[1][3] = 5;
    a.ind_mtx[2][0] = 7; b.ind_mtx[2][0] = 9; a.ind_mtx[2][3] = 4; b.ind_mtx[2][3] = 6;
    b.tev_regs[1][2] = std::numeric_limits<float>::infinity();
    Program program;
    program.ps_colours(0u, a, b);
    for (float t : {0.25f, 0.5f, 0.8f}) {
        GxPsConstants got = b;
        evaluate(program, affine_identity(), t, reinterpret_cast<std::uint32_t*>(&got));
        GxPsConstants want{};
        ps_colours_at(a, b, t, want);
        check(std::memcmp(&got.ind_mtx, &want.ind_mtx, sizeof(want.ind_mtx)) == 0, "indirect matrices match");
        const auto* g = reinterpret_cast<const std::uint32_t*>(&got);
        const auto* w = reinterpret_cast<const std::uint32_t*>(&want);
        bool same = true;
        for (unsigned i = 0; i < sizeof(GxPsConstants) / 4u; ++i) same = same && near_word(g[i], w[i]);
        check(same, "pixel colour ops match ps_colours_at");
    }
    GxVsConstants va{}, vb{};
    for (unsigned ch = 0; ch < kMaxColorChannels; ++ch) {
        for (unsigned c = 0; c < 4u; ++c) {
            va.chan_ambient[ch][c] = uniform(0.0f, 1.0f);
            vb.chan_ambient[ch][c] = uniform(0.0f, 1.0f);
            va.chan_material[ch][c] = uniform(0.0f, 1.0f);
            vb.chan_material[ch][c] = va.chan_material[ch][c];
        }
        for (unsigned c = 0; c < 4u; ++c) {
            va.projection[ch][c] = uniform(-2.0f, 2.0f);
            vb.projection[ch][c] = uniform(-2.0f, 2.0f);
        }
    }
    Program vs;
    vs.channel_colours(0u, va, vb);
    vs.projection(0u, va, vb);
    GxVsConstants got = vb;
    evaluate(vs, affine_identity(), 0.4f, reinterpret_cast<std::uint32_t*>(&got));
    GxVsConstants want = vb;
    channel_colours_at(va, vb, 0.4f, want);
    for (unsigned i = 0; i < 16u; ++i) {
        (&want.projection[0][0])[i] = (&va.projection[0][0])[i] + ((&vb.projection[0][0])[i] - (&va.projection[0][0])[i]) * 0.4f;
    }
    const auto* g = reinterpret_cast<const std::uint32_t*>(&got);
    const auto* w = reinterpret_cast<const std::uint32_t*>(&want);
    bool same = true;
    for (unsigned i = 0; i < sizeof(GxVsConstants) / 4u; ++i) same = same && near_word(g[i], w[i]);
    check(same, "channel colour and projection ops match");
}

void test_layout() {
    Layout layout{3u, 2u, 4u};
    check(layout.palette_base() == 3u * sizeof(GxVsConstants), "palettes follow the VS blocks");
    check(layout.ps_base() % 256u == 0u && layout.palette_offset(1u) % 256u == 0u, "blocks stay 256-byte aligned");
    check(layout.bytes() == layout.ps_base() + 4u * sizeof(GxPsConstants), "layout size");
    Layout motion{3u, 2u, 4u, 2u};
    check(motion.prev_palette_offset(0u) == motion.palette_offset(2u) &&
              motion.ps_base() == motion.prev_palette_offset(1u) + kPrevPaletteBytes &&
              motion.prev_palette_offset(1u) % 256u == 0u,
          "previous-frame palette rows follow the palettes");
}

// Ops flagged for the frame presented before take its t and camera; the frame
// at t = 1 evaluates only those.
void test_previous_frame() {
    for (int round = 0; round < 200; ++round) {
        const Affine34 va = view(uniform(-1.0f, 1.0f), uniform(-50.0f, 50.0f), 100.0f, 500.0f);
        const Affine34 vb = nudge(va, 0.05f);
        CameraMotion camera;
        camera_motion(&va, &vb, 0.0f, CameraCutLimits{}, camera);
        const Affine34 wa = random_object();
        const Affine34 wb = nudge(wa, 0.1f);
        EyeMotion eye;
        eye_motion(&wa, affine_multiply(vb, wb), camera, false, eye);
        GxVsConstants pa{}, pb{};
        const bool orthographic = round % 4 == 0;
        for (GxVsConstants* c : {&pa, &pb}) {
            const float k = c == &pa ? 1.0f : uniform(0.9f, 1.1f);
            c->projection[0][0] = 1.2f * k;
            c->projection[1][1] = 1.6f * k;
            c->projection[0][orthographic ? 3 : 2] = 0.01f * k;
            c->projection[1][orthographic ? 3 : 2] = -0.02f * k;
            c->projection[3][orthographic ? 3 : 2] = orthographic ? 1.0f : -1.0f;
        }
        const std::uint32_t ctrl[2]{0u, 1234u};
        const std::uint32_t base_row = 416u;
        Program program;
        program.eye(0u, eye);
        program.store(30u, &base_row, 1u);
        program.at_previous(true);
        program.eye(12u, eye);
        program.projection_params(24u - kVsMotionPrevProj, pa, pb);
        program.store(28u, ctrl, 2u);
        program.at_previous(false);
        const float t = uniform(0.3f, 1.0f);
        const float t_prev = uniform(0.0f, t);
        FrameTimes times{};
        times.camera = camera_at(camera, t);
        times.t = t;
        times.camera_prev = camera_at(camera, t_prev);
        times.t_prev = t_prev;
        std::uint32_t out[31]{};
        evaluate(program, times, out);
        const Affine34 now = eye_at(eye, camera, times.camera, t);
        const Affine34 before = eye_at(eye, camera, times.camera_prev, t_prev);
        bool same = true;
        for (unsigned i = 0; i < 12u; ++i) {
            same = same && near_word(out[i], std::bit_cast<std::uint32_t>(now[i])) &&
                   near_word(out[12u + i], std::bit_cast<std::uint32_t>(before[i]));
        }
        const unsigned column = orthographic ? 3u : 2u;
        const float want_proj[4]{pa.projection[0][0] + (pb.projection[0][0] - pa.projection[0][0]) * t_prev,
                                 pa.projection[0][column] + (pb.projection[0][column] - pa.projection[0][column]) * t_prev,
                                 pa.projection[1][1] + (pb.projection[1][1] - pa.projection[1][1]) * t_prev,
                                 pa.projection[1][column] + (pb.projection[1][column] - pa.projection[1][column]) * t_prev};
        for (unsigned i = 0; i < 4u; ++i) {
            same = same && near_word(out[24u + i], std::bit_cast<std::uint32_t>(want_proj[i]));
        }
        same = same && out[28] == 0u && out[29] == 1234u && out[30] == base_row;
        check(same, "previous-frame ops take its t and camera");
        std::uint32_t last[31]{};
        times.previous_only = true;
        evaluate(program, times, last);
        bool previous_only = true;
        for (unsigned i = 0; i < 12u; ++i) previous_only = previous_only && last[i] == 0u;
        for (unsigned i = 12u; i < 31u; ++i) previous_only = previous_only && last[i] == out[i];
        check(previous_only, "the frame at t = 1 evaluates the stores and the previous-frame ops only");
    }
}

}  // namespace

int main() {
    test_transforms();
    test_palettes();
    test_texture_matrices();
    test_colours();
    test_layout();
    test_previous_frame();
    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("interp_gpu: all tests passed\n");
    return 0;
}
