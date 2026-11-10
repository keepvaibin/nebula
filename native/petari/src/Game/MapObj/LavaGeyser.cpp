#include "Game/MapObj/LavaGeyser.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHitSensor_f1, "updateHitSensor.f1", 100.f);

namespace NrvLavaGeyser {
    NEW_NERVE(HostTypeNrvWaitSwitch, LavaGeyser, WaitSwitch);
    NEW_NERVE(HostTypeNrvWait, LavaGeyser, Wait);
    NEW_NERVE(HostTypeNrvSign, LavaGeyser, Sign);
    NEW_NERVE(HostTypeNrvShootUp, LavaGeyser, ShootUp);
    NEW_NERVE(HostTypeNrvShootKeep, LavaGeyser, ShootKeep);
    NEW_NERVE(HostTypeNrvShootDown, LavaGeyser, ShootDown);
};  // namespace NrvLavaGeyser

LavaGeyser::LavaGeyser(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< LavaGeyser >);
    mArg0 = 180;
    mArg1 = 180;
    mTopPos.set(0.0f, 0.0f, 0.0f);
    mUp.set(0.0f, 1.0f, 0.0f);
}

void LavaGeyser::init(const TableIter& iter) {
    MR::initDefaultPos(this, iter);
    Quat4f quat1;
    quat1.setEulerDegree(mRotation.x, mRotation.y, mRotation.z);
    quat1.getYDir(mUp);
    MR::getPlacementArg0NoInit(iter, &mArg0);
    MR::getPlacementArg1NoInit(iter, &mArg1);
    MR::useStageSwitchReadA(this, iter);
    initModelManagerWithAnm("LavaGeyser", nullptr, false);
    MR::startBtk(this, "LavaGeyser");
    MR::setBtkFrameAtRandom(this);
    MR::hideModel(this);
    MR::connectToSceneMapObj(this);
    initHitSensor(3);
    MR::addHitSensorCallbackMapObj(this, "attack", 1, mScale.y * cNum_init_f1);
    initEffectKeeper(0, nullptr);
    initSound(4, false);
    MR::setClippingTypeSphereContainsModelBoundingBox(this, cNum_init_f2);
    MR::setGroupClipping(this, iter, 16);
    if (MR::isValidSwitchA(this)) {
        initNerve(GET_NERVE(LavaGeyser, HostTypeNrvWaitSwitch));
    } else {
        initNerve(GET_NERVE(LavaGeyser, HostTypeNrvWait));
    }
    makeActorAppeared();
}

void LavaGeyser::startClipped() {
    LiveActor::startClipped();
    MR::forceDeleteEffectAll(this);
    if (!isNerve(GET_NERVE(LavaGeyser, HostTypeNrvWaitSwitch))) {
        setNerve(GET_NERVE(LavaGeyser, HostTypeNrvWait));
    }
}

void LavaGeyser::updateHitSensor(HitSensor* pSensor) {
    Vec3f cPos = *MR::getPlayerCenterPos();
    cPos -= mPosition;
    // The sensor follows the player's height up the column, stopping 100 short of the top.
    f32 playerHeight = mUp.dot(cPos);
    Vec3f toTop = mTopPos;
    toTop -= mPosition;
    f32 maxHeight = mUp.dot(toTop) + -cNum_updateHitSensor_f1;
    if (maxHeight < 0.f) {
        pSensor->mPosition.set(mTopPos);
        return;
    }

    f32 sensorHeight = MR::clamp(playerHeight, 0.0f, maxHeight);

    pSensor->mPosition.set(mPosition);
    pSensor->mPosition.add(mUp * sensorHeight);
}

void LavaGeyser::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    MR::sendMsgEnemyAttackFireStrong(pReceiver, pSender);
}

void LavaGeyser::exeWaitSwitch() {
    if (MR::isFirstStep(this)) {
        MR::hideModel(this);
        MR::invalidateHitSensors(this);
    }
    if (MR::isOnSwitchA(this)) {
        setNerve(GET_NERVE(LavaGeyser, HostTypeNrvSign));
    }
}

void LavaGeyser::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::hideModel(this);
        MR::invalidateHitSensors(this);
        mTopPos.set(mPosition);
    }
    if (MR::isValidSwitchA(this) && !MR::isOnSwitchA(this)) {
        setNerve(GET_NERVE(LavaGeyser, HostTypeNrvWaitSwitch));
    } else if (MR::isStep(this, mArg0)) {
        setNerve(GET_NERVE(LavaGeyser, HostTypeNrvSign));
    }
}

void LavaGeyser::exeSign() {
    if (MR::isFirstStep(this)) {
        const Vec3f* pos = &mPosition;
        mTopPos.set(*pos);
        MR::emitEffect(this, "Sign");
    }
    MR::startLevelSound(this, "SE_OJ_LV_LAVA_GEYSER_SIGN");
    if (MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(1500))) {
        MR::deleteEffect(this, "Sign");
        setNerve(GET_NERVE(LavaGeyser, HostTypeNrvShootUp));
    }
}

void LavaGeyser::exeShootUp() {
    if (MR::isFirstStep(this)) {
        MR::showModel(this);
        MR::validateHitSensors(this);
        MR::startSound(this, "SE_OJ_LAVA_GEYSER_SHOOT");
        MR::startBck(this, "LavaGeyserAppear");
    }
    MR::copyJointPos(this, "Top", &mTopPos);
    MR::startLevelSound(this, "SE_OJ_LV_LAVA_GEYSER_SIGN");
    MR::startLevelSound(this, "SE_OJ_LV_LAVA_GEYSER_KEEP");
    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(LavaGeyser, HostTypeNrvShootKeep));
    }
}

void LavaGeyser::exeShootKeep() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "LavaGeyserWait");
    }
    MR::copyJointPos(this, "Top", &mTopPos);
    MR::startLevelSound(this, "SE_OJ_LV_LAVA_GEYSER_SIGN");
    MR::startLevelSound(this, "SE_OJ_LV_LAVA_GEYSER_KEEP");
    if (MR::isStep(this, mArg1)) {
        setNerve(GET_NERVE(LavaGeyser, HostTypeNrvShootDown));
    }
}

void LavaGeyser::exeShootDown() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "LavaGeyserDisappear");
    }
    MR::copyJointPos(this, "Top", &mTopPos);
    MR::startLevelSound(this, "SE_OJ_LV_LAVA_GEYSER_KEEP");
    if (MR::isBckStopped(this)) {
        MR::hideModel(this);
        setNerve(GET_NERVE(LavaGeyser, HostTypeNrvWait));
    }
}
