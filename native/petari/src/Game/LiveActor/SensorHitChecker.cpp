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
};  // namespace

SensorGroup::SensorGroup(int maxSensors) : mMaxSensors(maxSensors), mSensorCount(0), mSensors(new HitSensor*[maxSensors]()) {
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

void SensorHitChecker::doObjColGroup(SensorGroup* pGroup1, SensorGroup* pGroup2) const {
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
}

// Pairs each sensor with itself too; checkAttack skips sensors of one host.
void SensorHitChecker::doObjColInSameGroup(SensorGroup* pSensorGroup) const {
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
