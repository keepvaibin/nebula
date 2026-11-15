#include "Game/MapObj/SideSpikeMoveStep.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/Enemy/WalkerStateBindStarPointer.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapObjActorInitInfo.hpp"
#include "Game/Util.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvSideSpikeMoveStep {
    NEW_NERVE(SideSpikeMoveStepNrvWait, SideSpikeMoveStep, Wait);
    NEW_NERVE_ONEND(SideSpikeMoveStepNrvPointerStop, SideSpikeMoveStep, PointerStop, PointerStop);
    NEW_NERVE_ONEND(SideSpikeMoveStepNrvPointerStopSync, SideSpikeMoveStep, PointerStopSync, PointerStopSync);
};  // namespace NrvSideSpikeMoveStep

SideSpikeMoveStep::SideSpikeMoveStep(const char* pName)
    : MapObjActor(pName), mScaleController(nullptr), mStateBindStarPointer(nullptr), mRailClipping(0.0f, 0.0f, 0.0f) {
    lumacore::setClass(this, lumacore::kClassId< SideSpikeMoveStep >);
}

void SideSpikeMoveStep::init(const TableIter& rIter) {
    MapObjActor::init(rIter);
    MapObjActorInitInfo info;
    info.setupDefaultPos();
    info.setupConnectToScene();
    info.setupGroupClipping(32);
    info.setupSound(6);
    info.setupRailMover();
    info.setupShadow(nullptr);
    info.setupNerve(GET_NERVE(SideSpikeMoveStep, SideSpikeMoveStepNrvWait));
    MapObjActorUtil::setupInitInfoTypical(&info, mObjectName);
    initialize(rIter, info);
    initEffectKeeper(1, mObjectName);
    f32 radius = 0.0f;
    MR::calcModelBoundingRadius(&radius, this);
    MR::initAndSetRailClipping(&mRailClipping, this, 100.0f, radius);
    MR::joinToGroupArray(this, rIter, nullptr, 32);
    MR::initStarPointerTarget(this, 300.0f, Vec3f(0.0f, 0.0f, 0.0f));
    mScaleController = new AnimScaleController(nullptr);
    mScaleController->setParamTight();
    mStateBindStarPointer = new WalkerStateBindStarPointer(this, mScaleController);
}

void SideSpikeMoveStep::exeWait() {
    if (MR::isFirstStep(this)) {
        MapObjActorUtil::resumeAllMapPartsFunctions(this);
    }
    if (MapObjActorUtil::isRailMoverWorking(this)) {
        MR::startLevelSound(this, "SE_OJ_LV_SIDE_SPIKE_STEP_MV");
        if (MapObjActorUtil::isRailMoverReachedEnd(this)) {
            MR::startSound(this, "SE_OJ_SIDE_SPIKE_STEP_ED");
        }
    }
    if (mStateBindStarPointer->tryStartPointBind()) {
        MR::sendMsgToGroupMember(ACTMES_GROUP_MOVE_STOP, this, getSensor("body"), "body");
        setNerve(GET_NERVE(SideSpikeMoveStep, SideSpikeMoveStepNrvPointerStop));
    }
}

void SideSpikeMoveStep::exePointerStop() {
    if (MR::isFirstStep(this)) {
        MapObjActorUtil::pauseAllMapPartsFunctions(this);
    }
    if (MR::updateActorState(this, mStateBindStarPointer)) {
        MR::sendMsgToGroupMember(ACTMES_GROUP_MOVE_START, this, getSensor("body"), "body");
    }
}

void SideSpikeMoveStep::endPointerStop() {
    mStateBindStarPointer->kill();
}

void SideSpikeMoveStep::exePointerStopSync() {
    if (MR::isFirstStep(this)) {
        MapObjActorUtil::pauseAllMapPartsFunctions(this);
        mScaleController->startPointerHitVibration();
    }
}

void SideSpikeMoveStep::endPointerStopSync() {
    mStateBindStarPointer->kill();
}

void SideSpikeMoveStep::calcAndSetBaseMtx() {
    MapObjActor::calcAndSetBaseMtx();
    if (MR::isInitializeStateEnd()) {
        Vec3f scale = mScaleController->mScale * mScale;
        MR::setBaseScale(this, scale);
    }
}

void SideSpikeMoveStep::control() {
    mScaleController->updateNerve();
    MapObjActor::control();
}

bool SideSpikeMoveStep::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (msg == ACTMES_GROUP_MOVE_STOP) {
        if (!isNerve(GET_NERVE(SideSpikeMoveStep, SideSpikeMoveStepNrvWait))) {
            return false;
        }
        setNerve(GET_NERVE(SideSpikeMoveStep, SideSpikeMoveStepNrvPointerStopSync));
        return true;
    } else if (msg == ACTMES_GROUP_MOVE_START) {
        if (isNerve(GET_NERVE(SideSpikeMoveStep, SideSpikeMoveStepNrvWait))) {
            return false;
        }
        setNerve(GET_NERVE(SideSpikeMoveStep, SideSpikeMoveStepNrvWait));
        return true;
    }

    return false;
}
