#include "Game/MapObj/FloaterFloatingForceTypeNormal.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/FloaterFunction.hpp"
#include "Game/Util.hpp"
#include "Game/System/LumaCore.hpp"

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
            mSinkSpeed = MR::converge< f32 >(mSinkSpeed, 0.0f, 0.05f);
        } else {
            mSinkSpeed += mSinkAccel * 0.0001f;
        }
    } else if (mSinkDepth <= 0.0f) {
        mSinkSpeed = MR::converge< f32 >(mSinkSpeed, 0.0f, 0.05f);
    } else {
        mSinkSpeed -= mRiseAccel * 0.0001f;
    }

    mSinkSpeed *= 0.98f;
    mSinkSpeed = MR::clamp(mSinkSpeed, -50.0f, 50.0f);

    f32 sinkStep = mSinkSpeed;

    // The landing jolt: 6 steps down, then up.
    if (FloaterFunction::isExistImpactOnRide(mObjName)) {
        if (isNerve(GET_NERVE(FloaterFloatingForceTypeNormal, HostTypeMoveFirst))) {
            sinkStep = 10.0f;

            if (getStep() > 5) {
                sinkStep *= -1.0f;
            }
        }
    }

    if (FloaterFunction::isExistSpeedUpLanding(mObjName)) {
        bool isValidAndOnSwitchA = MR::isValidSwitchA(mHost) && MR::isOnSwitchA(mHost);

        if (!isValidAndOnSwitchA && isNerve(GET_NERVE(FloaterFloatingForceTypeNormal, HostTypeMoveOnPlayer)) && getStep() < 7) {
            sinkStep += (7 - getStep()) * 0.8f;
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

    if (mSinkSpeed > 0.1f) {
        const char* seMoveDown = FloaterFunction::getSeMoveDown(mObjName);

        if (seMoveDown != nullptr) {
            MR::startLevelSound(mHost, seMoveDown);
        }
    } else if (mSinkSpeed < -0.1f) {
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
