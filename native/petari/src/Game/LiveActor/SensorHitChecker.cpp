#include "Game/LiveActor/SensorHitChecker.hpp"
#include "Game/Util/Array.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    bool isSensorActive(const HitSensor* pSensor) {
        return pSensor->isValid() && !MR::isClipped(pSensor->mHost);
    }

    // checkAttack on two gathered sensors: the same test with the same operations, and the same calls on a hit.
    void checkAttackEntries(const SensorHitEntry& rEntry1, const SensorHitEntry& rEntry2) {
        if (rEntry1.mHost == rEntry2.mHost) {
            return;
        }

        f32 xPos = rEntry1.mX - rEntry2.mX;
        f32 yPos = rEntry1.mY - rEntry2.mY;
        f32 zPos = rEntry1.mZ - rEntry2.mZ;
        f32 totalSize = rEntry1.mRadius + rEntry2.mRadius;

        // Written as not-outside so a NaN distance counts as a hit.
        if (!((((xPos * xPos) + (yPos * yPos)) + (zPos * zPos)) >= (totalSize * totalSize))) {
            if (!rEntry2.mSensor->isType(ATYPE_EYE)) {
                rEntry1.mSensor->addHitSensor(rEntry2.mSensor);
            }

            if (!rEntry1.mSensor->isType(ATYPE_EYE)) {
                rEntry2.mSensor->addHitSensor(rEntry1.mSensor);
            }
        }
    }
};  // namespace

SensorGroup::SensorGroup(int maxSensors)
    : mMaxSensors(maxSensors), mSensorCount(0), mSensors(new HitSensor*[maxSensors]()), mActiveCount(0), mActiveMax(0), mActive(nullptr) {
}

// Once a frame, before any pair: nothing in the pass changes a sensor's validity, place or radius or an actor's clipping
// (a hit only adds to the sensors' hit lists), so the sensors active here are the ones the pairs would find active.
void SensorGroup::gatherActive() {
    if (mActiveMax < mSensorCount) {
        delete[] mActive;
        mActiveMax = mMaxSensors > mSensorCount ? mMaxSensors : mSensorCount;
        mActive = new SensorHitEntry[mActiveMax];
    }

    mActiveCount = 0;
    for (s32 i = 0; i < mSensorCount; i++) {
        HitSensor* const pSensor = mSensors[i];
        if (!::isSensorActive(pSensor)) {
            continue;
        }

        SensorHitEntry& rEntry = mActive[mActiveCount++];
        rEntry.mX = pSensor->mPosition.x;
        rEntry.mY = pSensor->mPosition.y;
        rEntry.mZ = pSensor->mPosition.z;
        rEntry.mRadius = pSensor->getRadius();
        rEntry.mHost = pSensor->mHost;
        rEntry.mSensor = pSensor;
    }
}

void SensorGroup::add(HitSensor* pSensor) {
    if (mSensorCount >= mMaxSensors) {
        mSensors = MR::growArray(mSensors, mMaxSensors);
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
    mPlayerGroup->gatherActive();
    mRideGroup->gatherActive();
    mEyeGroup->gatherActive();
    mSimpleGroup->gatherActive();
    mMapObjGroup->gatherActive();
    mCharacterGroup->gatherActive();
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

// Every active pair in the order of the groups' sensors (movement gathered them).
void SensorHitChecker::doObjColGroup(SensorGroup* pGroup1, SensorGroup* pGroup2) const {
    const s32 group1Count = pGroup1->mActiveCount;
    const s32 group2Count = pGroup2->mActiveCount;
    const SensorHitEntry* const pActive1 = pGroup1->mActive;
    const SensorHitEntry* const pActive2 = pGroup2->mActive;
    for (s32 i = 0; i < group1Count; i++) {
        for (s32 j = 0; j < group2Count; j++) {
            ::checkAttackEntries(pActive1[i], pActive2[j]);
        }
    }
}

// Pairs each sensor with itself too; the test skips sensors of one host.
void SensorHitChecker::doObjColInSameGroup(SensorGroup* pSensorGroup) const {
    const s32 count = pSensorGroup->mActiveCount;
    const SensorHitEntry* const pActive = pSensorGroup->mActive;
    for (s32 i = 0; i < count; i++) {
        for (s32 j = i; j < count; j++) {
            ::checkAttackEntries(pActive[i], pActive[j]);
        }
    }
}

void SensorHitChecker::checkAttack(HitSensor* pSensor1, HitSensor* pSensor2) const {
    if (pSensor1->mHost == pSensor2->mHost) {
        return;
    }

    f32 xPos = pSensor1->mPosition.x - pSensor2->mPosition.x;
    f32 yPos = pSensor1->mPosition.y - pSensor2->mPosition.y;
    f32 zPos = pSensor1->mPosition.z - pSensor2->mPosition.z;
    f32 totalSize = pSensor1->getRadius() + pSensor2->getRadius();

    // Written as not-outside so a NaN distance counts as a hit.
    if (!((((xPos * xPos) + (yPos * yPos)) + (zPos * zPos)) >= (totalSize * totalSize))) {
        if (!pSensor2->isType(ATYPE_EYE)) {
            pSensor1->addHitSensor(pSensor2);
        }

        if (!pSensor1->isType(ATYPE_EYE)) {
            pSensor2->addHitSensor(pSensor1);
        }
    }
}

namespace MR {
    void initHitSensorGroup(HitSensor* pSensor) {
        MR::getSceneObj< SensorHitChecker >(SceneObj_SensorHitChecker)->initGroup(pSensor);
    }
};  // namespace MR

// The sizes are first capacities; the groups grow.
SensorHitChecker::SensorHitChecker(const char* pName)
    : NameObj(pName), mPlayerGroup(new SensorGroup(0x10)), mRideGroup(new SensorGroup(0x80)), mEyeGroup(new SensorGroup(0x200)),
      mSimpleGroup(new SensorGroup(0x800)), mMapObjGroup(new SensorGroup(0x400)), mCharacterGroup(new SensorGroup(0x400)) {
    lumacore::setClass(this, lumacore::kClassId< SensorHitChecker >);
}
