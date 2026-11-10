#include "Game/MapObj/WaterfallCaveCover.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapObjActorInitInfo.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_exeBreak_f1, "exeBreak.f1", 1000.0f);

namespace {
    NEBULA_TUNABLE(f32, sSensorRadius, 500.0f);
    const s32 sRiddleSeStep = NebulaFrameClock::stepsOfMilliseconds(1500);
};  // namespace

namespace NrvWaterfallCaveCover {
    NEW_NERVE(HostTypeWait, WaterfallCaveCover, Wait);
    NEW_NERVE(HostTypeBreak, WaterfallCaveCover, Break);
};  // namespace NrvWaterfallCaveCover

void WaterfallCaveCover::init(const TableIter& rIter) {
    MapObjActor::init(rIter);
    MapObjActorInitInfo info;
    info.setupDefaultPos();
    info.setupConnectToScene();
    info.setupSound(4);
    info.setupGroupClipping(16);
    info.setupNerve(GET_NERVE(WaterfallCaveCover, HostTypeWait));
    info.setupHitSensor();
    info.setupHitSensorParam(8, ::sSensorRadius, Vec3f(0.0f, 0.0f, 0.0f));
    info.setupEffect(nullptr);
    initialize(rIter, info);
}

bool WaterfallCaveCover::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgJetTurtleAttack(msg) && isNerve(GET_NERVE(WaterfallCaveCover, HostTypeWait))) {
        setNerve(GET_NERVE(WaterfallCaveCover, HostTypeBreak));
        return true;
    }
    return false;
}

void WaterfallCaveCover::exeWait() {
}

void WaterfallCaveCover::exeBreak() {
    if (MR::isFirstStep(this)) {
        MR::invalidateHitSensor(this, "body");
        MR::invalidateCollisionParts(this);
        MapObjActorUtil::startBreak(this);
        MR::startRumbleWithShakeCameraStrong(this, "中", "弱", cNum_exeBreak_f1, FLOAT_MAX);
        MR::startSound(this, "SE_OJ_WATERFALL_COVER_BREAK");
    }

    if (MR::isStep(this, ::sRiddleSeStep)) {
        MR::startSystemSE("SE_SY_READ_RIDDLE_S");
    }

    if (MapObjActorUtil::isBreakStopped(this)) {
        MR::validateClipping(mModelObj);
        mModelObj->kill();
        kill();
    }
}
