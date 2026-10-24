// SPDX-License-Identifier: GPL-3.0-only
// Tests of frame interpolation, the actor-light aim and the sky-sun search.
#include "galaxy/gx/frame_interpolation.h"
#include "galaxy/gx/actor_lights.h"
#include "galaxy/gx/rt_sky_sun.h"

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

std::array<float, 3> light_position(const std::vector<std::uint32_t>& palette, unsigned light) {
    const std::uint32_t* p = palette.data() + kXfLightBase + kXfLightWords * light + kXfLightPosition;
    return {std::bit_cast<float>(p[0]), std::bit_cast<float>(p[1]), std::bit_cast<float>(p[2])};
}

bool same_direction(const std::array<float, 3>& v, const std::array<float, 3>& w, float eps) {
    const float lv = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    const float lw = std::sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
    return lv > 0.0f && lw > 0.0f && (v[0] * w[0] + v[1] * w[1] + v[2] * w[2]) / (lv * lw) > 1.0f - eps;
}

// Eye-space direction of a world direction under a camera.
std::array<float, 3> eye_direction(const Affine34& view, const std::array<float, 3>& d) {
    std::array<float, 3> e{};
    for (unsigned r = 0; r < 3u; ++r) e[r] = view[r * 4u] * d[0] + view[r * 4u + 1u] * d[1] + view[r * 4u + 2u] * d[2];
    return e;
}

void test_actor_lights() {
    check(actor_light_load(0u, 3u).written == 0x07u && actor_light_load(0u, 3u).follow == 0x03u,
          "an ActorLightInfo load writes lights 0-2; A and B can follow the camera");
    check(actor_light_load(1u, 3u).written == 0x09u && actor_light_load(1u, 3u).follow == 0x01u,
          "a coin load writes lights 0 and 3; only A can follow the camera");
    check(actor_light_load(2u, 3u).follow == 0u && actor_light_load(4u, 3u).written == 0u,
          "point light and player loads move no follow-camera light");

    // The game's follow-camera key and fill (eye space) under a turning, moving camera.
    std::vector<std::uint32_t> pal_a(kXfPaletteWords, 0u), pal_b(kXfPaletteWords, 0u), out(kXfPaletteWords);
    const float key[3]{75000.0f, 100000.0f, 76400.0f}, fill[3]{-100000.0f, -93017.6f, -24902.3f};
    for (std::vector<std::uint32_t>* p : {&pal_a, &pal_b}) {
        (*p)[kXfLightBase + 3u] = 0xA49F8900u;
        (*p)[kXfLightBase + kXfLightWords + 3u] = 0x565F9100u;
        for (unsigned i = 0; i < 3u; ++i) {
            (*p)[kXfLightBase + kXfLightPosition + i] = std::bit_cast<std::uint32_t>(key[i]);
            (*p)[kXfLightBase + kXfLightWords + kXfLightPosition + i] = std::bit_cast<std::uint32_t>(fill[i]);
        }
    }
    const std::array<float, 3> sun{0.0f, 0.6f, -0.8f};
    const Affine34 va = camera(0.0f, 0.0f, 100.0f, 500.0f);
    const Affine34 vb = camera(0.3f, 80.0f, 100.0f, 420.0f);
    const ActorLightAim aim_a = aim_at_key(va, sun), aim_b = aim_at_key(vb, sun);
    check(aim_a.valid && aim_b.valid, "the sun gives an aim");
    const std::vector<std::uint32_t> game = pal_a;
    aim_actor_lights(pal_a.data(), game.data(), 0x01u, 0x02u, aim_a);
    aim_actor_lights(pal_b.data(), game.data(), 0x01u, 0x02u, aim_b);
    check(pal_a[kXfLightBase + 3u] == 0xA49F8900u && pal_a[kXfLightBase + kXfLightWords + 3u] == 0x565F9100u,
          "moved lights keep the game's colours");
    const std::array<float, 3> to_sun = eye_direction(va, sun);
    const std::array<float, 3> away{-to_sun[0], -to_sun[1], -to_sun[2]};
    check(same_direction(light_position(pal_a, 0u), to_sun, 1e-6f), "light 0 lies towards the sun");
    check(same_direction(light_position(pal_a, 1u), away, 1e-6f), "light 1 lies opposite the sun");

    // Sub-frames: the lights stay towards and away from the sun at every camera between.
    CameraMotion cam;
    camera_motion(&va, &vb, 0.0f, CameraCutLimits{}, cam);
    PaletteMotion motion;
    palette_motion(pal_a.data(), pal_b.data(), cam, false, motion, kAllPaletteSlots, kAllPaletteSlots, 0x03u);
    check(motion.light_changed == 0x03u && motion.split, "both lights interpolate in world space");
    for (float t : {0.25f, 0.5f, 0.75f}) {
        const Affine34 vt = camera_at(cam, t);
        palette_at(motion, pal_a.data(), pal_b.data(), cam, vt, t, out.data());
        const std::array<float, 3> sun_t = eye_direction(vt, sun);
        check(same_direction(light_position(out, 0u), sun_t, 1e-5f), "a sub-frame's light 0 lies towards the sun");
        check(same_direction(light_position(out, 1u), {-sun_t[0], -sun_t[1], -sun_t[2]}, 1e-5f),
              "a sub-frame's light 1 lies opposite the sun");
    }

    // A key that comes or goes: turned part of the way from the game's direction.
    ActorLightAim ramping = aim_a;
    const float key_length = std::sqrt(key[0] * key[0] + key[1] * key[1] + key[2] * key[2]);
    ramping.blend = 0.0f;
    aim_actor_lights(pal_a.data(), game.data(), 0x01u, 0x00u, ramping);
    const std::array<float, 3> g0 = light_position(pal_a, 0u);
    check(near(g0[0], key[0]) && near(g0[1], key[1]) && near(g0[2], key[2]), "blend 0 is the game's position");
    ramping.blend = 0.25f;
    aim_actor_lights(pal_a.data(), game.data(), 0x01u, 0x00u, ramping);
    std::array<float, 3> quarter{};
    for (unsigned i = 0; i < 3u; ++i) {
        quarter[i] = key[i] / key_length * 0.75f + to_sun[i] * 0.25f;
    }
    check(same_direction(light_position(pal_a, 0u), quarter, 1e-5f),
          "a ramping aim turns light 0 its blend of the way from the game's direction");
}

// A sky map capture: every bin given `sky`, those within `radius` (cosine) of `sun` its colour,
// those `cloud` picks white.
std::vector<std::uint32_t> sky_capture(const std::array<float, 3>& sky, const std::array<float, 3>& sun,
                                       float radius, const std::array<float, 3>& sun_color, unsigned cloud_every) {
    std::vector<std::uint32_t> words(kSkyBins * kSkyBinWords, 0u);
    for (unsigned b = 0; b < kSkyBins; ++b) {
        const std::array<float, 3> d = sky_bin_direction(b);
        std::array<float, 3> c = sky;
        if (d[0] * sun[0] + d[1] * sun[1] + d[2] * sun[2] > radius) {
            c = sun_color;
        } else if (cloud_every != 0u && b % cloud_every == 0u) {
            c = {1.0f, 1.0f, 1.0f};
        }
        words[b * kSkyBinWords] = 2u;
        for (unsigned i = 0; i < 3u; ++i) {
            words[b * kSkyBinWords + 1u + i] = static_cast<std::uint32_t>(c[i] * 255.0f * 2.0f + 0.5f);
        }
    }
    return words;
}

void test_sky_sun() {
    // Bins and their centres agree.
    bool round_trip = true;
    for (unsigned b = 0; b < kSkyBins; ++b) {
        round_trip = round_trip && sky_bin(sky_bin_direction(b)) == b;
    }
    check(round_trip, "a bin's centre lies in that bin");
    const std::array<float, 3> to_sun{0.36f, 0.64f, -0.68f};
    const std::array<float, 3> blue{0.4f, 0.6f, 0.9f};

    SkyMap map;
    check(!map.find().valid, "an empty map shows no sun");
    const std::vector<std::uint32_t> clear = sky_capture(blue, to_sun, 0.995f, {1.0f, 0.97f, 0.9f}, 0u);
    map.add(clear.data());
    const SkySun found = map.find();
    check(found.valid, "a bright place in a blue sky is its sun");
    check(same_direction(found.direction, to_sun, 1e-3f), "the sun's direction is that place's");
    check(found.color[0] == 1.0f && found.color[2] < 0.95f, "the sun's colour is that place's");

    SkyMap clouds;
    clouds.add(sky_capture(blue, to_sun, 0.995f, {0.98f, 0.98f, 0.98f}, 7u).data());
    check(!clouds.find().valid, "white clouds as bright as it all over the sky make no sun");

    SkyMap grey;
    grey.add(sky_capture(blue, to_sun, 0.995f, blue, 0u).data());
    check(!grey.find().valid, "an even sky shows no sun");

    SkyMap low;
    low.add(sky_capture(blue, {0.6f, -0.6f, -0.53f}, 0.995f, {1.0f, 1.0f, 1.0f}, 0u).data());
    check(!low.find().valid, "a bright place well below the horizon is no sun");

    // A daylight sky without a sun: its key comes from its bright side, 50 degrees up; an even
    // sky's from its default side; a night sky has none.
    SkyMap bright_side;
    const std::array<float, 3> horizon{0.8f, 0.0f, -0.6f};
    bright_side.add(sky_capture(blue, horizon, 0.5f, {1.0f, 1.0f, 1.0f}, 0u).data());
    const SkyKey side = bright_side.key();
    check(side.valid && side.strength > 0.9f, "a daylight sky has a key");
    const float across = std::sqrt(1.0f - SkyMap::kKeyHeight * SkyMap::kKeyHeight);
    check(same_direction(side.direction, {0.8f * across, SkyMap::kKeyHeight, -0.6f * across}, 1e-3f),
          "its key stands over its bright side, 50 degrees up");
    const SkyKey even = grey.key();
    check(even.valid && same_direction(even.direction, {SkyMap::kKeyDefaultSide[0] * across, SkyMap::kKeyHeight,
                                                        SkyMap::kKeyDefaultSide[1] * across}, 1e-4f),
          "an even daylight sky lights from its default side");
    SkyMap night;
    night.add(sky_capture({0.02f, 0.03f, 0.08f}, to_sun, 0.995f, {0.02f, 0.03f, 0.08f}, 0u).data());
    check(!night.key().valid, "a night sky has no key");

    // A sun once found stays while the map no longer shows it; a brighter place elsewhere wins.
    SkySun held{};
    map.update(held);
    check(held.valid, "update keeps a found sun");
    map.add(sky_capture(blue, to_sun, 0.995f, blue, 0u).data());
    for (unsigned i = 0; i < 40u; ++i) {
        map.add(sky_capture(blue, to_sun, 0.995f, blue, 0u).data());
    }
    map.update(held);
    check(held.valid && same_direction(held.direction, to_sun, 1e-3f), "a held sun stays without a new one");
    const std::array<float, 3> other{-to_sun[0], to_sun[1], -to_sun[2]};
    for (unsigned i = 0; i < 40u; ++i) {
        map.add(sky_capture(blue, other, 0.995f, {1.0f, 1.0f, 1.0f}, 0u).data());
    }
    map.update(held);
    check(held.valid && same_direction(held.direction, other, 1e-3f), "a brighter sun elsewhere replaces it");
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
    test_light_and_palette();
    test_actor_lights();
    test_sky_sun();
    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("frame_interpolation: all tests passed\n");
    return 0;
}
