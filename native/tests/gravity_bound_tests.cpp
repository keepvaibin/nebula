// SPDX-License-Identifier: GPL-3.0-only
// The plain module's bounds (P-139, P-152): a gravity's sphere holds every point its own test gives a gravity at, and an
// area form's sphere every point inside the form, for every shape, moved, turned and scaled; and the bounds leave out
// most of the far points, so the searches pass them. The shapes and forms are the game's.
#include "Game/AreaObj/AreaForm.hpp"
#include "Game/Gravity.hpp"
#include "Game/System/NebulaArena.hpp"

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/Table.hpp"

// The forms' init reads a placement; the test sets their fields itself and reads none.
namespace MR {
    bool getPlacementTrans(const TableIter&, Vec3f*) {
        return false;
    }
    bool getPlacementRotate(const TableIter&, Vec3f*) {
        return false;
    }
    bool getPlacementScale(const TableIter&, Vec3f*) {
        return false;
    }
}  // namespace MR
s32 Table::searchItemInfo(const char*) const {
    return -1;
}

namespace {
    int sFailures = 0;

    void expect(bool ok, const char* what) {
        if (!ok) {
            printf("FAILED: %s\n", what);
            sFailures++;
        }
    }

    uint32_t sSeed = 12345;

    f32 random01() {
        sSeed = sSeed * 1664525u + 1013904223u;
        return static_cast< f32 >(sSeed >> 8) / 16777216.0f;
    }

    f32 between(f32 low, f32 high) {
        return low + (high - low) * random01();
    }

    Vec3f randomVec(f32 size) {
        return Vec3f(between(-size, size), between(-size, size), between(-size, size));
    }

    Vec3f randomDir() {
        for (;;) {
            Vec3f v = randomVec(1.0f);
            const f32 length = v.length();
            if (length > 0.1f && length <= 1.0f) {
                return v * (1.0f / length);
            }
        }
    }

    // A turn (a unit quaternion's matrix), a scale (the same on every axis, or per axis when `uneven`) and a move.
    Pos3f randomMtx(bool uneven) {
        f32 q[4];
        f32 length = 0.0f;
        do {
            length = 0.0f;
            for (int i = 0; i < 4; i++) {
                q[i] = between(-1.0f, 1.0f);
                length += q[i] * q[i];
            }
        } while (length < 0.01f || length > 1.0f);
        length = sqrtf(length);
        const f32 w = q[0] / length, x = q[1] / length, y = q[2] / length, z = q[3] / length;
        const f32 r[3][3] = {
            {1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)},
            {2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)},
            {2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)},
        };
        const f32 scale = between(0.5f, 3.0f);
        f32 axis[3] = {scale, scale, scale};
        if (uneven) {
            for (int c = 0; c < 3; c++) {
                axis[c] = between(0.3f, 3.0f);
            }
        }

        Pos3f mtx;
        mtx.identity();
        for (int row = 0; row < 3; row++) {
            for (int c = 0; c < 3; c++) {
                mtx.mMtx[row][c] = r[row][c] * axis[c];
            }
        }
        const Vec3f move = randomVec(20000.0f);
        mtx.mMtx[0][3] = move.x;
        mtx.mMtx[1][3] = move.y;
        mtx.mMtx[2][3] = move.z;
        return mtx;
    }

    // Points round `pGravity`: near it, on its scale, and far. Every point its test gives a gravity at must be inside
    // its bound. Counts the points the bound leaves out.
    struct Tally {
        u32 points;
        u32 left;
        u32 applied;
    };

    void probe(PlanetGravity* pGravity, const char* pName, Tally* pTally) {
        // The middle of the bound when there is one (else the origin), so the points fall round the shape.
        const Vec3f center = pGravity->mBoundRadiusSquared >= 0.0f ? pGravity->mBoundCenter : Vec3f(0.0f, 0.0f, 0.0f);
        const f32 reach = pGravity->mBoundRadiusSquared >= 0.0f ? sqrtf(pGravity->mBoundRadiusSquared) : 10000.0f;
        for (int i = 0; i < 400; i++) {
            const f32 size = i < 200 ? reach * 1.2f : reach * 4.0f;
            const Vec3f point = center + randomVec(size);
            Vec3f gravity;
            const bool applies = pGravity->calcGravity(&gravity, point);
            const bool inside = pGravity->isInBound(point);
            pTally->points++;
            pTally->left += inside ? 0 : 1;
            pTally->applied += applies ? 1 : 0;
            if (applies && !inside) {
                printf("  %s: gravity at %f %f %f, outside its bound (center %f %f %f, radius %f)\n", pName, point.x, point.y, point.z,
                       pGravity->mBoundCenter.x, pGravity->mBoundCenter.y, pGravity->mBoundCenter.z, sqrtf(pGravity->mBoundRadiusSquared));
                expect(false, "a gravity applies outside its bound");
            }
        }
    }

    void settle(PlanetGravity* pGravity, bool follow) {
        if (follow) {
            pGravity->updateMtx(randomMtx(false));
            pGravity->refreshBound();
        } else {
            pGravity->updateIdentityMtx();
        }
    }

    void setRange(PlanetGravity* pGravity) {
        pGravity->mRange = random01() < 0.15f ? -1.0f : between(0.0f, 3000.0f);
        pGravity->mDistant = random01() < 0.5f ? 0.0f : between(-200.0f, 800.0f);
    }

    void testGravities() {
        Tally tally = {};
        for (int round = 0; round < 60; round++) {
            const bool follow = (round & 1) != 0;

            PointGravity point;
            point.mOrigPosition = randomVec(5000.0f);
            setRange(&point);
            settle(&point, follow);
            probe(&point, "point", &tally);

            SegmentGravity segment;
            segment.setGravityPoint(0, randomVec(5000.0f));
            segment.setGravityPoint(1, randomVec(5000.0f));
            segment.setSideVector(randomDir());
            segment.setValidSideDegree(between(0.0f, 360.0f));
            segment.setEdgeValid(0, random01() < 0.7f);
            segment.setEdgeValid(1, random01() < 0.7f);
            setRange(&segment);
            settle(&segment, follow);
            probe(&segment, "segment", &tally);

            DiskGravity disk;
            disk.setLocalPosition(randomVec(5000.0f));
            disk.setLocalDirection(randomDir());
            disk.setSideDirection(randomDir());
            disk.setRadius(between(0.0f, 4000.0f));
            disk.setValidDegree(between(0.0f, 360.0f));
            disk.setBothSide(random01() < 0.5f);
            disk.setEnableEdgeGravity(random01() < 0.7f);
            setRange(&disk);
            settle(&disk, follow);
            probe(&disk, "disk", &tally);

            DiskTorusGravity torus;
            torus.setPosition(randomVec(5000.0f));
            torus.setDirection(randomDir());
            torus.setRadius(between(0.0f, 4000.0f));
            torus.setDiskRadius(between(-100.0f, 6000.0f));
            torus.setEdgeType(static_cast< s32 >(between(0.0f, 3.99f)));
            torus.setBothSide(random01() < 0.5f);
            setRange(&torus);
            settle(&torus, follow);
            probe(&torus, "torus", &tally);

            ConeGravity cone;
            Pos3f coneMtx = randomMtx(true);
            for (int row = 0; row < 3; row++) {
                for (int c = 0; c < 3; c++) {
                    coneMtx.mMtx[row][c] *= 1000.0f;
                }
            }
            cone.setLocalMatrix(coneMtx);
            cone.setEnableBottom(random01() < 0.7f);
            cone.setTopCutRate(random01() < 0.5f ? 0.0f : between(0.0f, 1.0f));
            setRange(&cone);
            settle(&cone, follow);
            probe(&cone, "cone", &tally);

            CubeGravity cube;
            Pos3f cubeMtx = randomMtx(true);
            for (int row = 0; row < 3; row++) {
                for (int c = 0; c < 3; c++) {
                    cubeMtx.mMtx[row][c] *= 800.0f;
                }
            }
            if (round % 5 == 4) {
                cubeMtx.mMtx[0][1] += 300.0f;  // a slanted box: no bound
            }
            cube.setCube(cubeMtx);
            cube.mActiveFaces = static_cast< u8 >(random01() < 0.5f ? 0x3f : static_cast< u32 >(between(0.0f, 63.99f)));
            setRange(&cube);
            settle(&cube, follow);
            probe(&cube, "cube", &tally);

            for (int type = 0; type < 3; type++) {
                ParallelGravity parallel;
                parallel.setPlane(randomDir(), randomVec(5000.0f));
                parallel.setRangeType(static_cast< ParallelGravity::RANGE_TYPE >(type));
                if (type == ParallelGravity::RangeType_Box) {
                    Pos3f boxMtx = randomMtx(true);
                    for (int row = 0; row < 3; row++) {
                        for (int c = 0; c < 3; c++) {
                            boxMtx.mMtx[row][c] *= 1000.0f;
                        }
                    }
                    parallel.setRangeBox(boxMtx);
                }
                parallel.setRangeCylinder(between(0.0f, 3000.0f), between(-500.0f, 3000.0f));
                parallel.setBaseDistance(between(0.0f, 3000.0f));
                setRange(&parallel);
                settle(&parallel, follow);
                probe(&parallel, "parallel", &tally);
            }

            WireGravity wire;
            const u32 points = static_cast< u32 >(between(2.0f, 8.99f));
            wire.setPointListSize(points);
            Vec3f at = randomVec(5000.0f);
            for (u32 i = 0; i < points; i++) {
                wire.addPoint(at);
                at += randomVec(1500.0f);
            }
            setRange(&wire);
            settle(&wire, follow);
            probe(&wire, "wire", &tally);
        }
        printf("gravity: %u points, %u gave a gravity, %u left out by the bounds\n", tally.points, tally.applied, tally.left);
        expect(tally.applied > tally.points / 20, "the shapes give gravity at some of the points");
        expect(tally.left > tally.points / 4, "the bounds leave out the far points");
    }

    void testAreaForms() {
        u32 points = 0, inside = 0, left = 0;
        for (int round = 0; round < 200; round++) {
            AreaFormCube cube(round & 1);
            cube.mTranslation = randomVec(5000.0f);
            cube.mRotation = randomVec(180.0f);
            cube.mScale = Vec3f(between(0.1f, 5.0f), between(0.1f, 5.0f), between(0.1f, 5.0f));
            cube.updateBoxParam();

            AreaFormSphere sphere;
            sphere.mTranslation = randomVec(5000.0f);
            sphere.mRadius = between(0.0f, 3000.0f);

            AreaFormBowl bowl;
            bowl.mTranslation = randomVec(5000.0f);
            bowl.mRadius = between(0.0f, 3000.0f);
            bowl.calcUpVec(randomVec(180.0f));

            AreaFormCylinder cylinder;
            cylinder.mTranslation = randomVec(5000.0f);
            cylinder.calcDir(randomVec(180.0f));
            cylinder.mRadius = between(0.0f, 3000.0f);
            cylinder.mHeight = between(-1000.0f, 3000.0f);

            const AreaForm* forms[] = {&cube, &sphere, &bowl, &cylinder};
            for (const AreaForm* pForm : forms) {
                Vec3f center;
                f32 radius;
                expect(pForm->calcBound(&center, &radius), "a form that follows nothing has a bound");
                const f32 room = radius * 1.001f + 1.0f;
                for (int i = 0; i < 200; i++) {
                    const Vec3f point = center + randomVec(i < 100 ? radius * 1.2f + 10.0f : radius * 4.0f + 10.0f);
                    const bool in = pForm->isInVolume(point);
                    const bool held = !((point - center).squared() > room * room);
                    points++;
                    inside += in ? 1 : 0;
                    left += held ? 0 : 1;
                    if (in && !held) {
                        printf("  form %d: %f %f %f inside, outside its bound\n", static_cast< int >(pForm - forms[0]), point.x, point.y, point.z);
                        expect(false, "a point inside a form is outside its bound");
                    }
                }
            }

            Pos3f follow = randomMtx(false);
            cube.mFollowMtx = &follow;
            Vec3f center;
            f32 radius;
            expect(!cube.calcBound(&center, &radius), "a form that follows a matrix has no bound");
        }
        printf("areas: %u points, %u inside, %u left out by the bounds\n", points, inside, left);
        expect(inside > points / 20, "the forms hold some of the points");
        expect(left > points / 4, "the bounds leave out the far points");
    }
}  // namespace

int main() {
    NebulaArena::create(nullptr);
    testGravities();
    testAreaForms();
    if (sFailures != 0) {
        printf("%d failed\n", sFailures);
        return 1;
    }
    printf("gravity and area bounds: ok\n");
    return 0;
}
