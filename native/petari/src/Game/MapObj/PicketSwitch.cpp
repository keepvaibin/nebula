#include "Game/MapObj/PicketSwitch.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvPicketSwitch {
    NEW_NERVE(PicketSwitchNrvWait, PicketSwitch, Wait);
    NEW_NERVE(PicketSwitchNrvFirstDrop, PicketSwitch, FirstDrop);
    NEW_NERVE(PicketSwitchNrvLastDrop, PicketSwitch, LastDrop);
};  // namespace NrvPicketSwitch

PicketSwitch::PicketSwitch(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< PicketSwitch >);
}

void PicketSwitch::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::needStageSwitchWriteA(this, rIter);
    const char* pName;
    MR::getObjectName(&pName, rIter);
    initModelManagerWithAnm(pName, nullptr, false);
    MR::connectToSceneMapObjDecorationStrongLight(this);
    initHitSensor(1);
    MR::addBodyMessageSensor(this, ATYPE_SWITCH);
    MR::initCollisionPartsAutoEqualScale(this, pName, getSensor(0), MR::getJointMtx(this, "Picket"));
    initEffectKeeper(0, nullptr);
    initSound(4, false);
    initNerve(GET_NERVE(PicketSwitch, PicketSwitchNrvWait));
    makeActorAppeared();
}

void PicketSwitch::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBrk(this, "Second");
        MR::setBrkFrameAndStop(this, 0.0f);
    }
}

void PicketSwitch::exeFirstDrop() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "First");
        MR::tryRumblePadStrong(this, PLAYER_1);
        MR::startSound(this, "SE_OJ_PICKET_SWITCH_DROP");
    }
}

void PicketSwitch::exeLastDrop() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Second");
        MR::startBrk(this, "Second");
        MR::tryRumblePadVeryStrong(this, PLAYER_1);
        MR::startSound(this, "SE_OJ_PICKET_SWITCH_DROP");
        MR::startSound(this, "SE_OJ_PICKET_SWITCH_ON");
    }

    if (MR::isBckStopped(this)) {
        MR::onSwitchA(this);
        MR::invalidateHitSensors(this);
    }
}

bool PicketSwitch::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (MR::isMsgPlayerHipDropFloor(msg)) {
        if (pReceiver->isType(ATYPE_SWITCH)) {
            if (isNerve(GET_NERVE(PicketSwitch, PicketSwitchNrvWait))) {
                setNerve(GET_NERVE(PicketSwitch, PicketSwitchNrvLastDrop));
                return true;
            }
            if ((isNerve(GET_NERVE(PicketSwitch, PicketSwitchNrvFirstDrop)))) {
                if (MR::isBckStopped(this)) {
                    setNerve(GET_NERVE(PicketSwitch, PicketSwitchNrvLastDrop));
                    return true;
                }
            }
            return false;
        }
    }
    return false;
}
