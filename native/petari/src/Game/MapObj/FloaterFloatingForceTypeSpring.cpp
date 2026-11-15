#include "Game/MapObj/FloaterFloatingForceTypeSpring.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/FloaterFunction.hpp"
#include "Game/Util.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 sSpeedMin = 0.1f;
};  // namespace

namespace NrvFloaterFloatingForceTypeSpring {
    NEW_NERVE(HostTypeWait, FloaterFloatingForceTypeSpring, Wait);
    NEW_NERVE(HostTypeMove, FloaterFloatingForceTypeSpring, Move);
    NEW_NERVE(HostTypeMoveReturn, FloaterFloatingForceTypeSpring, MoveReturn);
};  // namespace NrvFloaterFloatingForceTypeSpring

FloaterSpringForce::FloaterSpringForce(f32 baseValue, f32 spring, f32 damp) : mSpringValue() {
    mBaseValue = baseValue;
    mSpring = spring;
    mDamp = damp;
    mSpringValue = new SpringValue(baseValue, 0.0f, spring, damp, 0.0f);
}

void FloaterSpringForce::addSpringVelocity(f32 vel) {
    mSpringValue->mVelocity += vel;
}

void FloaterSpringForce::setSpringBaseValue(f32 baseValue) {
    mBaseValue = baseValue;

    mSpringValue->setParam(mBaseValue, 0.0f, mSpring, mDamp, 0.0f);
}

FloaterFloatingForceTypeSpring::FloaterFloatingForceTypeSpring(LiveActor* pHost, const char* pName)
    : FloaterFloatingForce(pHost, pName), mSpringForce(), mVelocity(gZeroVec), mRideSinkSpeed() {
    lumacore::setClass(this, lumacore::kClassId< FloaterFloatingForceTypeSpring >);
}

void FloaterFloatingForceTypeSpring::init(const TableIter& rIter) {
    FloaterFloatingForce::init(rIter);

    // The rotate arguments carry the spring constants and the ride kick.
    f32 spring = 0.0f;
    MR::getMapPartsArgRotateSpeed(&spring, rIter);
    spring *= 0.001f;

    f32 damp = 0.0f;
    MR::getMapPartsArgRotateAngle(&damp, rIter);
    damp *= 0.001f;

    s32 rideSinkSpeed = 0;
    MR::getMapPartsArgRotateAxis(&rideSinkSpeed, rIter);
    mRideSinkSpeed = rideSinkSpeed * 0.1f;

    mSpringForce = new FloaterSpringForce(-mMaxSinkDepth, spring, damp);

    initNerve(GET_NERVE(FloaterFloatingForceTypeSpring, HostTypeWait));
    MR::useStageSwitchReadA(mHost, rIter);
}

void FloaterFloatingForceTypeSpring::start() {
    FloaterFloatingForce::start();
    setNerve(GET_NERVE(FloaterFloatingForceTypeSpring, HostTypeWait));
}

void FloaterFloatingForceTypeSpring::updateVelocity() {
    f32 sinkSpeed = mSpringForce->mSpringValue->mVelocity;
    bool isValidAndOnSwitchA = MR::isValidSwitchA(mHost) && MR::isOnSwitchA(mHost);

    if (isValidAndOnSwitchA) {
        sinkSpeed *= 3.0f;
    }

    mVelocity.set(mUp * sinkSpeed);

    if (MR::isNearZero(sinkSpeed)) {
        mVelocity.zero();
    }

    mSpringForce->mSpringValue->update();
}

void FloaterFloatingForceTypeSpring::updateHostVelocity(Vec3f* pDst) const {
    pDst->set(getCurrentVelocity());
}

void FloaterFloatingForceTypeSpring::soundMoveSE() {
    if (mObjName == nullptr) {
        return;
    }

    if (getCurrentSinkSpeed() > ::sSpeedMin) {
        const char* seMoveDown = FloaterFunction::getSeMoveDown(mObjName);

        if (seMoveDown != nullptr) {
            MR::startLevelSound(mHost, seMoveDown);
        }
    } else if (getCurrentSinkSpeed() < -::sSpeedMin) {
        const char* seMoveUp = FloaterFunction::getSeMoveUp(mObjName);

        if (seMoveUp != nullptr) {
            MR::startLevelSound(mHost, seMoveUp);
        }
    }
}

f32 FloaterFloatingForceTypeSpring::getCurrentSinkSpeed() const {
    return mSpringForce->mSpringValue->mVelocity;
}

bool FloaterFloatingForceTypeSpring::tryOn() {
    bool isValidAndOnSwitchA = MR::isValidSwitchA(mHost) && MR::isOnSwitchA(mHost);

    if (isValidAndOnSwitchA) {
        return false;
    }

    if (!MR::isOnPlayer(mHost)) {
        return false;
    }

    setNerve(GET_NERVE(FloaterFloatingForceTypeSpring, HostTypeMove));

    return true;
}

bool FloaterFloatingForceTypeSpring::tryReturn() {
    bool isValidAndOnSwitchA = MR::isValidSwitchA(mHost) && MR::isOnSwitchA(mHost);

    if (!isValidAndOnSwitchA && MR::isOnPlayer(mHost)) {
        return false;
    }

    setNerve(GET_NERVE(FloaterFloatingForceTypeSpring, HostTypeMoveReturn));

    return true;
}

void FloaterFloatingForceTypeSpring::exeWait() {
    tryOn();
}

void FloaterFloatingForceTypeSpring::exeMove() {
    if (isFirstStep()) {
        mSpringForce->setSpringBaseValue(-mMaxSinkDepth);
        mSpringForce->addSpringVelocity(-mRideSinkSpeed);
    }

    updateVelocity();
    soundMoveSE();
    tryReturn();
}

void FloaterFloatingForceTypeSpring::exeMoveReturn() {
    if (isFirstStep()) {
        mSpringForce->setSpringBaseValue((mHost->mPosition - mBasePos).length());
    }

    updateVelocity();

    if (mObjName != nullptr && getCurrentSinkSpeed() != ::sSpeedMin) {
        const char* seMoveUp = FloaterFunction::getSeMoveUp(mObjName);

        if (seMoveUp != nullptr) {
            MR::startLevelSound(mHost, seMoveUp);
        }
    }

    if (MR::isNearZero(mBasePos - mHost->mPosition)) {
        if (MR::isNearZero(getCurrentSinkSpeed(), 0.1f)) {
            mHost->mPosition.set(mBasePos);
            setNerve(GET_NERVE(FloaterFloatingForceTypeSpring, HostTypeWait));
            return;
        }
    }

    tryOn();
}
