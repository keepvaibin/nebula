#include "Game/MapObj/HitWallTimerSwitch.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/CollisionParts.hpp"
#include "Game/MapObj/MapObjConnector.hpp"
#include "Game/Util.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 50.f);

namespace NrvHitWallTimerSwitch {
    NEW_NERVE(HitWallTimerSwitchNrvOff, HitWallTimerSwitch, Off);
    NEW_NERVE(HitWallTimerSwitchNrvSwitchDown, HitWallTimerSwitch, SwitchDown);
    NEW_NERVE(HitWallTimerSwitchNrvOn, HitWallTimerSwitch, On);
    NEW_NERVE(HitWallTimerSwitchNrvSwitchUp, HitWallTimerSwitch, SwitchUp);
};  // namespace NrvHitWallTimerSwitch

HitWallTimerSwitch::HitWallTimerSwitch(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< HitWallTimerSwitch >);
    mMoveCollision = nullptr;
    mConnector = nullptr;
    mTimer = NebulaFrameClock::stepsOfSeconds(5);
    mIsHit = 0;
}

void HitWallTimerSwitch::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("HitWallTimerSwitch", nullptr, false);
    MR::connectToSceneMapObjDecoration(this);
    initHitSensor(2);
    MR::addHitSensorMapObj(this, "body", 0x10, 0.0f, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensor(this, "hit", ATYPE_SWITCH_BIND, 0x10, cNum_init_f1, Vec3f(0.0f, 0.0f, 0.0f));
    MR::initCollisionParts(this, "HitWallTimerSwitch", getSensor("body"), nullptr);
    mMoveCollision = MR::createCollisionPartsFromLiveActor(this, "Move", getSensor("hit"), MR::CollisionScaleType_Default);
    mConnector = new MapObjConnector(this);
    initSound(4, false);
    MR::needStageSwitchWriteA(this, rIter);
    MR::getPlacementArg0NoInit(rIter, &mTimer);
    initNerve(GET_NERVE(HitWallTimerSwitch, HitWallTimerSwitchNrvOff));
    MR::startBck(this, "Wait");
    appear();
}

void HitWallTimerSwitch::initAfterPlacement() {
    mConnector->attachToBack();
}

void HitWallTimerSwitch::calcAnim() {
    LiveActor::calcAnim();
    Pos3f mtx;
    mtx.setInline(MR::getJointMtx(this, "Move"));
    mMoveCollision->setMtx(mtx);
}

void HitWallTimerSwitch::calcAndSetBaseMtx() {
    LiveActor::calcAndSetBaseMtx();
    mConnector->connect();
}

void HitWallTimerSwitch::control() {
    mIsHit = 0;
}

bool HitWallTimerSwitch::receiveMsgPlayerAttack(u32 /*msg*/, HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(HitWallTimerSwitch, HitWallTimerSwitchNrvOff)) && pReceiver->isType(ATYPE_SWITCH_BIND)) {
        mIsHit = 1;
        return true;
    }

    return false;
}

bool HitWallTimerSwitch::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (!isNerve(GET_NERVE(HitWallTimerSwitch, HitWallTimerSwitchNrvOff))) {
        return false;
    }

    if (msg == ACTMES_IS_RUSH_ENABLE) {
        if (pReceiver->isType(ATYPE_SWITCH_BIND)) {
            return true;
        }
    }

    return false;
}

bool HitWallTimerSwitch::trySwitchDown() {
    if (mIsHit) {
        setNerve(GET_NERVE(HitWallTimerSwitch, HitWallTimerSwitchNrvSwitchDown));
        return true;
    }

    return false;
}

bool HitWallTimerSwitch::tryOn() {
    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(HitWallTimerSwitch, HitWallTimerSwitchNrvOn));
        return true;
    }

    return false;
}

bool HitWallTimerSwitch::trySwitchUp() {
    if (getNerveStep() > mTimer) {
        setNerve(GET_NERVE(HitWallTimerSwitch, HitWallTimerSwitchNrvSwitchUp));
        return true;
    } else {
        if (!MR::isPlayerDead() && !MR::isPowerStarGetDemoActive()) {
            if (mTimer == getNerveStep()) {
                MR::startSystemSE("SE_SY_TIMER_A_0");
            } else if (!(getNerveStep() % NebulaFrameClock::stepsOfSeconds(1))) {
                if (getNerveStep() < mTimer - NebulaFrameClock::stepsOfSeconds(2)) {
                    MR::startSystemSE("SE_SY_TIMER_A_2");
                } else {
                    MR::startSystemSE("SE_SY_TIMER_A_1");
                }
            }
        }

        return false;
    }
}

bool HitWallTimerSwitch::tryOff() {
    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(HitWallTimerSwitch, HitWallTimerSwitchNrvOff));
        return true;
    }

    return false;
}

void HitWallTimerSwitch::exeOff() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
        MR::validateClipping(this);
        MR::offSwitchA(this);
        MR::validateCollisionParts(mMoveCollision);
    }

    if (trySwitchDown()) {
        return;
    }
}

void HitWallTimerSwitch::exeSwitchDown() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "On");
        MR::startBtp(this, "On");
        MR::invalidateClipping(this);
    }

    if (tryOn()) {
        return;
    }
}

void HitWallTimerSwitch::exeOn() {
    if (MR::isFirstStep(this)) {
        MR::shakeCameraNormal();
        MR::onSwitchA(this);
        MR::startSound(this, "SE_OJ_SWITCH_1");
        MR::invalidateCollisionParts(mMoveCollision);
    }

    if (trySwitchUp()) {
        return;
    }
}

void HitWallTimerSwitch::exeSwitchUp() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
        MR::startBtp(this, "Off");
    }

    if (tryOff()) {
        return;
    }
}
