// SPDX-License-Identifier: GPL-3.0-only
// SensorHitChecker in the plain module (Game/LiveActor/SensorHitChecker.cpp, P-039): the sweep finds every pair the
// console's walk finds, and each sensor keeps the sensors it touches in the walk's order. The checker, the sensors, their
// groups and the arenas are the game's; the scene, the actors' clipping and the sensor kinds are the test's. The walk
// here is the one the plain module made before the sweep (each group's active sensors, every pair), and the test times
// both on a crowded stage.
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/SensorHitChecker.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/LumaCore.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"

#include <chrono>
#include <limits>
#include <stdio.h>
#include <string.h>
#include <vector>

#if !defined(NEBULA_PLAIN_MODULE)
#error "sensor_hit_tests tests the plain module's pairs"
#endif

namespace {
    int sFailures = 0;

    void expect(bool ok, const char* what) {
        if (!ok) {
            printf("FAILED: %s\n", what);
            sFailures++;
        }
    }

    SensorHitChecker* sChecker = nullptr;

    // The actors: only their addresses and whether each is clipped.
    const int cActorsMax = 4096;
    char sActors[cActorsMax];
    bool sClipped[cActorsMax];

    LiveActor* actor(int index) {
        return reinterpret_cast< LiveActor* >(&sActors[index]);
    }

    u64 sRandom = 0x9E3779B97F4A7C15ull;

    u32 next() {
        sRandom ^= sRandom << 13;
        sRandom ^= sRandom >> 7;
        sRandom ^= sRandom << 17;
        return static_cast< u32 >(sRandom >> 32);
    }

    f32 between(f32 low, f32 high) {
        return low + (high - low) * (static_cast< f32 >(next() >> 8) / 16777216.0f);
    }
}  // namespace

// The scene the checker lives in and what it asks of the game: the test's.
NameObj::NameObj(const char* pName) : mName(pName) {}
NameObj::~NameObj() {}
void NameObj::init(const TableIter&) {}
void NameObj::initAfterPlacement() {}
void NameObj::movement() {}
void NameObj::draw() const {}
void NameObj::calcAnim() {}
void NameObj::calcViewAndEntry() {}
NameObj* SceneObjHolder::getObj(int) const {
    return sChecker;
}
namespace MR {
    SceneObjHolder* getSceneObjHolder() {
        return nullptr;
    }
    void connectToScene(NameObj*, int, int, int, int) {}
    bool isClipped(const LiveActor* pActor) {
        return sClipped[reinterpret_cast< const char* >(pActor) - sActors];
    }
    bool isSensorPlayer(const HitSensor* pSensor) {
        return pSensor->mType == ATYPE_PLAYER;
    }
    bool isSensorRide(const HitSensor* pSensor) {
        return pSensor->mType == ATYPE_RIDE;
    }
    bool isSensorMapObj(const HitSensor* pSensor) {
        return pSensor->mType == ATYPE_MAP_OBJ;
    }
    bool isSensorRush(const HitSensor*) {
        return false;
    }
    bool isSensorAutoRush(const HitSensor*) {
        return false;
    }
}  // namespace MR
namespace lumacore {
    void setClass(NameObj*, u16) {}
}  // namespace lumacore
namespace NebulaMods {
    bool sWanted[NEBULA_MOD_EVENT_COUNT];
    void dispatch(NebulaModEvent, s32, void*, const char*) {}
}  // namespace NebulaMods

namespace {
    // The walk the plain module made before the sweep: every pair of the groups' active sensors in order.
    struct Walk {
        std::vector< SensorHitEntry > mFirst;
        std::vector< SensorHitEntry > mSecond;

        static void gather(const SensorGroup* pGroup, std::vector< SensorHitEntry >& rOut) {
            rOut.clear();
            for (s32 i = 0; i < pGroup->mSensorCount; i++) {
                HitSensor* const pSensor = pGroup->mSensors[i];
                if (pSensor->isValid() && !MR::isClipped(pSensor->mHost)) {
                    rOut.push_back({pSensor->mPosition.x, pSensor->mPosition.y, pSensor->mPosition.z, pSensor->mRadius, pSensor->mHost,
                                    pSensor});
                }
            }
        }

        static void check(const SensorHitEntry& rEntry1, const SensorHitEntry& rEntry2) {
            if (rEntry1.mHost == rEntry2.mHost) {
                return;
            }
            f32 xPos = rEntry1.mX - rEntry2.mX;
            f32 yPos = rEntry1.mY - rEntry2.mY;
            f32 zPos = rEntry1.mZ - rEntry2.mZ;
            f32 totalSize = rEntry1.mRadius + rEntry2.mRadius;
            if (!((((xPos * xPos) + (yPos * yPos)) + (zPos * zPos)) >= (totalSize * totalSize))) {
                if (!rEntry2.mSensor->isType(ATYPE_EYE)) {
                    rEntry1.mSensor->addHitSensor(rEntry2.mSensor);
                }
                if (!rEntry1.mSensor->isType(ATYPE_EYE)) {
                    rEntry2.mSensor->addHitSensor(rEntry1.mSensor);
                }
            }
        }

        void group(const SensorGroup* pGroup1, const SensorGroup* pGroup2) {
            gather(pGroup1, mFirst);
            gather(pGroup2, mSecond);
            for (const SensorHitEntry& rEntry1 : mFirst) {
                for (const SensorHitEntry& rEntry2 : mSecond) {
                    check(rEntry1, rEntry2);
                }
            }
        }

        void same(const SensorGroup* pGroup) {
            gather(pGroup, mFirst);
            for (size_t i = 0; i < mFirst.size(); i++) {
                for (size_t j = i; j < mFirst.size(); j++) {
                    check(mFirst[i], mFirst[j]);
                }
            }
        }

        // SensorHitChecker::movement's group pairs, in its order.
        void movement(const SensorHitChecker& rChecker) {
            const SensorGroup* const groups[] = {rChecker.mPlayerGroup, rChecker.mRideGroup,   rChecker.mEyeGroup,
                                                 rChecker.mSimpleGroup, rChecker.mMapObjGroup, rChecker.mCharacterGroup};
            for (const SensorGroup* pGroup : groups) {
                for (s32 i = 0; i < pGroup->mSensorCount; i++) {
                    pGroup->mSensors[i]->mSensorCount = 0;
                }
            }
            group(rChecker.mPlayerGroup, rChecker.mCharacterGroup);
            group(rChecker.mPlayerGroup, rChecker.mMapObjGroup);
            group(rChecker.mPlayerGroup, rChecker.mRideGroup);
            group(rChecker.mPlayerGroup, rChecker.mSimpleGroup);
            group(rChecker.mPlayerGroup, rChecker.mEyeGroup);
            group(rChecker.mRideGroup, rChecker.mCharacterGroup);
            group(rChecker.mRideGroup, rChecker.mMapObjGroup);
            group(rChecker.mRideGroup, rChecker.mSimpleGroup);
            group(rChecker.mRideGroup, rChecker.mEyeGroup);
            group(rChecker.mEyeGroup, rChecker.mCharacterGroup);
            group(rChecker.mEyeGroup, rChecker.mMapObjGroup);
            group(rChecker.mEyeGroup, rChecker.mSimpleGroup);
            group(rChecker.mCharacterGroup, rChecker.mMapObjGroup);
            same(rChecker.mCharacterGroup);
        }
    };

    const u32 cKinds[] = {ATYPE_PLAYER, ATYPE_RIDE, ATYPE_EYE, ATYPE_COIN, ATYPE_STAR_PIECE, ATYPE_MAP_OBJ, ATYPE_ENEMY};

    struct Scene {
        std::vector< HitSensor* > mSensors;
        u32 mPairsTouched = 0;

        // Every sensor's list from the checker against the walk's. Returns the number of hits kept.
        bool sameAsWalk(Walk& rWalk, const char* pWhat) {
            sChecker->movement();
            std::vector< std::vector< HitSensor* > > sweep(mSensors.size());
            for (size_t i = 0; i < mSensors.size(); i++) {
                sweep[i].assign(mSensors[i]->mSensors, mSensors[i]->mSensors + mSensors[i]->mSensorCount);
            }
            rWalk.movement(*sChecker);
            mPairsTouched = 0;
            bool same = true;
            for (size_t i = 0; i < mSensors.size(); i++) {
                const HitSensor* const pSensor = mSensors[i];
                mPairsTouched += pSensor->mSensorCount;
                same = same && sweep[i].size() == pSensor->mSensorCount &&
                       (pSensor->mSensorCount == 0 ||
                        memcmp(sweep[i].data(), pSensor->mSensors, pSensor->mSensorCount * sizeof(HitSensor*)) == 0);
            }
            if (!same) {
                printf("  in: %s\n", pWhat);
            }
            return same;
        }

        HitSensor* add(u32 kind, f32 radius, f32 x, f32 y, f32 z, int actorIndex) {
            HitSensor* const pSensor = new HitSensor(kind, 1 + next() % 4, radius, actor(actorIndex));
            pSensor->mPosition.x = x;
            pSensor->mPosition.y = y;
            pSensor->mPosition.z = z;
            pSensor->validateBySystem();
            mSensors.push_back(pSensor);
            return pSensor;
        }

        // Out of every group, as when the stage ends.
        void leave() {
            for (HitSensor* pSensor : mSensors) {
                pSensor->invalidateBySystem();
            }
            mSensors.clear();
        }
    };

    f32 oddValue() {
        const f32 inf = std::numeric_limits< f32 >::infinity();
        const f32 values[] = {std::numeric_limits< f32 >::quiet_NaN(), inf, -inf, 3.0e38f, -3.0e38f, 1.0e-40f, 0.0f, -0.0f};
        return values[next() % (sizeof(values) / sizeof(values[0]))];
    }

    // A scene of `count` sensors on `actors` actors, in a field `size` wide, some odd, some clipped or switched off.
    void fill(Scene& rScene, int count, int actors, f32 size, f32 radiusMax, int oddEvery) {
        for (int a = 0; a < actors; a++) {
            sClipped[a] = next() % 10 == 0;
        }
        for (int i = 0; i < count; i++) {
            const u32 kind = cKinds[next() % (sizeof(cKinds) / sizeof(cKinds[0]))];
            f32 x = between(-size, size);
            f32 y = between(-size, size);
            f32 z = between(-size, size);
            f32 radius = between(0.0f, radiusMax);
            if (oddEvery != 0 && next() % oddEvery == 0) {
                switch (next() % 5) {
                case 0: x = oddValue(); break;
                case 1: y = oddValue(); break;
                case 2: z = oddValue(); break;
                case 3: radius = oddValue(); break;
                default: radius = -between(0.0f, radiusMax); break;
                }
            }
            HitSensor* const pSensor = rScene.add(kind, radius, x, y, z, static_cast< int >(next() % actors));
            if (next() % 12 == 0) {
                pSensor->invalidate();
            }
        }
    }
}  // namespace

int main() {
    NebulaArena::create(nullptr);
    sChecker = new SensorHitChecker("SensorHitChecker");
    Walk walk;

    // Random scenes, small to crowded, with and without odd places and radii.
    bool allSame = true;
    u32 touched = 0;
    for (int round = 0; round < 60; round++) {
        Scene scene;
        const int count = 4 + static_cast< int >(next() % (round < 30 ? 120 : 1500));
        fill(scene, count, 1 + static_cast< int >(next() % 600), between(10.0f, 5000.0f), between(1.0f, 800.0f), round % 3 == 0 ? 0 : 8);
        allSame = scene.sameAsWalk(walk, "a random scene") && allSame;
        touched += scene.mPairsTouched;
        // The next step, after some sensors move and some actors are clipped.
        for (HitSensor* pSensor : scene.mSensors) {
            if (next() % 3 == 0) {
                pSensor->mPosition.x += between(-100.0f, 100.0f);
            }
        }
        sClipped[next() % 600] = true;
        allSame = scene.sameAsWalk(walk, "a random scene's next step") && allSame;
        scene.leave();
    }
    expect(allSame, "every sensor keeps the sensors the walk finds, in the walk's order");
    expect(touched > 10000, "the random scenes touch");

    // Spheres that just touch, just miss and sit on one place, along every axis, on one place far from the origin.
    {
        Scene scene;
        const f32 bases[] = {0.0f, 1.0f, -1234.5f, 98765.4f, 1.0e7f};
        int actorIndex = 0;
        for (f32 base : bases) {
            for (int i = 0; i < 40; i++) {
                const f32 radius1 = between(0.0f, 300.0f);
                const f32 radius2 = (i % 4 == 0) ? 0.0f : between(0.0f, 300.0f);
                const f32 total = radius1 + radius2;
                const f32 gaps[] = {total, nextafterf(total, 0.0f), nextafterf(total, 1.0e30f), 0.0f};
                const f32 gap = gaps[i % 4];
                const int axis = i % 3;
                f32 place[3] = {base, base * 0.5f, -base};
                scene.add(cKinds[next() % 7], radius1, place[0], place[1], place[2], actorIndex++);
                place[axis] += (i % 2 == 0) ? gap : -gap;
                scene.add(cKinds[next() % 7], radius2, place[0], place[1], place[2], actorIndex++);
            }
        }
        for (int a = 0; a < actorIndex; a++) {
            sClipped[a] = false;
        }
        expect(scene.sameAsWalk(walk, "touching edges"), "spheres that just touch or just miss pair as in the walk");
        expect(scene.mPairsTouched > 0, "the touching edges touch");
        scene.leave();
    }

    // Many sensors of one actor on one place: no pair of one actor, and none of a sensor with itself.
    {
        Scene scene;
        sClipped[0] = false;
        sClipped[1] = false;
        for (int i = 0; i < 200; i++) {
            scene.add(i % 2 == 0 ? ATYPE_ENEMY : ATYPE_MAP_OBJ, 50.0f, 10.0f, 10.0f, 10.0f, 0);
        }
        scene.add(ATYPE_ENEMY, 50.0f, 10.0f, 10.0f, 10.0f, 1);
        expect(scene.sameAsWalk(walk, "one actor's crowd"), "one actor's sensors pair as in the walk");
        expect(scene.mSensors[200]->mSensorCount == 200 && scene.mSensors[0]->mSensorCount == 1,
               "only the other actor's sensor touches the crowd");
        scene.leave();
    }

    // A crowded stage: 2400 coins and star pieces, 600 map parts, 500 characters, 300 eyes, 20 rides and the player, on
    // 3000 actors in a field 20000 across. The walk is the plain module's before P-039.
    {
        Scene scene;
        for (int a = 0; a < 3000; a++) {
            sClipped[a] = false;
        }
        struct Kind {
            u32 mType;
            int mCount;
            f32 mRadius;
        };
        const Kind kinds[] = {{ATYPE_PLAYER, 1, 80.0f},      {ATYPE_RIDE, 20, 150.0f},  {ATYPE_EYE, 300, 600.0f},
                              {ATYPE_COIN, 1600, 55.0f},     {ATYPE_STAR_PIECE, 800, 40.0f}, {ATYPE_MAP_OBJ, 600, 250.0f},
                              {ATYPE_ENEMY, 500, 120.0f}};
        for (const Kind& rKind : kinds) {
            for (int i = 0; i < rKind.mCount; i++) {
                scene.add(rKind.mType, rKind.mRadius, between(-10000.0f, 10000.0f), between(-500.0f, 500.0f), between(-10000.0f, 10000.0f),
                          static_cast< int >(next() % 3000));
            }
        }
        expect(scene.sameAsWalk(walk, "the crowded stage"), "the crowded stage pairs as in the walk");

        const int steps = 200;
        double sweepBest = 1e30;
        double walkBest = 1e30;
        for (int run = 0; run < 5; run++) {
            const auto start = std::chrono::steady_clock::now();
            for (int s = 0; s < steps; s++) {
                sChecker->movement();
            }
            const auto middle = std::chrono::steady_clock::now();
            for (int s = 0; s < steps; s++) {
                walk.movement(*sChecker);
            }
            const auto end = std::chrono::steady_clock::now();
            const double sweepTime = std::chrono::duration< double, std::micro >(middle - start).count() / steps;
            const double walkTime = std::chrono::duration< double, std::micro >(end - middle).count() / steps;
            sweepBest = sweepTime < sweepBest ? sweepTime : sweepBest;
            walkBest = walkTime < walkBest ? walkTime : walkBest;
        }
        printf("crowded stage, %zu sensors, %u hits kept: walk %.1f us a step, sweep %.1f us a step\n", scene.mSensors.size(),
               scene.mPairsTouched, walkBest, sweepBest);
        expect(sweepBest < walkBest, "the sweep is faster than the walk on the crowded stage");
        scene.leave();
    }

    if (sFailures != 0) {
        return 1;
    }
    printf("sensor_hit_tests: all passed\n");
    return 0;
}
