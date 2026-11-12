#include "Game/MapObj/ManholeCover.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapObjActorInitInfo.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/SoundUtil.hpp"

namespace {
    const f32 sSensorRadius = 150.0f;
    const s32 sRattleTime = 45;
};  // namespace

namespace NrvManholeCover {
    NEW_NERVE(HostTypeWait, ManholeCover, Wait);
    NEW_NERVE(HostTypeOpen, ManholeCover, Open);
    NEW_NERVE(HostTypeRattle, ManholeCover, Rattle);
};  // namespace NrvManholeCover

void ManholeCover::init(const TableIter& rIter) {
    MapObjActor::init(rIter);
    MapObjActorInitInfo info;
    info.setupDefaultPos();
    info.setupConnectToScene();
    info.setupEffect(nullptr);
    info.setupSound(4);
    info.setupHitSensor();
    info.setupHitSensorParam(1, ::sSensorRadius, Vec3f(0.0f, 0.0f, 0.0f));
    info.setupNerve(GET_NERVE(ManholeCover, HostTypeWait));
    MapObjActor::initialize(rIter, info);
}

bool ManholeCover::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if ((MR::isMsgPlayerHipDropFloor(msg) || MR::isMsgPlayerUpperPunch(msg)) &&
        (isNerve(GET_NERVE(ManholeCover, HostTypeWait)) || isNerve(GET_NERVE(ManholeCover, HostTypeRattle)))) {
        setNerve(GET_NERVE(ManholeCover, HostTypeOpen));

        return true;
    }

    return false;
}

bool ManholeCover::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgSpinStormRange(msg) && isNerve(GET_NERVE(ManholeCover, HostTypeWait))) {
        setNerve(GET_NERVE(ManholeCover, HostTypeRattle));

        return true;
    }

    return false;
}

void ManholeCover::exeOpen() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Rotation");
        MR::invalidateCollisionParts(this);
    }

    if (MR::isBckStopped(this)) {
        MR::validateCollisionParts(this);
        setNerve(GET_NERVE(ManholeCover, HostTypeWait));
    }
}

void ManholeCover::exeRattle() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Vibration");
        MR::startSound(this, "SE_OJ_MANHOLE_RATTLE");
    }

    if (MR::isGreaterStep(this, ::sRattleTime) && MR::isBckStopped(this)) {
        setNerve(GET_NERVE(ManholeCover, HostTypeWait));
    }
}

void ManholeCover::exeWait() {
}
