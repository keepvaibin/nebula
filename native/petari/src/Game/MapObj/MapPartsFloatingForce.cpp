#include "Game/MapObj/MapPartsFloatingForce.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/FloaterFunction.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/MapPartsUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/SpringValue.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_initForSpringMotion_f1, "initForSpringMotion.f1", 0.001f);
NEBULA_TUNABLE_NAMED(f32, cNum_initForSpringMotion_f2, "initForSpringMotion.f2", 0.001f);
NEBULA_TUNABLE_NAMED(f32, cNum_initForSpringMotion_f3, "initForSpringMotion.f3", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateVelocity_f1, "updateVelocity.f1", 0.05f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateVelocity_f2, "updateVelocity.f2", 0.0001f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateVelocity_f3, "updateVelocity.f3", 0.05f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateVelocity_f4, "updateVelocity.f4", 0.0001f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateVelocity_f5, "updateVelocity.f5", 0.98f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateVelocity_f6, "updateVelocity.f6", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateVelocity_f7, "updateVelocity.f7", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateVelocitySpring_f1, "updateVelocitySpring.f1", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeMove_f1, "exeMove.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeMove_f2, "exeMove.f2", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeMoveReturn_f1, "exeMoveReturn.f1", 0.1f);

namespace NrvMapPartsFloatingForce {
    NEW_NERVE(HostTypeWait, MapPartsFloatingForce, Wait);
    NEW_NERVE(HostTypeMove, MapPartsFloatingForce, Move);
    NEW_NERVE(HostTypeMoveSpring, MapPartsFloatingForce, MoveSpring);
    NEW_NERVE(HostTypeMoveReturn, MapPartsFloatingForce, MoveReturn);
};  // namespace NrvMapPartsFloatingForce

SpringStep::SpringStep(f32 cond, f32 speed, f32 angle) : mSpringValue() {
    mSpeed = speed;
    mAngle = angle;
    mSpringValue = new SpringValue(cond, 0.0f, speed, angle, 0.0f);
}

void SpringStep::addSpringVelocity(f32 vel) {
    mSpringValue->mVelocity += vel;
}

void SpringStep::setSpringBaseValue(f32 val) {
    mSpringValue->setParam(static_cast< f64 >(val), 0.0f, mSpeed, mAngle, 0.0f);
}

MapPartsFloatingForce::MapPartsFloatingForce(LiveActor* pActor)
    : MapPartsFunction(pActor, "浮力"), mSpringStep(), mObjectName(), mMaxDepth(), mRotateAngle(), mRotateSpeed(), mRotateAccelType(), mBasePos(gZeroVec),
      mUpDirection(0.0f, 1.0f, 0.0f), mSinkDepth(), mSinkSpeed(), mVelocity(0.0f, 0.0f, 0.0f), mRideSinkSpeed() {
    lumacore::setClass(this, lumacore::kClassId< MapPartsFloatingForce >);
}

void MapPartsFloatingForce::init(const TableIter& rIter) {
    initNerve(GET_NERVE(MapPartsFloatingForce, HostTypeWait));
    MR::getMapPartsArgRotateAccelType(&mRotateAccelType, rIter);

    if (mRotateAccelType == 1) {
        initForSpringMotion(rIter);
    } else {
        initForNormalMotion(rIter);
    }

    MR::useStageSwitchReadA(mHost, rIter);
}

void MapPartsFloatingForce::setObjectName(const char* pName) {
    mObjectName = pName;
}

void MapPartsFloatingForce::initForNormalMotion(const TableIter& rIter) {
    s32 conditionType = 0;
    MR::getMapPartsArgMoveConditionType(&conditionType, rIter);
    mMaxDepth = conditionType;
    MR::getMapPartsArgRotateSpeed(&mRotateSpeed, rIter);
    MR::getMapPartsArgRotateAngle(&mRotateAngle, rIter);
}

void MapPartsFloatingForce::initForSpringMotion(const TableIter& rIter) {
    s32 conditionType = 0;
    MR::getMapPartsArgMoveConditionType(&conditionType, rIter);
    mMaxDepth = conditionType;
    f32 rotateSpeed = 0.0f;
    MR::getMapPartsArgRotateSpeed(&rotateSpeed, rIter);
    rotateSpeed *= cNum_initForSpringMotion_f1;
    f32 rotateAngle = 0.0f;
    MR::getMapPartsArgRotateAngle(&rotateAngle, rIter);
    rotateAngle *= cNum_initForSpringMotion_f2;
    s32 rotateAxis = 0;
    MR::getMapPartsArgRotateAxis(&rotateAxis, rIter);
    mRideSinkSpeed = cNum_initForSpringMotion_f3 * rotateAxis;
    mSpringStep = new SpringStep(-conditionType, rotateSpeed, rotateAngle);
}

void MapPartsFloatingForce::start() {
    mBasePos.set(mHost->mPosition);
    Pos3f baseMtx;
    baseMtx.set(mHost->getBaseMtx());
    baseMtx.getYDir(mUpDirection);
    MR::normalize(&mUpDirection);
    if (mRotateAccelType != 1) {
        setNerve(GET_NERVE(MapPartsFloatingForce, HostTypeMove));
    }
}

void MapPartsFloatingForce::updateVelocity() {
    if (MR::isOnPlayer(MR::getBodySensor(mHost))) {
        if (mMaxDepth <= mSinkDepth) {
            mSinkSpeed = MR::converge< f32 >(mSinkSpeed, 0.0f, cNum_updateVelocity_f1);
        } else {
            mSinkSpeed = mSinkSpeed + (cNum_updateVelocity_f2 * mRotateSpeed);
        }
    } else {
        if (mSinkDepth <= 0.0f) {
            mSinkSpeed = MR::converge< f32 >(mSinkSpeed, 0.0f, cNum_updateVelocity_f3);
        } else {
            mSinkSpeed = mSinkSpeed - (cNum_updateVelocity_f4 * mRotateAngle);
        }
    }

    mSinkSpeed *= cNum_updateVelocity_f5;
    mSinkSpeed = MR::clamp(mSinkSpeed, -cNum_updateVelocity_f6, cNum_updateVelocity_f7);
}

void MapPartsFloatingForce::updateVelocitySpring() {
    f32 velocity = mSpringStep->mSpringValue->mVelocity;
    bool isSwitchOn = false;
    if (MR::isValidSwitchA(mHost) && MR::isOnSwitchA(mHost)) {
        isSwitchOn = true;
    }

    if (isSwitchOn) {
        velocity *= cNum_updateVelocitySpring_f1;
    }

    mVelocity.set(mUpDirection * velocity);
    if (MR::isNearZero(mVelocity)) {
        mVelocity.zero();
    }

    mSpringStep->mSpringValue->update();
}

bool MapPartsFloatingForce::tryOn() {
    bool isSwitchOn = false;

    if (MR::isValidSwitchA(mHost) && MR::isOnSwitchA(mHost)) {
        isSwitchOn = true;
    }

    if (isSwitchOn) {
        return false;
    }

    if (MR::isOnPlayer(MR::getBodySensor(mHost))) {
        const char* pGroundSound = FloaterFunction::getSeGroundOn(mObjectName);
        if (pGroundSound != nullptr) {
            MR::startSound(mHost, pGroundSound);
        }

        setNerve(GET_NERVE(MapPartsFloatingForce, HostTypeMoveSpring));
        return true;
    }

    return false;
}

bool MapPartsFloatingForce::tryReturn() {
    bool isSwitchOn = false;

    if (MR::isValidSwitchA(mHost) && MR::isOnSwitchA(mHost)) {
        isSwitchOn = true;
    }

    if (!isSwitchOn) {
        if (MR::isOnPlayer(MR::getBodySensor(mHost))) {
            return false;
        }
    }

    setNerve(GET_NERVE(MapPartsFloatingForce, HostTypeMoveReturn));
    return true;
}

void MapPartsFloatingForce::exeWait() {
    if (isFirstStep()) {
        mSinkDepth = 0.0f;
    }

    if (mRotateAccelType == 1) {
        tryOn();
    }
}

void MapPartsFloatingForce::exeMove() {
    updateVelocity();
    mVelocity.set(-mUpDirection * mSinkSpeed);
    mSinkDepth += mSinkSpeed;
    mSinkDepth = MR::clamp(mSinkDepth, 0.0f, mMaxDepth);
    Vec3f* const pPosition = &mHost->mPosition;
    pPosition->set(mBasePos - mUpDirection * mSinkDepth);
    if (mObjectName != nullptr) {
        if (mSinkSpeed > cNum_exeMove_f1) {
            const char* pSound = FloaterFunction::getSeMoveDown(mObjectName);
            if (pSound != nullptr) {
                MR::startLevelSound(mHost, pSound);
            }
        } else if (mSinkSpeed < -cNum_exeMove_f2) {
            const char* pSound = FloaterFunction::getSeMoveUp(mObjectName);
            if (pSound != nullptr) {
                MR::startLevelSound(mHost, pSound);
            }
        }
    }
}

void MapPartsFloatingForce::exeMoveSpring() {
    if (isFirstStep()) {
        mSpringStep->setSpringBaseValue(-mMaxDepth);
        mSpringStep->addSpringVelocity(-mRideSinkSpeed);
    }

    updateVelocitySpring();
    mHost->mVelocity.set(mVelocity);
    tryReturn();
}

void MapPartsFloatingForce::exeMoveReturn() {
    if (isFirstStep()) {
        mSpringStep->setSpringBaseValue((mHost->mPosition - mBasePos).length());
    }

    updateVelocitySpring();
    mHost->mVelocity.set(mVelocity);
    if (mObjectName != nullptr && cNum_exeMoveReturn_f1 != mSinkSpeed) {
        const char* pSound = FloaterFunction::getSeMoveUp(mObjectName);
        if (pSound != nullptr) {
            MR::startLevelSound(mHost, pSound);
        }
    }

    if (MR::isNearZero(mBasePos - mHost->mPosition) && MR::isNearZero(mVelocity)) {
        mHost->mPosition.set(mBasePos);
        setNerve(GET_NERVE(MapPartsFloatingForce, HostTypeWait));
    } else {
        tryOn();
    }
}
