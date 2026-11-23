// SPDX-License-Identifier: GPL-3.0-only
// The ray of the scene query (Game/Util/ScenePointRay.hpp, P-294, S0.2.3): the direction through a screen point is the
// targets' screen projection (StarPointerTarget::calcScreenPositionFromView) read backwards, it is the same at every
// resolution, and a sphere is met at its near surface, never behind the camera or around it.
#include "Game/Util/ScenePointRay.hpp"

#include <math.h>
#include <stdio.h>

namespace {
    int sFailures;

    void expect(bool ok, const char* pWhat) {
        if (!ok) {
            printf("FAIL %s\n", pWhat);
            sFailures++;
        }
    }

    bool isNear(f32 a, f32 b, f32 tolerance) {
        return fabsf(a - b) <= tolerance * (1.0f + fabsf(b));
    }

    struct Projection {
        f32 p00;
        f32 p02;
        f32 p11;
        f32 p12;
    };

    // StarPointerTarget::calcScreenPositionFromView, written out: the view point onto the screen of width by height.
    void project(const Projection& rProj, const Vec3f& rView, f32 width, f32 height, f32* pX, f32* pY) {
        const f32 w = -rView.z;
        const f32 x = (rView.x * rProj.p00 + rView.z * rProj.p02) / w;
        const f32 y = -((rView.y * rProj.p11 + rView.z * rProj.p12) / w);
        *pX = width * 0.5f + x * width * 0.5f;
        *pY = height * 0.5f + y * height * 0.5f;
    }

    // A perspective of 45 degrees at 16:9, centred, and one shifted off the centre.
    const Projection cProjections[] = {
        {1.0f / (16.0f / 9.0f * 0.41421356f), 0.0f, 1.0f / 0.41421356f, 0.0f},
        {1.0f / (16.0f / 9.0f * 0.41421356f), 0.125f, 1.0f / 0.41421356f, -0.0625f},
    };

    void testRoundTrip() {
        const Vec3f points[] = {Vec3f(0.0f, 0.0f, -100.0f), Vec3f(120.0f, -40.0f, -900.0f), Vec3f(-3000.0f, 2500.0f, -20000.0f),
                                Vec3f(1.5f, 0.75f, -12.0f)};
        for (const Projection& rProj : cProjections) {
            for (const Vec3f& rPoint : points) {
                f32 x, y;
                project(rProj, rPoint, 1280.0f, 720.0f, &x, &y);
                const Vec3f dir = ScenePointRay::calcViewDir(x, y, 1280.0f, 720.0f, rProj.p00, rProj.p02, rProj.p11, rProj.p12);
                const f32 depth = -rPoint.z;
                expect(dir.z == -1.0f, "the direction has a view depth of 1");
                expect(isNear(dir.x * depth, rPoint.x, 1e-4f) && isNear(dir.y * depth, rPoint.y, 1e-4f),
                       "the ray through a point's screen position passes through the point at its depth");
            }
        }
    }

    void testEveryResolution() {
        const f32 sizes[][2] = {{160.0f, 90.0f}, {640.0f, 360.0f}, {1280.0f, 720.0f}, {3840.0f, 2160.0f}};
        const Projection& rProj = cProjections[1];
        const Vec3f base = ScenePointRay::calcViewDir(0.3f * 1280.0f, 0.8f * 720.0f, 1280.0f, 720.0f, rProj.p00, rProj.p02, rProj.p11, rProj.p12);
        for (const auto& rSize : sizes) {
            const Vec3f dir = ScenePointRay::calcViewDir(0.3f * rSize[0], 0.8f * rSize[1], rSize[0], rSize[1], rProj.p00, rProj.p02, rProj.p11,
                                                         rProj.p12);
            expect(isNear(dir.x, base.x, 1e-6f) && isNear(dir.y, base.y, 1e-6f) && dir.z == base.z,
                   "the same place on the screen gives the same ray at every resolution");
        }
    }

    void testSpheres() {
        f32 depth = 0.0f;
        expect(ScenePointRay::calcDepthToSphere(Vec3f(0.0f, 0.0f, -1.0f), Vec3f(0.0f, 0.0f, -100.0f), 10.0f, &depth) && isNear(depth, 90.0f, 1e-6f),
               "a sphere straight ahead is met at its near surface");
        expect(!ScenePointRay::calcDepthToSphere(Vec3f(0.0f, 0.0f, -1.0f), Vec3f(20.0f, 0.0f, -100.0f), 10.0f, &depth),
               "a sphere beside the ray is missed");
        expect(!ScenePointRay::calcDepthToSphere(Vec3f(0.0f, 0.0f, -1.0f), Vec3f(0.0f, 0.0f, 100.0f), 10.0f, &depth),
               "a sphere behind the camera is missed");
        expect(!ScenePointRay::calcDepthToSphere(Vec3f(0.0f, 0.0f, -1.0f), Vec3f(0.0f, 0.0f, -5.0f), 10.0f, &depth),
               "a sphere around the camera is missed");

        // An oblique ray through a target's screen position meets its sphere on the near side, at its surface.
        const Projection& rProj = cProjections[0];
        const Vec3f center(400.0f, -150.0f, -2000.0f);
        const f32 radius = 110.0f;
        f32 x, y;
        project(rProj, center, 1280.0f, 720.0f, &x, &y);
        const Vec3f dir = ScenePointRay::calcViewDir(x, y, 1280.0f, 720.0f, rProj.p00, rProj.p02, rProj.p11, rProj.p12);
        expect(ScenePointRay::calcDepthToSphere(dir, center, radius, &depth), "the ray through a target's centre meets it");
        const Vec3f hit(dir.x * depth, dir.y * depth, dir.z * depth);
        const Vec3f fromCenter(hit.x - center.x, hit.y - center.y, hit.z - center.z);
        expect(isNear(sqrtf(fromCenter.dot(fromCenter)), radius, 1e-3f), "the hit lies on the sphere");
        expect(depth < -center.z, "the hit is on the near side of the sphere");
    }
}  // namespace

int main() {
    testRoundTrip();
    testEveryResolution();
    testSpheres();
    if (sFailures != 0) {
        printf("%d failed\n", sFailures);
        return 1;
    }

    printf("scene point ray: all passed\n");
    return 0;
}
