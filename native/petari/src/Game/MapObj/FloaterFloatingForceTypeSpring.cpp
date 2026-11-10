#include "Game/MapObj/FloaterFloatingForceTypeSpring.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/FloaterFunction.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 0.001f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 0.001f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateVelocity_f1, "updateVelocity.f1", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeMoveReturn_f1, "exeMoveReturn.f1", 0.1f);

namespace {
    NEBULA_TUNABLE(f32, sSpeedMin, 0.1f);
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
    spring *= cNum_init_f1;

    f32 damp = 0.0f;
    MR::getMapPartsArgRotateAngle(&damp, rIter);
    damp *= cNum_init_f2;

    s32 rideSinkSpeed = 0;
    MR::getMapPartsArgRotateAxis(&rideSinkSpeed, rIter);
    mRideSinkSpeed = rideSinkSpeed * cNum_init_f3;

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
        sinkSpeed *= cNum_updateVelocity_f1;
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
        if (MR::isNearZero(getCurrentSinkSpeed(), cNum_exeMoveReturn_f1)) {
            mHost->mPosition.set(mBasePos);
            setNerve(GET_NERVE(FloaterFloatingForceTypeSpring, HostTypeWait));
            return;
        }
    }

    tryOn();
}
