#include "Game/MapObj/FlameGun.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 77.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 77.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 77.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f1, "initSensor.f1", 77.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f2, "initSensor.f2", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRotate_f1, "exeRotate.f1", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHitSensor_f1, "updateHitSensor.f1", 90.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHitSensor_f2, "updateHitSensor.f2", 700.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHitSensor_f3, "updateHitSensor.f3", 90.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHitSensor_f4, "updateHitSensor.f4", 610.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHitSensor_f5, "updateHitSensor.f5", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHitSensor_f6, "updateHitSensor.f6", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHitSensor_f7, "updateHitSensor.f7", 55.0f);

namespace NrvFlameGun {
    NEW_NERVE(HostTypeNrvSwitchWait, FlameGun, SwitchWait);
    NEW_NERVE(HostTypeNrvWait, FlameGun, Wait);
    NEW_NERVE(HostTypeNrvPreRadiate, FlameGun, PreRadiate);
    NEW_NERVE(HostTypeNrvRadiate, FlameGun, Radiate);
    NEW_NERVE(HostTypeNrvRotate, FlameGun, Rotate);
    NEW_NERVE(HostTypeNrvRadiateOnly, FlameGun, RadiateOnly);
};  // namespace NrvFlameGun

FlameGun::FlameGun(const char* pName) : LiveActor(pName) {
    mState = -1;
}

void FlameGun::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::setGroupClipping(this, rIter, 32);
    s32 arg = 0;
    MR::getPlacementArg0NoInit(rIter, &arg);
    mState = arg;

    if (MR::useStageSwitchReadA(this, rIter)) {
        initNerve(GET_NERVE(FlameGun, HostTypeNrvSwitchWait));
    } else {
        switch (mState) {
        case 0:
            initNerve(GET_NERVE(FlameGun, HostTypeNrvWait));
            break;
        case 1:
            initNerve(GET_NERVE(FlameGun, HostTypeNrvRotate));
            break;
        case 2:
            initNerve(GET_NERVE(FlameGun, HostTypeNrvRadiateOnly));
            break;
        default:
            // A mode past the three (a mod's placement) waits like mode 0: the actor needs a nerve to run.
            initNerve(GET_NERVE(FlameGun, HostTypeNrvWait));
            break;
        }
    }

    initModelManagerWithAnm("FlameGun", nullptr, false);
    MR::connectToSceneEnemy(this);
    initSound(4, false);
    initBinder(cNum_init_f1 * mScale.y, cNum_init_f2 * mScale.y, 0);
    initEffectKeeper(3, nullptr);
    initSensor();
    MR::initShadowVolumeSphere(this, cNum_init_f3);
    appear();
    MR::invalidateHitSensor(this, "attack");
}

void FlameGun::initSensor() {
    f32 scale = mScale.y;
    initHitSensor(2);
    Vec3f offs;
    offs.x = 0.0f;
    offs.y = cNum_initSensor_f1 * scale;
    offs.z = 0.0f;
    MR::addHitSensorEnemy(this, "body", 32, offs.y, offs);
    MR::addHitSensorCallbackEnemy(this, "attack", 16, cNum_initSensor_f2 * scale);
}

void FlameGun::initAfterPlacement() {
    MR::offCalcShadow(this, nullptr);
}

void FlameGun::endClipped() {
    LiveActor::endClipped();

    if (!isNerve(GET_NERVE(FlameGun, HostTypeNrvRadiate)) && !isNerve(GET_NERVE(FlameGun, HostTypeNrvRotate))) {
        isNerve(GET_NERVE(FlameGun, HostTypeNrvRadiateOnly));
    }
}

void FlameGun::exeSwitchWait() {
    if (MR::isFirstStep(this)) {
        MR::forceDeleteEffectAll(this);
    }

    if (MR::isValidSwitchA(this) && MR::isOnSwitchA(this)) {
        switch (mState) {
        case 0:
            setNerve(GET_NERVE(FlameGun, HostTypeNrvPreRadiate));
            break;
        case 1:
            setNerve(GET_NERVE(FlameGun, HostTypeNrvRotate));
            break;
        case 2:
            setNerve(GET_NERVE(FlameGun, HostTypeNrvRadiateOnly));
            break;
        }
    }
}

void FlameGun::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
    }

    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(2))) {
        setNerve(GET_NERVE(FlameGun, HostTypeNrvPreRadiate));
    }
}

void FlameGun::exePreRadiate() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "ShootingBefore");
        MR::startBrk(this, "ShootingBefore");
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(FlameGun, HostTypeNrvRadiate));
        MR::setBrkFrameAndStop(this, 0.0f);
    }
}

void FlameGun::exeRadiate() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Shooting");
        MR::validateHitSensor(this, "attack");
    }

    MR::startLevelSound(this, "SE_EM_LV_FLAMEGUN_FIRE");

    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(2))) {
        setNerve(GET_NERVE(FlameGun, HostTypeNrvWait));
        MR::forceDeleteEffectAll(this);
        MR::invalidateHitSensor(this, "attack");
    }
}

void FlameGun::exeRotate() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Shooting");
        MR::validateHitSensor(this, "attack");
    }

    mRotation.y += cNum_exeRotate_f1;
    MR::startLevelSound(this, "SE_EM_LV_FLAMEGUN_FIRE");

    if (!MR::isValidHitSensor(this, "attack")) {
        MR::validateHitSensor(this, "attack");
    }
}

void FlameGun::exeRadiateOnly() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Shooting");
        MR::validateHitSensor(this, "attack");
    }

    MR::startLevelSound(this, "SE_EM_LV_FLAMEGUN_FIRE");
}

void FlameGun::updateHitSensor(HitSensor* pSensor) {
    f32 scale = mScale.y;
    Vec3f playerOffset(*MR::getPlayerCenterPos());
    playerOffset -= mPosition;
    Pos3f baseMtx(getBaseMtx());
    Vec3f side, up, front;
    baseMtx.getXDir(side);
    baseMtx.getYDir(up);
    baseMtx.getZDir(front);
    Vec3f direction(front);
    MR::normalizeOrZero(&direction);

    f32 distance = direction.dot(playerOffset);
    pSensor->mPosition.set(mPosition);
    distance = MR::clamp(distance, scale * (cNum_updateHitSensor_f1 * scale), scale * (cNum_updateHitSensor_f2 * scale));
    f32 rate = (distance - cNum_updateHitSensor_f3 * scale) / (cNum_updateHitSensor_f4 * scale);
    f32 radius = (1.0f - rate) * (cNum_updateHitSensor_f5 * scale) + rate * (cNum_updateHitSensor_f6 * scale);

    pSensor->mPosition.add(direction * distance);
    pSensor->mPosition.add(side * 0.0f);
    pSensor->mPosition.add(up * cNum_updateHitSensor_f7);
    pSensor->mPosition.add(front * 0.0f);

    pSensor->mRadius = radius;
}

void FlameGun::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::sendMsgEnemyAttackFire(pReceiver, pSender)) {
        MR::sendMsgPush(pReceiver, pSender);
    }
}
