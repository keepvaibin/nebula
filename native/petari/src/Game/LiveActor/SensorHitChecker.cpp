#include "Game/LiveActor/SensorHitChecker.hpp"
#include "Game/Util/Array.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/LumaCore.hpp"

#include <algorithm>

namespace {
    bool isSensorActive(const HitSensor* pSensor) {
        return pSensor->isValid() && !MR::isClipped(pSensor->mHost);
    }

    inline SensorHitEntry entryOf(HitSensor* pSensor) {
        return {pSensor->mPosition.x, pSensor->mPosition.y, pSensor->mPosition.z, pSensor->getRadius(), pSensor->mHost, pSensor};
    }

    // checkAttack's test: two sensors of different hosts whose spheres are not apart. Written as not-outside so a NaN
    // distance counts as a hit.
    inline bool touches(const SensorHitEntry& rEntry1, const SensorHitEntry& rEntry2) {
        if (rEntry1.mHost == rEntry2.mHost) {
            return false;
        }

        f32 xPos = rEntry1.mX - rEntry2.mX;
        f32 yPos = rEntry1.mY - rEntry2.mY;
        f32 zPos = rEntry1.mZ - rEntry2.mZ;
        f32 totalSize = rEntry1.mRadius + rEntry2.mRadius;

        return !((((xPos * xPos) + (yPos * yPos)) + (zPos * zPos)) >= (totalSize * totalSize));
    }

    // checkAttack's calls on a hit: each sensor keeps the other, except that nothing keeps an eye.
    inline void addPair(const SensorHitEntry& rEntry1, const SensorHitEntry& rEntry2) {
        if (!rEntry2.mSensor->isType(ATYPE_EYE)) {
            rEntry1.mSensor->addHitSensor(rEntry2.mSensor);
        }

        if (!rEntry1.mSensor->isType(ATYPE_EYE)) {
            rEntry2.mSensor->addHitSensor(rEntry1.mSensor);
        }
    }

    inline void checkAttackEntries(const SensorHitEntry& rEntry1, const SensorHitEntry& rEntry2) {
        if (touches(rEntry1, rEntry2)) {
            addPair(rEntry1, rEntry2);
        }
    }

#if defined(NEBULA_PLAIN_MODULE)
    // The steps of laying out a group of `count` sensors for the sweep, as the sort's comparisons: count times the bits
    // of count. None when the group is laid out this frame already.
    inline s64 layOutSteps(const SensorGroup* pGroup) {
        if (pGroup->mSweepMade) {
            return 0;
        }

        s64 bits = 0;
        for (u32 count = static_cast< u32 >(pGroup->mActiveCount); count != 0; count >>= 1) {
            bits++;
        }
        return static_cast< s64 >(pGroup->mActiveCount) * bits;
    }

    // A number that is neither infinite nor NaN (both give NaN here).
    inline bool isFiniteValue(f32 value) {
        return value - value == 0.0f;
    }

    // A sensor the sweep places by its extent along x: a finite place and a finite radius that is not negative. Any
    // other (a NaN or infinite place, a negative or infinite radius) can pass the test with a sensor anywhere, so it is
    // paired with every sensor.
    inline bool isPlaced(const SensorHitEntry& rEntry) {
        return isFiniteValue(rEntry.mX) && isFiniteValue(rEntry.mY) && isFiniteValue(rEntry.mZ) && isFiniteValue(rEntry.mRadius) &&
               rEntry.mRadius >= 0.0f;
    }

    // A pair that touched, as (first index << 32 | second index): sorted, the order the pairs are walked in.
    inline u64 pairKey(s32 first, s32 second) {
        return (static_cast< u64 >(static_cast< u32 >(first)) << 32) | static_cast< u32 >(second);
    }
#endif
};  // namespace

SensorGroup::SensorGroup(int maxSensors)
    : mMaxSensors(maxSensors), mSensorCount(0), mSensors(new HitSensor*[maxSensors]()), mActiveCount(0),
#if defined(NEBULA_PLAIN_MODULE)
      mActive(new SensorHitEntry[maxSensors]), mSweepMade(false), mSweepCount(0), mSweep(new SensorSweepItem[maxSensors]),
      mUnplacedCount(0), mUnplaced(new s32[maxSensors]) {
#else
      mActive(nullptr), mSweepMade(false), mSweepCount(0), mSweep(nullptr), mUnplacedCount(0), mUnplaced(nullptr) {
#endif
}

// Once a frame, before any pair: nothing in the pass changes a sensor's validity, place or radius or an actor's clipping
// (a hit only adds to the sensors' hit lists), so the sensors active here are the ones the pairs would find active.
void SensorGroup::gatherActive() {
    mActiveCount = 0;
    mSweepMade = false;
    for (s32 i = 0; i < mSensorCount; i++) {
        HitSensor* const pSensor = mSensors[i];
        if (::isSensorActive(pSensor)) {
            mActive[mActiveCount++] = ::entryOf(pSensor);
        }
    }
}

#if defined(NEBULA_PLAIN_MODULE)
// The plain module's sweep (P-039): the active sensors with a place laid out along x, sorted by where each starts, once a
// frame, on the group's first sweep; a frame whose pairs are all walked lays out nothing.
void SensorGroup::layOutSweep() {
    if (mSweepMade) {
        return;
    }

    mSweepCount = 0;
    mUnplacedCount = 0;
    for (s32 i = 0; i < mActiveCount; i++) {
        const SensorHitEntry& rEntry = mActive[i];
        if (::isPlaced(rEntry)) {
            // Wider than the sphere: the test's float rounding can find a pair touching whose true distance is a few
            // parts in ten million past the radii's sum, so the extent is grown by far more than that (a part in a
            // hundred thousand of the radius, a part in a billion of the place). A wider extent only adds candidates
            // the test then turns away.
            const f64 x = rEntry.mX;
            const f64 reach = static_cast< f64 >(rEntry.mRadius) * (1.0 + 1e-5) + (x < 0.0 ? -x : x) * 1e-9;
            mSweep[mSweepCount++] = {x - reach, x + reach, i};
        } else {
            mUnplaced[mUnplacedCount++] = i;
        }
    }
    std::sort(mSweep, mSweep + mSweepCount, [](const SensorSweepItem& rA, const SensorSweepItem& rB) {
        return rA.mLow < rB.mLow || (rA.mLow == rB.mLow && rA.mIndex < rB.mIndex);
    });
    mSweepMade = true;
}
#endif

void SensorGroup::add(HitSensor* pSensor) {
    if (mSensorCount >= mMaxSensors) {
        mSensors = MR::growArray(mSensors, mMaxSensors);
        if (mActive != nullptr) {
            mActive = MR::growArray(mActive, mMaxSensors);
            mSweep = MR::growArray(mSweep, mMaxSensors);
            mUnplaced = MR::growArray(mUnplaced, mMaxSensors);
        }
        mMaxSensors *= 2;
    }

    mSensors[mSensorCount] = pSensor;
    mSensorCount++;
    pSensor->mSensorGroup = this;
}

// Unordered: the last sensor takes the removed one's place.
void SensorGroup::remove(HitSensor* pSensor) {
    for (s32 i = 0; i < mSensorCount; i++) {
        if (mSensors[i] == pSensor) {
            mSensorCount--;
            mSensors[i] = mSensors[mSensorCount];
            break;
        }
    }
}

void SensorGroup::clear() const {
    for (s32 i = 0; i < mSensorCount; i++) {
        mSensors[i]->mSensorCount = 0;
    }
}

void SensorHitChecker::init(const TableIter& /*rIter*/) {
    MR::connectToScene(this, MR::MovementType_SensorHitChecker, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_None);
}

void SensorHitChecker::initGroup(HitSensor* pSensor) {
    if (MR::isSensorPlayer(pSensor)) {
        pSensor->mSensorGroup = mPlayerGroup;
    } else if (MR::isSensorRide(pSensor)) {
        pSensor->mSensorGroup = mRideGroup;
    } else if (pSensor->isType(ATYPE_EYE)) {
        pSensor->mSensorGroup = mEyeGroup;
    } else if (pSensor->isType(ATYPE_COIN) || pSensor->isType(ATYPE_STAR_PIECE) || pSensor->isType(ATYPE_ENEMY_SIMPLE) ||
               pSensor->isType(ATYPE_MAP_OBJ_SIMPLE) || pSensor->isType(ATYPE_PLAYER_AUTO_JUMP) || MR::isSensorRush(pSensor) ||
               MR::isSensorAutoRush(pSensor)) {
        pSensor->mSensorGroup = mSimpleGroup;
    } else if (MR::isSensorMapObj(pSensor)) {
        pSensor->mSensorGroup = mMapObjGroup;
    } else {
        pSensor->mSensorGroup = mCharacterGroup;
    }
}

void SensorHitChecker::movement() {
    mPlayerGroup->clear();
    mRideGroup->clear();
    mEyeGroup->clear();
    mSimpleGroup->clear();
    mMapObjGroup->clear();
    mCharacterGroup->clear();
#if defined(NEBULA_PLAIN_MODULE)
    mPlayerGroup->gatherActive();
    mRideGroup->gatherActive();
    mEyeGroup->gatherActive();
    mSimpleGroup->gatherActive();
    mMapObjGroup->gatherActive();
    mCharacterGroup->gatherActive();
#endif
    doObjColGroup(mPlayerGroup, mCharacterGroup);
    doObjColGroup(mPlayerGroup, mMapObjGroup);
    doObjColGroup(mPlayerGroup, mRideGroup);
    doObjColGroup(mPlayerGroup, mSimpleGroup);
    doObjColGroup(mPlayerGroup, mEyeGroup);
    doObjColGroup(mRideGroup, mCharacterGroup);
    doObjColGroup(mRideGroup, mMapObjGroup);
    doObjColGroup(mRideGroup, mSimpleGroup);
    doObjColGroup(mRideGroup, mEyeGroup);
    doObjColGroup(mEyeGroup, mCharacterGroup);
    doObjColGroup(mEyeGroup, mMapObjGroup);
    doObjColGroup(mEyeGroup, mSimpleGroup);
    doObjColGroup(mCharacterGroup, mMapObjGroup);
    doObjColInSameGroup(mCharacterGroup);
}

#if defined(NEBULA_PLAIN_MODULE)
// The pairs that touched, sorted, and each one's calls: the order of the console's walk (the first group's sensors in
// order, each with the second's in order), so every sensor keeps the sensors it touches in the console's order.
void SensorHitChecker::addFoundPairs(const SensorHitEntry* pActive1, const SensorHitEntry* pActive2) const {
    u64* const pFound = mFound.mItems;
    std::sort(pFound, pFound + mFoundCount);
    for (u32 k = 0; k < mFoundCount; k++) {
        ::addPair(pActive1[static_cast< s32 >(pFound[k] >> 32)], pActive2[static_cast< s32 >(pFound[k] & 0xFFFFFFFFu)]);
    }
}

void SensorHitChecker::testPair(const SensorHitEntry* pActive1, s32 first, const SensorHitEntry* pActive2, s32 second) const {
    if (::touches(pActive1[first], pActive2[second])) {
        mFound.room(mFoundCount + 1)[mFoundCount] = ::pairKey(first, second);
        mFoundCount++;
    }
}
#endif

// Every active pair in the order of the groups' sensors. The plain module walks each group's array of active sensors when
// the pairs are no more than the sweep's steps (laying out the groups not yet laid out this frame, then one pass over
// both); past that it sweeps the two groups' sensors along x, tests only the pairs whose extents overlap there (and the
// unplaced sensors with every sensor), and makes the hits' calls in the walk's order (P-039). A module
// with hook points calls isSensorActive and checkAttack for every pair, as the game does, so a mod that hooks them sees
// every pair.
void SensorHitChecker::doObjColGroup(SensorGroup* pGroup1, SensorGroup* pGroup2) const {
#if defined(NEBULA_PLAIN_MODULE)
    const s32 group1Count = pGroup1->mActiveCount;
    const s32 group2Count = pGroup2->mActiveCount;
    const SensorHitEntry* const pActive1 = pGroup1->mActive;
    const SensorHitEntry* const pActive2 = pGroup2->mActive;
    if (static_cast< s64 >(group1Count) * group2Count <=
        ::layOutSteps(pGroup1) + ::layOutSteps(pGroup2) + group1Count + group2Count) {
        for (s32 i = 0; i < group1Count; i++) {
            for (s32 j = 0; j < group2Count; j++) {
                ::checkAttackEntries(pActive1[i], pActive2[j]);
            }
        }
        return;
    }

    // Each pair of placed sensors whose extents overlap, once: the one of the two that starts first meets every sensor
    // of the other group that starts before it ends.
    pGroup1->layOutSweep();
    pGroup2->layOutSweep();
    mFoundCount = 0;
    const SensorSweepItem* const pSweep1 = pGroup1->mSweep;
    const SensorSweepItem* const pSweep2 = pGroup2->mSweep;
    const s32 sweep1Count = pGroup1->mSweepCount;
    const s32 sweep2Count = pGroup2->mSweepCount;
    s32 i = 0;
    s32 j = 0;
    while (i < sweep1Count && j < sweep2Count) {
        if (pSweep1[i].mLow < pSweep2[j].mLow) {
            for (s32 k = j; k < sweep2Count && pSweep2[k].mLow <= pSweep1[i].mHigh; k++) {
                testPair(pActive1, pSweep1[i].mIndex, pActive2, pSweep2[k].mIndex);
            }
            i++;
        } else {
            for (s32 k = i; k < sweep1Count && pSweep1[k].mLow <= pSweep2[j].mHigh; k++) {
                testPair(pActive1, pSweep1[k].mIndex, pActive2, pSweep2[j].mIndex);
            }
            j++;
        }
    }

    // The unplaced: the first group's with every sensor of the second, the second's with the first group's placed ones.
    for (s32 u = 0; u < pGroup1->mUnplacedCount; u++) {
        for (s32 k = 0; k < group2Count; k++) {
            testPair(pActive1, pGroup1->mUnplaced[u], pActive2, k);
        }
    }
    for (s32 u = 0; u < pGroup2->mUnplacedCount; u++) {
        for (s32 k = 0; k < group1Count; k++) {
            if (::isPlaced(pActive1[k])) {
                testPair(pActive1, k, pActive2, pGroup2->mUnplaced[u]);
            }
        }
    }
    addFoundPairs(pActive1, pActive2);
#else
    const s32 group1Count = pGroup1->mSensorCount;
    for (s32 i = 0; i < group1Count; i++) {
        HitSensor* const pSensor1 = pGroup1->mSensors[i];
        if (!::isSensorActive(pSensor1)) {
            continue;
        }

        const s32 group2Count = pGroup2->mSensorCount;
        for (s32 j = 0; j < group2Count; j++) {
            HitSensor* const pSensor2 = pGroup2->mSensors[j];
            if (::isSensorActive(pSensor2)) {
                checkAttack(pSensor1, pSensor2);
            }
        }
    }
#endif
}

// Pairs each sensor with itself too; the test skips sensors of one host. The plain module's sweep leaves those pairs out
// for that reason.
void SensorHitChecker::doObjColInSameGroup(SensorGroup* pSensorGroup) const {
#if defined(NEBULA_PLAIN_MODULE)
    const s32 count = pSensorGroup->mActiveCount;
    const SensorHitEntry* const pActive = pSensorGroup->mActive;
    if (static_cast< s64 >(count) * (count + 1) / 2 <= ::layOutSteps(pSensorGroup) + count) {
        for (s32 i = 0; i < count; i++) {
            for (s32 j = i; j < count; j++) {
                ::checkAttackEntries(pActive[i], pActive[j]);
            }
        }
        return;
    }

    // Each pair of placed sensors whose extents overlap, once, the lower index first as in the walk.
    pSensorGroup->layOutSweep();
    mFoundCount = 0;
    const SensorSweepItem* const pSweep = pSensorGroup->mSweep;
    const s32 sweepCount = pSensorGroup->mSweepCount;
    for (s32 p = 0; p < sweepCount; p++) {
        for (s32 q = p + 1; q < sweepCount && pSweep[q].mLow <= pSweep[p].mHigh; q++) {
            const s32 a = pSweep[p].mIndex;
            const s32 b = pSweep[q].mIndex;
            testPair(pActive, a < b ? a : b, pActive, a < b ? b : a);
        }
    }

    // The unplaced with every other sensor; two unplaced ones once, from the lower.
    for (s32 u = 0; u < pSensorGroup->mUnplacedCount; u++) {
        const s32 a = pSensorGroup->mUnplaced[u];
        for (s32 k = 0; k < count; k++) {
            if (k == a || (k < a && !::isPlaced(pActive[k]))) {
                continue;
            }
            testPair(pActive, a < k ? a : k, pActive, a < k ? k : a);
        }
    }
    addFoundPairs(pActive, pActive);
#else
    const s32 count = pSensorGroup->mSensorCount;
    for (s32 i = 0; i < count; i++) {
        HitSensor* const pSensor1 = pSensorGroup->mSensors[i];
        if (!::isSensorActive(pSensor1)) {
            continue;
        }

        for (s32 j = i; j < count; j++) {
            HitSensor* const pSensor2 = pSensorGroup->mSensors[j];
            if (::isSensorActive(pSensor2)) {
                checkAttack(pSensor1, pSensor2);
            }
        }
    }
#endif
}

void SensorHitChecker::checkAttack(HitSensor* pSensor1, HitSensor* pSensor2) const {
    ::checkAttackEntries(::entryOf(pSensor1), ::entryOf(pSensor2));
}

namespace MR {
    void initHitSensorGroup(HitSensor* pSensor) {
        MR::getSceneObj< SensorHitChecker >(SceneObj_SensorHitChecker)->initGroup(pSensor);
    }
};  // namespace MR

// The sizes are first capacities; the groups grow.
SensorHitChecker::SensorHitChecker(const char* pName)
    : NameObj(pName), mPlayerGroup(new SensorGroup(0x10)), mRideGroup(new SensorGroup(0x80)), mEyeGroup(new SensorGroup(0x200)),
      mSimpleGroup(new SensorGroup(0x800)), mMapObjGroup(new SensorGroup(0x400)), mCharacterGroup(new SensorGroup(0x400)),
      mFound(), mFoundCount(0) {
    lumacore::setClass(this, lumacore::kClassId< SensorHitChecker >);
}
