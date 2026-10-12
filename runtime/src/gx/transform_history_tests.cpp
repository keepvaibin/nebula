// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/gx/transform_history.h"

#include <cmath>
#include <cstdio>

using namespace galaxy::gx;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

TransformRecord make(std::uint64_t sig, float x, float y, float z) {
    TransformRecord r{};
    r.signature = sig;
    r.translation[0] = x;
    r.translation[1] = y;
    r.translation[2] = z;
    r.position = affine_identity();
    r.position[3] = x;
    r.position[7] = y;
    r.position[11] = z;
    return r;
}

Affine34 rotation_y(float radians, float tx, float ty, float tz) {
    const float c = std::cos(radians), s = std::sin(radians);
    return {c, 0.0f, s, tx,
            0.0f, 1.0f, 0.0f, ty,
            -s, 0.0f, c, tz};
}

void test_affine() {
    const Affine34 m = rotation_y(0.7f, 3.0f, -2.0f, 5.0f);
    Affine34 inv{};
    check(affine_inverse(m, inv), "rotation is invertible");
    const Affine34 id = affine_multiply(m, inv);
    const Affine34 expect = affine_identity();
    bool ok = true;
    for (unsigned i = 0; i < 12u; ++i) ok = ok && near(id[i], expect[i]);
    check(ok, "M * inverse(M) == I");
    Affine34 scaled = affine_identity();
    scaled[0] = 2.0f;
    scaled[5] = 0.5f;
    scaled[10] = 4.0f;
    scaled[3] = 1.0f;
    check(affine_inverse(scaled, inv), "scale is invertible");
    const float p[3]{3.0f, 4.0f, 5.0f};
    float q[3]{}, r[3]{};
    affine_transform_point(scaled, p, q);
    affine_transform_point(inv, q, r);
    check(near(r[0], 3.0f) && near(r[1], 4.0f) && near(r[2], 5.0f), "inverse round trip");
    Affine34 singular{};
    check(!affine_inverse(singular, inv), "singular rejected");
}

void test_interpolate() {
    const Affine34 a = rotation_y(0.0f, 0.0f, 0.0f, -10.0f);
    const Affine34 b = rotation_y(1.5707963f, 10.0f, 0.0f, -10.0f);
    const Affine34 mid = affine_interpolate(a, b, 0.5f);
    const Affine34 expect = rotation_y(0.7853982f, 5.0f, 0.0f, -10.0f);
    bool ok = true;
    for (unsigned i = 0; i < 12u; ++i) ok = ok && near(mid[i], expect[i], 1e-4f);
    check(ok, "slerp halfway between 0 and 90 degrees is 45 degrees");
    const Affine34 at0 = affine_interpolate(a, b, 0.0f);
    const Affine34 at1 = affine_interpolate(a, b, 1.0f);
    bool ends = true;
    for (unsigned i = 0; i < 12u; ++i) ends = ends && near(at0[i], a[i]) && near(at1[i], b[i]);
    check(ends, "interpolation ends at its inputs");
    Affine34 sa = rotation_y(0.3f, 0.0f, 0.0f, 0.0f);
    Affine34 sb = rotation_y(0.5f, 0.0f, 0.0f, 0.0f);
    for (unsigned r = 0; r < 3u; ++r) {
        sa[r * 4u + 0u] *= 2.0f;
        sb[r * 4u + 0u] *= 4.0f;
    }
    const Affine34 sm = affine_interpolate(sa, sb, 0.5f);
    const float sx = std::sqrt(sm[0] * sm[0] + sm[4] * sm[4] + sm[8] * sm[8]);
    const float sy = std::sqrt(sm[1] * sm[1] + sm[5] * sm[5] + sm[9] * sm[9]);
    check(near(sx, 3.0f, 1e-3f) && near(sy, 1.0f, 1e-3f), "per-axis scale lerps");
    check(near(std::atan2(sm[2], sm[10]), 0.4f, 1e-3f), "rotation slerps under scale");
    // Shear: element-wise.
    Affine34 shear = affine_identity();
    shear[1] = 0.5f;
    const Affine34 lm = affine_interpolate(affine_identity(), shear, 0.5f);
    check(near(lm[1], 0.25f) && near(lm[0], 1.0f), "shear lerps element-wise");
    // 180 degree flip stays a rotation (no collapse through the origin).
    const Affine34 flip = rotation_y(3.1415926f, 0.0f, 0.0f, 0.0f);
    const Affine34 half = affine_interpolate(affine_identity(), flip, 0.5f);
    const float len = std::sqrt(half[0] * half[0] + half[4] * half[4] + half[8] * half[8]);
    check(near(len, 1.0f, 1e-3f), "half of a flip keeps unit scale");
}

void test_matching_reordered() {
    TransformHistory h;
    h.reserve(16);
    h.begin_scene(1, nullptr);
    h.add(make(1, 0.0f, 0.0f, -100.0f));
    h.add(make(1, 30.0f, 0.0f, -100.0f));
    h.add(make(2, 0.0f, 5.0f, -50.0f));
    h.end_scene();
    h.begin_scene(3, nullptr);
    check(h.has_previous() && h.previous_frame() == 1u, "previous scene kept");
    // Same objects, drawn in another order, each moved a little.
    const TransformRecord* a = h.add(make(1, 29.0f, 0.0f, -100.0f));
    const TransformRecord* b = h.add(make(2, 0.5f, 5.0f, -50.0f));
    const TransformRecord* c = h.add(make(1, 1.0f, 0.0f, -100.0f));
    check(a != nullptr && near(a->translation[0], 30.0f), "nearest same-signature match");
    check(b != nullptr && b->signature == 2u, "signature separates candidates");
    check(c != nullptr && near(c->translation[0], 0.0f), "remaining candidate claimed");
    check(h.stats().matched == 3u && h.stats().records == 3u, "stats count matches");
    check(h.current_matches()[0] == 1 && h.current_matches()[2] == 0, "match indices");
}

void test_claimed_once_and_threshold() {
    TransformHistory h;
    h.begin_scene(1, nullptr);
    h.add(make(7, 0.0f, 0.0f, -10.0f));
    h.begin_scene(2, nullptr);  // begin without end closes the scene
    check(h.add(make(7, 0.0f, 0.0f, -10.0f)) != nullptr, "first claims");
    check(h.add(make(7, 0.0f, 0.0f, -10.0f)) == nullptr, "a record is claimed once");
    // |t| = 10 -> limit 0.25 * 10 + 1 = 3.5.
    h.begin_scene(3, nullptr);
    check(h.add(make(7, 3.0f, 0.0f, -10.0f)) != nullptr, "inside the limit");
    h.begin_scene(4, nullptr);
    check(h.add(make(7, 7.5f, 0.0f, -10.0f)) == nullptr, "beyond the limit");
}

void test_camera_delta() {
    // World point at (0, 0, -2); the camera moves 3 units along x between
    // frames, so the eye-space position moves by 3, beyond the 1.5 limit.
    const Affine34 view_prev = affine_identity();
    Affine34 view_cur = affine_identity();
    view_cur[3] = -3.0f;  // camera at x = +3
    TransformHistory h;
    h.begin_scene(1, &view_prev);
    h.add(make(9, 0.0f, 0.0f, -2.0f));
    h.end_scene();
    h.begin_scene(3, &view_cur);
    check(h.camera_known(), "camera delta known");
    const float cur[3]{-3.0f, 0.0f, -2.0f};
    float prev[3]{};
    affine_transform_point(h.camera_delta(), cur, prev);
    check(near(prev[0], 0.0f) && near(prev[2], -2.0f), "delta maps current to previous eye");
    check(h.add(make(9, -3.0f, 0.0f, -2.0f)) != nullptr, "camera-predicted match");

    TransformHistory blind;
    blind.begin_scene(1, nullptr);
    blind.add(make(9, 0.0f, 0.0f, -2.0f));
    blind.begin_scene(3, nullptr);
    check(!blind.camera_known(), "no camera without views");
    check(blind.add(make(9, -3.0f, 0.0f, -2.0f)) == nullptr, "no match without the camera");

    // A rotating camera: 10 degrees about y.
    const Affine34 rot = rotation_y(0.1745f, 0.0f, 0.0f, 0.0f);
    TransformHistory spin;
    spin.begin_scene(1, &view_prev);
    spin.add(make(4, 0.0f, 0.0f, -40.0f));
    spin.begin_scene(3, &rot);
    const float world[3]{0.0f, 0.0f, -40.0f};
    float eye[3]{};
    affine_transform_point(rot, world, eye);
    check(spin.add(make(4, eye[0], eye[1], eye[2])) != nullptr, "rotation predicted");
    float back[3]{};
    affine_transform_point(spin.camera_delta(), eye, back);
    check(near(back[0], 0.0f, 1e-3f) && near(back[2], -40.0f, 1e-3f), "rotation delta");
}

void test_reset_and_outside_scene() {
    TransformHistory h;
    check(h.add(make(1, 0.0f, 0.0f, 0.0f)) == nullptr, "no scene, no record");
    check(h.current().empty(), "record ignored outside a scene");
    h.begin_scene(1, nullptr);
    h.add(make(1, 0.0f, 0.0f, -5.0f));
    h.reset();
    h.begin_scene(2, nullptr);
    check(!h.has_previous(), "reset forgets the previous scene");
    check(h.add(make(1, 0.0f, 0.0f, -5.0f)) == nullptr, "nothing to match after reset");
}

void test_many_records() {
    TransformHistory h;
    h.reserve(4096);
    h.begin_scene(1, nullptr);
    for (unsigned i = 0; i < 3000u; ++i) {
        h.add(make(100u + (i % 37u), static_cast<float>(i % 50u) * 20.0f,
                   static_cast<float>(i / 50u) * 20.0f, -500.0f));
    }
    h.begin_scene(3, nullptr);
    unsigned matched = 0;
    // Draw order repeats with neighbouring pairs swapped.
    for (unsigned n = 0; n < 3000u; ++n) {
        const unsigned i = n ^ 1u;
        const TransformRecord* m = h.add(make(100u + (i % 37u),
            static_cast<float>(i % 50u) * 20.0f + 0.5f,
            static_cast<float>(i / 50u) * 20.0f, -500.0f));
        if (m != nullptr && near(m->translation[0] + 0.5f,
                                 static_cast<float>(i % 50u) * 20.0f + 0.5f) &&
            near(m->translation[1], static_cast<float>(i / 50u) * 20.0f)) {
            ++matched;
        }
    }
    check(matched == 3000u, "3000 records match their own previous record");
}

}  // namespace

int main() {
    test_affine();
    test_interpolate();
    test_matching_reordered();
    test_claimed_once_and_threshold();
    test_camera_delta();
    test_reset_and_outside_scene();
    test_many_records();
    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("transform history: ok\n");
    return 0;
}
