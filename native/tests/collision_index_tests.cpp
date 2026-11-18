// SPDX-License-Identifier: GPL-3.0-only
// The plain module's collision parts by place (Game/Map/CollisionPartIndex.cpp, P-129): for random parts (small,
// large, huge, and some with a place or radius that is not a number) and random point, ball and box queries, the walk
// gives every part the keeper's own test takes, in list order, as the walk of every part does; it stays so as parts
// move, change their radius, and leave or join the zone; and among many parts a point's query looks at far fewer than all.
#include "Game/Map/CollisionCategorizedKeeper.hpp"
#include "Game/Map/CollisionParts.hpp"
#include "Game/Map/CollisionPartIndex.hpp"
#include "Game/System/NebulaArena.hpp"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <vector>

// The parts and zone as the test makes them: their files are not linked here.
CollisionParts::CollisionParts() : mBoundingRadius(0.0f), mZone(nullptr), mListZone(nullptr) {
    mBaseMatrix.identity();
}

Vec3f CollisionParts::getTrans() {
    Vec3f translation;
    mBaseMatrix.getTrans(translation);
    return translation;
}

CollisionZone::CollisionZone(s32 zoneID) : mZoneID(zoneID), mCenter(0, 0, 0), mRadius(), mMin(0, 0, 0), mMax(0, 0, 0) {
    mChanges = 0;
}

namespace {
    int sFailures = 0;

    void expect(bool ok, const char* what) {
        if (!ok) {
            printf("FAILED: %s\n", what);
            sFailures++;
        }
    }

    uint32_t sSeed = 1;

    u32 random(u32 below) {
        sSeed = sSeed * 1664525u + 1013904223u;
        return static_cast< u32 >((static_cast< u64 >(sSeed >> 8) * below) >> 24);
    }

    f32 between(f32 low, f32 high) {
        return low + (high - low) * static_cast< f32 >(random(1u << 20)) / static_cast< f32 >(1u << 20);
    }

    // The keeper's tests: a point's and a ball's (CollisionCategorizedKeeper::checkStrikePoint, collectStrikeBall)
    // and a box's (collectStrikeLine, createAreaPolygonList: isSphereOverlappingWithBox).
    bool ballTakes(CollisionParts* pParts, const Vec3f& rPosition, f32 ballRadius) {
        const f32 radius = ballRadius + pParts->mBoundingRadius;
        Vec3f offset;
        offset.x = fabsf(pParts->getTrans().x - rPosition.x);
        if (radius < offset.x) {
            return false;
        }
        offset.y = fabsf(pParts->getTrans().y - rPosition.y);
        if (radius < offset.y) {
            return false;
        }
        offset.z = fabsf(pParts->getTrans().z - rPosition.z);
        if (radius < offset.z) {
            return false;
        }
        return !(offset.squared() > radius * radius);
    }

    bool boxTakes(CollisionParts* pParts, const Vec3f& rMin, const Vec3f& rMax) {
        const Vec3f center(pParts->getTrans());
        const f32 radius = pParts->mBoundingRadius;
        if (center.x < rMin.x - radius || rMax.x + radius < center.x) {
            return false;
        }
        if (center.y < rMin.y - radius || rMax.y + radius < center.y) {
            return false;
        }
        if (center.z < rMin.z - radius || rMax.z + radius < center.z) {
            return false;
        }
        return true;
    }

    void place(CollisionParts* pParts, f32 spread) {
        const u32 kind = random(100);
        f32 radius;
        if (kind < 70) {
            radius = between(50.0f, 600.0f);
        } else if (kind < 95) {
            radius = between(600.0f, 5000.0f);
        } else if (kind < 99) {
            radius = between(20000.0f, 200000.0f);
        } else {
            const u32 odd = random(4);
            radius = odd == 0 ? NAN : odd == 1 ? -1.0f : odd == 2 ? INFINITY : 300.0f;
            if (odd == 3) {
                pParts->mBaseMatrix.setTrans(NAN, 0.0f, 0.0f);
                pParts->mBoundingRadius = radius;
                return;
            }
        }
        pParts->mBaseMatrix.setTrans(between(-spread, spread), between(-spread / 4, spread / 4), between(-spread, spread));
        pParts->mBoundingRadius = radius;
    }

    // Parts a query's walk gives that the test takes, in order, against the walk of every part.
    bool sameAsWalk(CollisionZone* pZone, const Vec3f& rMin, const Vec3f& rMax, bool ball, const Vec3f& rCenter, f32 ballRadius, s64* pLooked) {
        std::vector< s32 > walked, all;
        CollisionPartWalk walk(pZone, rMin, rMax);
        s32 last = -1;
        for (s32 i; (i = walk.next()) >= 0;) {
            if (i <= last || i >= pZone->mParts.size()) {
                return false;
            }
            last = i;
            (*pLooked)++;
            CollisionParts* const pParts = pZone->mParts[i];
            if (ball ? ballTakes(pParts, rCenter, ballRadius) : boxTakes(pParts, rMin, rMax)) {
                walked.push_back(i);
            }
        }
        for (s32 i = 0; i < pZone->mParts.size(); i++) {
            CollisionParts* const pParts = pZone->mParts[i];
            if (ball ? ballTakes(pParts, rCenter, ballRadius) : boxTakes(pParts, rMin, rMax)) {
                all.push_back(i);
            }
        }
        return walked == all;
    }

    // `count` parts in a zone of `spread`; rounds of queries, each round after some parts moved, changed radius, left or
    // joined. Returns the parts the walks looked at, a query on average.
    double testZone(u32 count, f32 spread, u32 seed, double* pAll) {
        sSeed = seed;
        std::vector< CollisionParts* > parts;
        CollisionZone* const pZone = new CollisionZone(1);
        for (u32 i = 0; i < count; i++) {
            CollisionParts* const pParts = new CollisionParts();
            place(pParts, spread);
            parts.push_back(pParts);
            pZone->mParts.push_back(pParts);
            pParts->mListZone = pZone;
            pZone->mChanges++;
        }

        bool ok = true;
        s64 looked = 0;
        s64 queries = 0;
        s64 pointLooked = 0;
        s64 points = 0;
        for (u32 round = 0; round < 40 && ok; round++) {
            for (u32 q = 0; q < 200 && ok; q++) {
                const Vec3f at(between(-spread, spread), between(-spread / 4, spread / 4), between(-spread, spread));
                const u32 kind = random(4);
                if (kind == 0) {
                    const s64 before = looked;
                    ok = sameAsWalk(pZone, at, at, true, at, 0.0f, &looked);
                    pointLooked += looked - before;
                    points++;
                } else if (kind == 1) {
                    const f32 r = random(10) == 0 ? between(2000.0f, 40000.0f) : between(0.0f, 300.0f);
                    const Vec3f lo(at.x - r, at.y - r, at.z - r), hi(at.x + r, at.y + r, at.z + r);
                    ok = sameAsWalk(pZone, lo, hi, true, at, r, &looked);
                } else {
                    // A line's or an area's box, sometimes long.
                    const f32 lx = random(8) == 0 ? between(0.0f, 60000.0f) : between(0.0f, 800.0f);
                    const Vec3f hi(at.x + lx, at.y + between(0.0f, 400.0f), at.z + between(0.0f, 400.0f));
                    ok = sameAsWalk(pZone, at, hi, false, at, 0.0f, &looked);
                }
                queries++;
            }

            // Changes as the game makes them: a part moves or takes a new radius (CollisionParts tells its zone), or
            // leaves the zone (the last part takes its place) or joins it.
            const u32 changes = random(6);
            for (u32 c = 0; c < changes; c++) {
                const u32 what = random(3);
                if (what == 0) {
                    place(parts[random(static_cast< u32 >(parts.size()))], spread);
                    pZone->mChanges++;
                } else if (what == 1 && pZone->mParts.size() > 1) {
                    const s32 at = static_cast< s32 >(random(static_cast< u32 >(pZone->mParts.size())));
                    pZone->mParts[at] = pZone->mParts[pZone->mParts.size() - 1];
                    pZone->mParts.pop_back();
                    pZone->mChanges++;
                } else {
                    CollisionParts* const pParts = new CollisionParts();
                    place(pParts, spread);
                    parts.push_back(pParts);
                    pZone->mParts.push_back(pParts);
                    pZone->mChanges++;
                }
            }
        }

        char what[96];
        snprintf(what, sizeof(what), "the walk gives what the full walk takes, in order (%u parts)", count);
        expect(ok, what);
        *pAll = static_cast< double >(looked) / static_cast< double >(queries > 0 ? queries : 1);
        return static_cast< double >(pointLooked) / static_cast< double >(points > 0 ? points : 1);
    }
}  // namespace

int main() {
    NebulaArena::create(nullptr);
    // A point's query looks at few parts; a query over a wide box (more cells than the index serves) looks at all.
    double fewAll = 0.0, someAll = 0.0, manyAll = 0.0;
    const double few = testZone(30, 20000.0f, 11u, &fewAll);
    const double some = testZone(300, 60000.0f, 12u, &someAll);
    const double many = testZone(3000, 200000.0f, 13u, &manyAll);
    printf("parts a point's query looks at: %.1f of about 30, %.1f of about 300, %.1f of about 3000\n", few, some, many);
    printf("parts any query looks at: %.1f, %.1f, %.1f\n", fewAll, someAll, manyAll);
    expect(many * 10.0 < 3000.0, "among many parts a point's query looks at far fewer than all");
    if (sFailures != 0) {
        printf("%d failed\n", sFailures);
        return 1;
    }
    printf("collision index: ok\n");
    return 0;
}
