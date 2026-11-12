#include "Game/MapObj/ValveSwitch.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapObjConnector.hpp"
#include "Game/Util.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvValveSwitch {
    NEW_NERVE(ValveSwitchNrvWait, ValveSwitch, Wait);
    NEW_NERVE(ValveSwitchNrvAdjust, ValveSwitch, Adjust);
    NEW_NERVE(ValveSwitchNrvValve, ValveSwitch, Valve);
    NEW_NERVE(ValveSwitchNrvEnd, ValveSwitch, End);
};  // namespace NrvValveSwitch

ValveSwitch::ValveSwitch(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< ValveSwitch >);
    mBindActor = nullptr;
    mObjConnector = nullptr;
    mIsReverse = false;

    mObjConnector = new MapObjConnector(this);
    mValveMtx.identity();
}

void ValveSwitch::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::needStageSwitchWriteA(this, rIter);
    initModelManagerWithAnm("ValveSwitch", nullptr, false);
    MR::connectToSceneMapObjDecorationStrongLight(this);
    MR::initLightCtrl(this);
    initHitSensor(2);
    MR::addHitSensorMapObj(this, "body", 8, 100.0f, Vec3f(0.0f, 0.0f, 0.0f));
    Vec3f binder;
    binder.scale(-75.0f, mGravity);
    MR::addHitSensorAtJoint(this, "binder", "Valve", ATYPE_BINDER, 8, 150.0f, binder);
    mValveMtx.set(MR::getJointMtx(this, "Valve"));
    MR::initCollisionParts(this, "ValveCol", getSensor("binder"), mValveMtx.toMtxPtr());
    initEffectKeeper(0, nullptr);
    initSound(4, false);
    MR::getPlacementArg0NoInit(rIter, &mIsReverse);
    initNerve(GET_NERVE(ValveSwitch, ValveSwitchNrvWait));
    makeActorAppeared();
}

void ValveSwitch::initAfterPlacement() {
    mObjConnector->attachToUnder();
}

void ValveSwitch::control() {
    mValveMtx.set(MR::getJointMtx(this, "Valve"));
    MR::makeMtxWithoutScale(&mValveMtx, mValveMtx);
}

void ValveSwitch::calcAndSetBaseMtx() {
    LiveActor::calcAndSetBaseMtx();
    mObjConnector->connect();
}

void ValveSwitch::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBrk(this, "ValveSwitchOn");
        MR::setBrkFrameAndStop(this, 0.0f);

        if (mIsReverse) {
            MR::startBck(this, "ValveSwitchReverseOn");
        } else {
            MR::startBck(this, "ValveSwitchOn");
        }

        MR::setBckFrameAndStop(this, 0.0f);
    }
}

void ValveSwitch::exeAdjust() {
    if (MR::isFirstStep(this)) {
        MR::startBrk(this, "ValveSwitchOn");
        MR::setBrkFrameAndStop(this, 0.0f);
    }

    mBindActor->mPosition.lerp(mBindActor->mPosition, getSensor("binder")->mPosition, getNerveStep() / 3.0f);

    if (MR::isStep(this, 3)) {
        setNerve(GET_NERVE(ValveSwitch, ValveSwitchNrvValve));
    }
}

void ValveSwitch::exeValve() {
    if (MR::isFirstStep(this)) {
        if (mIsReverse) {
            MR::startBck(this, "ValveSwitchReverseOn");
        } else {
            MR::startBck(this, "ValveSwitchOn");
        }

        MR::startBrk(this, "ValveSwitchOn");
        MR::startBckPlayer("ScrewSwitchOn", static_cast< const char* >(nullptr));
    }

    if (MR::isLessStep(this, NebulaFrameClock::stepsOfMilliseconds(500))) {
        MR::startLevelSound(this, "SE_OJ_LV_VALVE_SWITCH_MOVE");
    }

    if (MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(500))) {
        MR::startSound(this, "SE_OJ_VALVE_SWITCH_ON");
        MR::tryRumblePadVeryStrong(this, PLAYER_1);
        MR::shakeCameraNormal();
    }

    if (MR::isBckStopped(this)) {
        MR::onSwitchA(this);
        setNerve(GET_NERVE(ValveSwitch, ValveSwitchNrvEnd));
    }
}

void ValveSwitch::exeEnd() {
    if (MR::isStep(this, 0)) {
        MR::invalidateHitSensors(this);

        if (MR::isPlayerInRush()) {
            MR::endBindAndPlayerWait(this);
            mBindActor = nullptr;
        }
    }
}

bool ValveSwitch::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (MR::isMsgRushBegin(msg) && MR::isSensorPlayer(pSender) && MR::isOnPlayer(getSensor("binder"))) {
        mBindActor = pSender->mHost;
        MR::startSound(mBindActor, "SE_PV_TWIST_START");
        MR::startSound(mBindActor, "SE_PM_SPIN_ATTACK");
        setNerve(GET_NERVE(ValveSwitch, ValveSwitchNrvAdjust));
        return true;
    }

    if (msg == ACTMES_UPDATE_BASEMTX && mBindActor != nullptr && isNerve(GET_NERVE(ValveSwitch, ValveSwitchNrvValve))) {
        updateBindActorMtx();
        return true;
    }

    return msg == ACTMES_RUSH_CANCEL;
}

void ValveSwitch::updateBindActorMtx() {
    Pos3f posMtx;
    posMtx.set(mBindActor->getBaseMtx());
    HitSensor* sensor = getSensor("binder");
    posMtx.mMtx[0][3] = sensor->mPosition.x;
    posMtx.mMtx[1][3] = sensor->mPosition.y;
    posMtx.mMtx[2][3] = sensor->mPosition.z;
    MR::setBaseTRMtx(mBindActor, posMtx);
}
