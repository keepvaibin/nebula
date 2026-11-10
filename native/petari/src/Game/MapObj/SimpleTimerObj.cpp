#include "Game/MapObj/SimpleTimerObj.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/FlashingCtrl.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapObjActorInitInfo.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const s32 sDefaultTimeLimit = NebulaFrameClock::stepsOfSeconds(10);
    const s32 sBlinkTime = NebulaFrameClock::stepsOfSeconds(2);
};  // namespace

namespace NrvSimpleTimerObj {
    NEW_NERVE(SimpleTimerObjNrvTimer, SimpleTimerObj, Timer);
};  // namespace NrvSimpleTimerObj

SimpleTimerObj::SimpleTimerObj(const char* pName) : MapObjActor(pName), mFlashingCtrl(), mTimeLimit(::sDefaultTimeLimit), mTimeLeft() {
    lumacore::setClass(this, lumacore::kClassId< SimpleTimerObj >);
}

void SimpleTimerObj::init(const TableIter& rIter) {
    MapObjActor::init(rIter);
    MapObjActorInitInfo info;
    MapObjActorUtil::setupInitInfoSimpleMapObj(&info);
    info.setupNerve(GET_NERVE(SimpleTimerObj, SimpleTimerObjNrvTimer));
    initialize(rIter, info);
    MR::getPlacementArg7NoInit(rIter, &mTimeLimit);
    mFlashingCtrl = new FlashingCtrl(this, true);
    MR::invalidateShadow(this, 0);
}

void SimpleTimerObj::exeTimer() {
    if (MR::isFirstStep(this)) {
        mTimeLeft = mTimeLimit;
    }
}

void SimpleTimerObj::appear() {
    MapObjActor::appear();
    MR::showModel(this);
    MR::validateShadow(this, 0);
    setNerve(GET_NERVE(SimpleTimerObj, SimpleTimerObjNrvTimer));
}

void SimpleTimerObj::kill() {
    mFlashingCtrl->end();
    MR::invalidateShadow(this, 0);
    MR::emitEffect(this, "Appear");
    MapObjActor::kill();
}

void SimpleTimerObj::control() {
    mTimeLeft--;

    if (mTimeLeft == ::sBlinkTime) {
        mFlashingCtrl->start(::sBlinkTime);
    } else if (mTimeLeft == 0) {
        kill();
    }
}
