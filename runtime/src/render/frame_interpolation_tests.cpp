#include "galaxy/display_timeline.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render/ambient_occlusion_shape.h"
// SPDX-License-Identifier: GPL-3.0-only
// Tests of frame interpolation and the sky-sun search.
#include "galaxy/render/frame_interpolation.h"

#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace galaxy::render;

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
}

void test_light() {
    const Affine34 va = camera(0.0f, 0.0f, 0.0f, 0.0f);
    const Affine34 vb = camera(0.3f, 100.0f, 0.0f, 0.0f);
    CameraMotion cam;
    camera_motion(&va, &vb, 0.0f, CameraCutLimits{}, cam);
    const float light_world[3]{500.0f, 200.0f, -800.0f};
    float a[16]{}, b[16]{}, out[16]{};
    affine_transform_point(va, light_world, a + 10);
    affine_transform_point(vb, light_world, b + 10);
    a[0] = 0.0f;
    b[0] = 200.0f;
    const float t = 0.5f;
    const Affine34 vt = camera_at(cam, t);
    light_at(a, b, cam, vt, t, true, out);
    float expect[3];
    affine_transform_point(vt, light_world, expect);
    check(near(out[10], expect[0], 1e-3f) && near(out[11], expect[1], 1e-3f) && near(out[12], expect[2], 1e-3f),
          "a static light stays at its world position");
    check(near(out[0], 100.0f), "a light's colour interpolates");
    // Without a previous frame the light keeps b's world position at every t.
    for (float tt : {0.0f, 0.4f, 1.0f}) {
        const Affine34 v_tt = camera_at(cam, tt);
        light_at(nullptr, b, cam, v_tt, tt, true, out);
        affine_transform_point(v_tt, light_world, expect);
        check(near(out[10], expect[0], 1e-3f) && near(out[11], expect[1], 1e-3f) && near(out[12], expect[2], 1e-3f),
              "a light without history stays at its world position");
    }
    // Lights that aren't split stay in eye space.
    light_at(a, b, cam, vt, 1.0f, false, out);
    check(near(out[10], b[10], 1e-4f) && near(out[11], b[11], 1e-4f) && near(out[12], b[12], 1e-4f),
          "an unsplit light at t=1 is b");
}

void test_occlusion_budget_and_sky_direction() {
    for (unsigned height=1; height<=8192; ++height) {
        const unsigned divisor=ao::occlusion_divisor(height);
        check((height+divisor-1u)/divisor <= ao::kFullLines,"AO stays within its working-resolution budget");
    }
    const Affine34 identity=affine_identity();
    const Affine34 roll={0,-1,0,100,1,0,0,200,0,0,1,300};
    const float up[3]={0.0f,1.0f,0.0f};
    float turned[3]{};
    eye_direction_at(up,identity,roll,turned);
    check(std::fabs(turned[0]+1.0f) < 1e-6f && std::fabs(turned[1]) < 1e-6f && std::fabs(turned[2]) < 1e-6f,
          "sky up rotates with the presented camera and ignores translation");
    float round_trip[3]{};
    eye_direction_at(turned,roll,identity,round_trip);
    check(std::fabs(round_trip[0]) < 1e-6f && std::fabs(round_trip[1]-1.0f) < 1e-6f && std::fabs(round_trip[2]) < 1e-6f,
          "sky direction survives eye/world conversion");
    const Affine34 flat={0,0,0,0,0,0,0,0,0,0,0,0};
    float kept[3]{};
    eye_direction_at(up,flat,roll,kept);
    check(kept[0]==0.0f && kept[1]==1.0f && kept[2]==0.0f, "a view without an inverse leaves the direction as it was");
}

}  // namespace

int main() {
    check(galaxy::presentation_grid_at(100, 114, 8) == 98, "presentation phase differs from app start phase");
    check(galaxy::presentation_grid_at(148, 114, 8) == 146, "skipped callbacks retain display phase");
    check(galaxy::presentation_grid_at(98, 114, 8) == 98, "exact presentation tick remains exact");
    check(galaxy::presentation_grid_at(100, 0, 8) == 100, "legacy callback uses its original phase");

    for (const auto height : galaxy::render::kInternalHeights) {
        for (const auto size : {std::pair{2504u, 2256u}, std::pair{2520u, 1080u}, std::pair{1280u, 720u}, std::pair{5120u, 1440u}}) {
            const auto got = galaxy::render::internal_extent(size.first, size.second, height, 16384u);
            check(got.second == (height ? height : size.second), "internal height follows explicit preset or output");
            check(std::abs(double(got.first) / got.second - double(size.first) / size.second) <= 0.5 / got.second + 1e-6, "internal width follows aspect while folding or resizing");
        }
    }

    test_occlusion_budget_and_sky_direction();
    test_tex_matrices();
    test_camera_endpoints_and_arc();
    test_camera_cut();
    test_static_object_under_moving_camera();
    test_moving_object_and_teleport();
    test_light();
    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("frame_interpolation: all tests passed\n");
    return 0;
}
