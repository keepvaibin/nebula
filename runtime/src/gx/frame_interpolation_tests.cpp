// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/gx/frame_interpolation.h"

#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace galaxy::gx;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps * (1.0f + std::fabs(b)); }

bool near_matrix(const Affine34& a, const Affine34& b, float eps = 1e-3f) {
    for (unsigned i = 0; i < 12u; ++i) {
        if (!near(a[i], b[i], eps)) return false;
    }
    return true;
}

Affine34 rotation_y(float radians, float tx, float ty, float tz) {
    const float c = std::cos(radians), s = std::sin(radians);
    return {c, 0.0f, s, tx, 0.0f, 1.0f, 0.0f, ty, -s, 0.0f, c, tz};
}

// World-to-eye matrix of a camera at `eye` turned by `yaw` about y.
Affine34 camera(float yaw, float ex, float ey, float ez) {
    const Affine34 to_world = rotation_y(yaw, ex, ey, ez);
    Affine34 view{};
    check(affine_inverse(to_world, view), "camera inverse");
    return view;
}

void test_camera_endpoints_and_arc() {
    const Affine34 a = camera(0.0f, 0.0f, 100.0f, 500.0f);
    const Affine34 b = camera(0.4f, 50.0f, 100.0f, 480.0f);
    CameraMotion m;
    camera_motion(&a, &b, 0.0f, CameraCutLimits{}, m);
    check(m.known && m.rigid && !m.cut, "camera pair is a rigid, uncut motion");
    check(near_matrix(camera_at(m, 0.0f), a), "camera at t=0 is a");
    check(near_matrix(camera_at(m, 1.0f), b), "camera at t=1 is b");
    // A camera turning in place keeps every world point at its distance.
    const Affine34 c0 = camera(0.0f, 0.0f, 0.0f, 0.0f);
    const Affine34 c1 = camera(0.6f, 0.0f, 0.0f, 0.0f);
    camera_motion(&c0, &c1, 0.0f, CameraCutLimits{}, m);
    const float world[3]{0.0f, 0.0f, -1000.0f};
    float eye[3]{};
    affine_transform_point(camera_at(m, 0.5f), world, eye);
    const float distance = std::sqrt(eye[0] * eye[0] + eye[1] * eye[1] + eye[2] * eye[2]);
    check(near(distance, 1000.0f, 1e-4f), "turning camera keeps the point's distance (arc, not chord)");
}

void test_camera_cut() {
    const Affine34 a = camera(0.0f, 0.0f, 0.0f, 0.0f);
    const Affine34 b = camera(2.0f, 0.0f, 0.0f, 0.0f);  // 115 degrees in one frame
    CameraMotion m;
    camera_motion(&a, &b, 0.0f, CameraCutLimits{}, m);
    check(m.cut, "a 115 degree turn is a cut");
    check(near_matrix(camera_at(m, 0.25f), b), "a cut shows the new camera at once");
    const Affine34 c = camera(0.0f, 5000.0f, 0.0f, 0.0f);
    camera_motion(&a, &c, 0.0f, CameraCutLimits{}, m);
    check(m.cut, "a 5000 unit jump from rest is a cut");
    camera_motion(&a, &c, 4000.0f, CameraCutLimits{}, m);
    check(!m.cut, "a 5000 unit move after a 4000 unit move is motion");
}

void test_static_object_under_moving_camera() {
    const Affine34 world = rotation_y(0.3f, 200.0f, 10.0f, -300.0f);
    const Affine34 va = camera(0.0f, 0.0f, 50.0f, 400.0f);
    const Affine34 vb = camera(0.2f, 30.0f, 50.0f, 380.0f);
    CameraMotion cam;
    camera_motion(&va, &vb, 0.0f, CameraCutLimits{}, cam);
    const Affine34 ma = affine_multiply(va, world);
    const Affine34 mb = affine_multiply(vb, world);
    EyeMotion motion;
    eye_motion(&ma, mb, cam, false, motion);
    check(motion.split, "perspective records split camera and object");
    for (float t : {0.0f, 0.3f, 0.7f, 1.0f}) {
        const Affine34 vt = camera_at(cam, t);
        const Affine34 at = eye_at(motion, cam, vt, t);
        Affine34 vt_inverse{};
        check(affine_inverse(vt, vt_inverse), "camera at t inverts");
        check(near_matrix(affine_multiply(vt_inverse, at), world, 2e-3f),
              "a static object stays at its world transform at every t");
    }
    // Unmatched records are taken as still in the world.
    eye_motion(nullptr, mb, cam, false, motion);
    const Affine34 vt = camera_at(cam, 0.5f);
    check(near_matrix(eye_at(motion, cam, vt, 0.5f), affine_multiply(vt, world), 2e-3f),
          "an unmatched record follows the camera from where it is");
}

void test_moving_object_and_teleport() {
    const Affine34 v = camera(0.0f, 0.0f, 0.0f, 0.0f);
    CameraMotion cam;
    camera_motion(&v, &v, 0.0f, CameraCutLimits{}, cam);
    const Affine34 a = rotation_y(0.0f, 0.0f, 0.0f, -500.0f);
    const Affine34 b = rotation_y(0.5f, 40.0f, 0.0f, -500.0f);
    EyeMotion motion;
    eye_motion(&a, b, cam, false, motion);
    check(!motion.still && !motion.snapped, "a moving object interpolates");
    const Affine34 half = eye_at(motion, cam, camera_at(cam, 0.5f), 0.5f);
    check(near(half[3], 20.0f, 1e-3f), "translation halfway at t=0.5");
    check(near_matrix(half, rotation_y(0.25f, 20.0f, 0.0f, -500.0f), 2e-3f), "rotation halfway at t=0.5");
    const Affine34 far = rotation_y(0.0f, 3000.0f, 0.0f, -500.0f);
    eye_motion(&a, far, cam, false, motion);
    check(motion.snapped, "a 3000 unit jump at 500 units from the eye is a teleport");
    check(near_matrix(eye_at(motion, cam, camera_at(cam, 0.3f), 0.3f), far), "a teleport shows b");
    // A pulsing object doubling its size between frames is motion.
    Affine34 pulse = a;
    for (unsigned r = 0; r < 3u; ++r) {
        for (unsigned c = 0; c < 3u; ++c) pulse[r * 4u + c] *= 2.0f;
    }
    eye_motion(&a, pulse, cam, false, motion);
    check(!motion.snapped && !motion.still, "a 2x scale pulse interpolates");
    // A sprite spun by 170 degrees and doubled: b's rotation, scale and position lerp.
    Affine34 spun = rotation_y(2.967f, 20.0f, 0.0f, -500.0f);
    for (unsigned r = 0; r < 3u; ++r) {
        for (unsigned c = 0; c < 3u; ++c) spun[r * 4u + c] *= 2.0f;
    }
    eye_motion(&a, spun, cam, false, motion);
    check(motion.flipped && !motion.snapped, "a flip is not a teleport");
    const Affine34 mid = eye_at(motion, cam, camera_at(cam, 0.5f), 0.5f);
    check(near(mid[3], 10.0f, 1e-3f), "a flipped sprite's position lerps");
    const float column = std::sqrt(mid[0] * mid[0] + mid[4] * mid[4] + mid[8] * mid[8]);
    check(near(column, 1.5f, 1e-3f), "a flipped sprite's scale lerps");
    check(near(mid[0] / column, spun[0] / 2.0f, 1e-3f), "a flipped sprite shows b's rotation");
}

void test_tex_matrices() {
    // A projection map moving with its object: every element lerps.
    const float pa[12]{0.5f, 0.0f, 0.0f, 3.0f, 0.0f, 0.5f, 0.0f, -2.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    const float pb[12]{0.45f, 0.1f, 0.0f, 4.5f, -0.1f, 0.45f, 0.0f, -0.4f, 0.0f, 0.0f, 1.0f, 0.0f};
    float out[12];
    tex_matrix_at(pa, pb, true, 0.5f, out);
    check(near(out[3], 3.75f) && near(out[7], -1.2f) && near(out[1], 0.05f), "a spatial matrix lerps whole");
    // The same pair as a texture-coordinate matrix: the translation steps, so all of it shows b.
    tex_matrix_at(pa, pb, false, 0.5f, out);
    check(out[3] == pb[3] && out[1] == pb[1], "a stepping coordinate matrix shows b whole");
    // A scrolling coordinate matrix wraps.
    const float sa[12]{1.0f, 0.0f, 0.0f, 0.95f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    const float sb[12]{1.0f, 0.0f, 0.0f, 0.05f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    tex_matrix_at(sa, sb, false, 0.5f, out);
    check(near(out[3], 0.0f, 1e-4f) || near(out[3], 1.0f, 1e-4f), "a scroll wraps forward");
    // Texgen sources.
    check(texgen_spatial(0u << 7u) && texgen_spatial(1u << 7u) && !texgen_spatial(5u << 7u) &&
              !texgen_spatial((1u << 4u) | (0u << 7u)),
          "position and normal texgens are spatial; texture coordinates and emboss are not");
}

void test_tex_words() {
    check(tex_word_at(150.25f, 151.25f, true, 0.0f) == 150.25f, "t=0 is a exactly (integer scroll)");
    check(tex_word_at(0.0f, 0.4f, true, 1.0f) == 0.4f, "t=1 is b exactly");
    check(near(tex_word_at(0.95f, 0.05f, true, 0.5f), 1.0f) || near(tex_word_at(0.95f, 0.05f, true, 0.5f), 0.0f),
          "a wrapped scroll interpolates forward");
    check(near(tex_word_at(0.0f, 0.1f, true, 0.5f), 0.05f), "a scroll interpolates");
    check(tex_word_at(0.0f, 0.4f, true, 0.5f) == 0.4f, "an atlas step does not slide");
    check(near(tex_word_at(1.0f, 0.5f, false, 0.5f), 0.75f), "scale terms interpolate");
}

void test_light_and_palette() {
    const Affine34 va = camera(0.0f, 0.0f, 0.0f, 0.0f);
    const Affine34 vb = camera(0.3f, 100.0f, 0.0f, 0.0f);
    CameraMotion cam;
    camera_motion(&va, &vb, 0.0f, CameraCutLimits{}, cam);
    const float light_world[3]{500.0f, 200.0f, -800.0f};
    std::uint32_t a[16]{}, b[16]{}, out[16]{};
    float pa[3], pb[3];
    affine_transform_point(va, light_world, pa);
    affine_transform_point(vb, light_world, pb);
    for (unsigned i = 0; i < 3u; ++i) {
        a[10 + i] = std::bit_cast<std::uint32_t>(pa[i]);
        b[10 + i] = std::bit_cast<std::uint32_t>(pb[i]);
        a[13 + i] = b[13 + i] = 0u;
    }
    a[3] = 0x000000FFu;
    b[3] = 0xFF0000FFu;
    const float t = 0.5f;
    const Affine34 vt = camera_at(cam, t);
    light_at(a, b, cam, vt, t, true, out);
    float p[3]{std::bit_cast<float>(out[10]), std::bit_cast<float>(out[11]), std::bit_cast<float>(out[12])};
    float expect[3];
    affine_transform_point(vt, light_world, expect);
    check(near(p[0], expect[0], 1e-3f) && near(p[1], expect[1], 1e-3f) && near(p[2], expect[2], 1e-3f),
          "a static light stays at its world position");
    check(((out[3] >> 24u) & 0xFFu) == 128u, "light color interpolates per byte");
    // Without a previous snapshot the light keeps b's world position at every t.
    for (float tt : {0.0f, 0.4f, 1.0f}) {
        const Affine34 v_tt = camera_at(cam, tt);
        light_at(nullptr, b, cam, v_tt, tt, true, out);
        float q[3]{std::bit_cast<float>(out[10]), std::bit_cast<float>(out[11]), std::bit_cast<float>(out[12])};
        affine_transform_point(v_tt, light_world, expect);
        check(near(q[0], expect[0], 1e-3f) && near(q[1], expect[1], 1e-3f) && near(q[2], expect[2], 1e-3f),
              "a light without history stays at its world position");
    }
    // Screen-space (unsplit) lights stay in eye space.
    light_at(a, b, cam, vt, 1.0f, false, out);
    check(near(std::bit_cast<float>(out[10]), pb[0], 1e-4f) && near(std::bit_cast<float>(out[11]), pb[1], 1e-4f) &&
              near(std::bit_cast<float>(out[12]), pb[2], 1e-4f), "an unsplit light at t=1 is b");

    std::vector<std::uint32_t> pal_a(kXfPaletteWords, 0u), pal_b(kXfPaletteWords, 0u), pal_out(kXfPaletteWords);
    const Affine34 world = rotation_y(0.2f, 10.0f, 0.0f, -400.0f);
    const Affine34 ma = affine_multiply(va, world), mb = affine_multiply(vb, world);
    for (unsigned i = 0; i < 12u; ++i) {
        pal_a[i] = std::bit_cast<std::uint32_t>(ma[i]);
        pal_b[i] = std::bit_cast<std::uint32_t>(mb[i]);
    }
    PaletteMotion motion;
    palette_motion(pal_a.data(), pal_b.data(), cam, false, motion);
    check(motion.any_changed && motion.position_changed[0] != 0u, "palette slot 0 changes under the camera");
    palette_at(motion, pal_a.data(), pal_b.data(), cam, camera_at(cam, 1.0f), 1.0f, pal_out.data());
    Affine34 slot{};
    for (unsigned i = 0; i < 12u; ++i) slot[i] = std::bit_cast<float>(pal_out[i]);
    check(near_matrix(slot, mb, 2e-3f), "palette at t=1 is the frame's palette");
}

void test_colours() {
    GxPsConstants a{}, b{}, out{};
    a.tev_regs[0][0] = 0.0f;
    b.tev_regs[0][0] = 1.0f;
    a.konst[1][3] = 0.2f;
    b.konst[1][3] = 0.6f;
    a.fog_params[4] = 0.0f;
    b.fog_params[4] = 0.5f;
    a.ind_mtx[0][0] = 100;
    b.ind_mtx[0][0] = 200;
    a.ind_mtx[0][3] = b.ind_mtx[0][3] = 7;
    check(ps_colours_move(a, b), "a colour change interpolates");
    ps_colours_at(a, b, 0.5f, out);
    check(near(out.tev_regs[0][0], 0.5f) && near(out.konst[1][3], 0.4f) && near(out.fog_params[4], 0.25f),
          "TEV, konst and fog colours lerp");
    check(out.ind_mtx[0][0] == 150, "indirect matrix rows lerp");
    ps_colours_at(a, b, 1.0f, out);
    check(std::memcmp(&out, &b, sizeof(out)) == 0, "t=1 is b exactly");
    GxPsConstants c = b;
    c.stage_config[0][0] = 1u;
    check(!ps_colours_move(a, c), "another TEV program is a discrete change");
    GxVsConstants va{}, vb{}, vout{};
    vb.chan_material[0][0] = 1.0f;
    check(channel_colours_move(va, vb), "a material colour change interpolates");
    vout = vb;
    channel_colours_at(va, vb, 0.25f, vout);
    check(near(vout.chan_material[0][0], 0.25f), "material colour lerps");
}

}  // namespace

int main() {
    test_colours();
    test_tex_matrices();
    test_camera_endpoints_and_arc();
    test_camera_cut();
    test_static_object_under_moving_camera();
    test_moving_object_and_teleport();
    test_tex_words();
    test_light_and_palette();
    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("frame_interpolation: all tests passed\n");
    return 0;
}
