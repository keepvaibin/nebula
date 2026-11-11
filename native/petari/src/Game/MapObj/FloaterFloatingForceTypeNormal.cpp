#include "Game/MapObj/FloaterFloatingForceTypeNormal.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/FloaterFunction.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_updateVelocity_f1, "updateVelocity.f1", 0.05f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateVelocity_f2, "updateVelocity.f2", 0.0001f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateVelocity_f3, "updateVelocity.f3", 0.05f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateVelocity_f4, "updateVelocity.f4", 0.0001f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateVelocity_f5, "updateVelocity.f5", 0.98f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateVelocity_f6, "updateVelocity.f6", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateVelocity_f7, "updateVelocity.f7", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateVelocity_f8, "updateVelocity.f8", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateVelocity_f9, "updateVelocity.f9", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_soundMoveSE_f1, "soundMoveSE.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_soundMoveSE_f2, "soundMoveSE.f2", 0.1f);

namespace NrvFloaterFloatingForceTypeNormal {
    NEW_NERVE(HostTypeWait, FloaterFloatingForceTypeNormal, Wait);
    NEW_NERVE(HostTypeMoveFirst, FloaterFloatingForceTypeNormal, MoveFirst);
    NEW_NERVE(HostTypeMoveOffPlayer, FloaterFloatingForceTypeNormal, Move);
    NEW_NERVE(HostTypeMoveOnPlayer, FloaterFloatingForceTypeNormal, Move);
};  // namespace NrvFloaterFloatingForceTypeNormal

FloaterFloatingForceTypeNormal::FloaterFloatingForceTypeNormal(LiveActor* pActor, const char* pName)
    : FloaterFloatingForce(pActor, pName), mRiseAccel(), mSinkAccel(), mSinkDepth(), mSinkSpeed() {
    lumacore::setClass(this, lumacore::kClassId< FloaterFloatingForceTypeNormal >);
}

void FloaterFloatingForceTypeNormal::init(const TableIter& rIter) {
    FloaterFloatingForce::init(rIter);
    MR::getMapPartsArgRotateSpeed(&mSinkAccel, rIter);
    MR::getMapPartsArgRotateAngle(&mRiseAccel, rIter);
    MR::useStageSwitchReadA(mHost, rIter);
    MapPartsFunction::initNerve(GET_NERVE(FloaterFloatingForceTypeNormal, HostTypeWait));
}

void FloaterFloatingForceTypeNormal::start() {
    FloaterFloatingForce::start();
    setStateMove();
}

void FloaterFloatingForceTypeNormal::updateHostTrans(Vec3f* pDst) const {
    pDst->set(mBasePos - (mUp * mSinkDepth));
}

void FloaterFloatingForceTypeNormal::updateVelocity(bool isOnPlayer) {
    if (isOnPlayer && !(MR::isValidSwitchA(mHost) && MR::isOnSwitchA(mHost))) {
        if (mMaxSinkDepth <= mSinkDepth) {
            mSinkSpeed = MR::converge< f32 >(mSinkSpeed, 0.0f, cNum_updateVelocity_f1);
        } else {
            mSinkSpeed += mSinkAccel * cNum_updateVelocity_f2;
        }
    } else if (mSinkDepth <= 0.0f) {
        mSinkSpeed = MR::converge< f32 >(mSinkSpeed, 0.0f, cNum_updateVelocity_f3);
    } else {
        mSinkSpeed -= mRiseAccel * cNum_updateVelocity_f4;
    }

    mSinkSpeed *= cNum_updateVelocity_f5;
    mSinkSpeed = MR::clamp(mSinkSpeed, -cNum_updateVelocity_f6, cNum_updateVelocity_f7);

    f32 sinkStep = mSinkSpeed;

    // The landing jolt: 6 steps down, then up.
    if (FloaterFunction::isExistImpactOnRide(mObjName)) {
        if (isNerve(GET_NERVE(FloaterFloatingForceTypeNormal, HostTypeMoveFirst))) {
            sinkStep = cNum_updateVelocity_f8;

            if (getStep() > 5) {
                sinkStep *= -1.0f;
            }
        }
    }

    if (FloaterFunction::isExistSpeedUpLanding(mObjName)) {
        bool isValidAndOnSwitchA = MR::isValidSwitchA(mHost) && MR::isOnSwitchA(mHost);

        if (!isValidAndOnSwitchA && isNerve(GET_NERVE(FloaterFloatingForceTypeNormal, HostTypeMoveOnPlayer)) && getStep() < 7) {
            sinkStep += (7 - getStep()) * cNum_updateVelocity_f9;
        }
    }

    mVelocity.set(-mUp * sinkStep);

    mSinkDepth += sinkStep;
    mSinkDepth = MR::clamp(mSinkDepth, 0.0f, mMaxSinkDepth);
}

void FloaterFloatingForceTypeNormal::soundMoveSE() {
    if (mObjName == nullptr) {
        return;
    }

    if (mSinkSpeed > cNum_soundMoveSE_f1) {
        const char* seMoveDown = FloaterFunction::getSeMoveDown(mObjName);

        if (seMoveDown != nullptr) {
            MR::startLevelSound(mHost, seMoveDown);
        }
    } else if (mSinkSpeed < -cNum_soundMoveSE_f2) {
        const char* seMoveUp = FloaterFunction::getSeMoveUp(mObjName);

        if (seMoveUp != nullptr) {
            MR::startLevelSound(mHost, seMoveUp);
        }
    }
}

void FloaterFloatingForceTypeNormal::setStateMove() {
    if (MR::isOnPlayer(MR::getBodySensor(mHost))) {
        MapPartsFunction::setNerve(GET_NERVE(FloaterFloatingForceTypeNormal, HostTypeMoveOnPlayer));
    } else {
        MapPartsFunction::setNerve(GET_NERVE(FloaterFloatingForceTypeNormal, HostTypeMoveOffPlayer));
    }
}

void FloaterFloatingForceTypeNormal::exeWait() {
    if (isFirstStep()) {
        mSinkDepth = 0.0f;
    }
}

void FloaterFloatingForceTypeNormal::exeMoveFirst() {
    if (isFirstStep()) {
        const char* seGroundOn = FloaterFunction::getSeGroundOn(mObjName);

        if (seGroundOn != nullptr) {
            MR::startSound(mHost, seGroundOn);
        }

        if (FloaterFunction::isExistImpactOnRide(mObjName)) {
            MR::shakeCameraWeak();
        }
    }

    updateVelocity(MR::isOnPlayer(MR::getBodySensor(mHost)));
    soundMoveSE();

    if (isStep(8)) {
        setStateMove();
    }
}

void FloaterFloatingForceTypeNormal::exeMove() {
    bool isOnPlayer = MR::isOnPlayer(MR::getBodySensor(mHost));

    if (isOnPlayer && MR::isNearZero(mSinkDepth) && !FloaterFunction::isExistSpeedUpLanding(mObjName)) {
        MapPartsFunction::setNerve(GET_NERVE(FloaterFloatingForceTypeNormal, HostTypeMoveFirst));
    }

    updateVelocity(isOnPlayer);
    soundMoveSE();

    if ((!isOnPlayer && isNerve(GET_NERVE(FloaterFloatingForceTypeNormal, HostTypeMoveOnPlayer)) && MapPartsFunction::getStep() >= 7) ||
        (isOnPlayer && MapPartsFunction::isNerve(GET_NERVE(FloaterFloatingForceTypeNormal, HostTypeMoveOffPlayer)))) {
        setStateMove();
    }
}
