#include "Game/MapObj/SpaceShipStep.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapObjActor.hpp"
#include "Game/MapObj/MapObjActorInitInfo.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateResetToInitPosCheck_f1, "updateResetToInitPosCheck.f1", 700.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateResetToInitPosCheck_f2, "updateResetToInitPosCheck.f2", 700.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateResetToInitPosCheck_f3, "updateResetToInitPosCheck.f3", 700.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_updateResetToInitPosCheck_isNearPlayer_1, "updateResetToInitPosCheck.isNearPlayer.1", 3000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateResetToInitPosCheck_isNearPlayer_2, "updateResetToInitPosCheck.isNearPlayer.2", 1000.0f);

namespace NrvSpaceShipStep {
    NEW_NERVE(SpaceShipStepNrvWait, SpaceShipStep, Wait);
    NEW_NERVE(SpaceShipStepNrvMoveSign, SpaceShipStep, MoveSign);
    NEW_NERVE(SpaceShipStepNrvMove, SpaceShipStep, Move);
    NEW_NERVE(SpaceShipStepNrvBreak, SpaceShipStep, Break);
};  // namespace NrvSpaceShipStep

SpaceShipStep::SpaceShipStep(const char* pName) : MapObjActor(pName), mRailClippingCenter(0.0f, 0.0f, 0.0f), mHiddenStep(), mHasBeenSeen() {
    lumacore::setClass(this, lumacore::kClassId< SpaceShipStep >);
}

void SpaceShipStep::init(const TableIter& rIter) {
    MapObjActor::init(rIter);
    MapObjActorInitInfo info;
    info.setupDefaultPos();
    info.setupConnectToScene();
    info.setupEffect(nullptr);
    info.setupSound(4);
    info.setupRailMover();
    info.setupBaseMtxFollowTarget();
    info.setupNerve(GET_NERVE(SpaceShipStep, SpaceShipStepNrvWait));
    initialize(rIter, info);
    f32 radius = 0.0f;
    MR::calcModelBoundingRadius(&radius, this);
    MR::initAndSetRailClipping(&mRailClippingCenter, this, cNum_init_f1, radius);
}

void SpaceShipStep::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::tryStartBck(this, "Wait");
        MR::validateClipping(this);
    }

    if (MR::isOnPlayer(this)) {
        setNerve(GET_NERVE(SpaceShipStep, SpaceShipStepNrvMoveSign));
    }
}

void SpaceShipStep::exeMoveSign() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "OnMario");
        MR::startSound(this, "SE_OJ_SPACE_SHIP_STEP_ST");
        MR::invalidateClipping(this);
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(SpaceShipStep, SpaceShipStepNrvMove));
    }
}

void SpaceShipStep::exeMove() {
    if (MR::isFirstStep(this)) {
        MR::tryStartBck(this, "Wait");
        MapObjActorUtil::startAllMapPartsFunctions(this);
    }

    MR::startLevelSound(this, "SE_OJ_LV_SPACE_SHIP_STEP_MV");
}

void SpaceShipStep::exeBreak() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Break");
        MR::startSound(this, "SE_OJ_SPACE_SHIP_STEP_BREAK");
        MR::startSound(this, "SE_OJ_SPACE_SHIP_STEP_ED");
    }
}

void SpaceShipStep::control() {
    MapObjActor::control();
    updateResetToInitPosCheck();
}

bool SpaceShipStep::receiveMsgEnemyAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (isNerve(GET_NERVE(SpaceShipStep, SpaceShipStepNrvBreak))) {
        return false;
    }

    if (MR::isMsgExplosionAttack(msg)) {
        setNerve(GET_NERVE(SpaceShipStep, SpaceShipStepNrvBreak));
        return true;
    }

    return false;
}

void SpaceShipStep::updateResetToInitPosCheck() {
    if (!mHasBeenSeen) {
        mHasBeenSeen = !MR::isJudgedToClipFrustum(mPosition, cNum_updateResetToInitPosCheck_f1);
    } else {
        if (isNerve(GET_NERVE(SpaceShipStep, SpaceShipStepNrvWait)) || !MR::isJudgedToClipFrustum(mPosition, cNum_updateResetToInitPosCheck_f2)) {
            mHiddenStep = 0;
        } else {
            if (mHiddenStep < NebulaFrameClock::stepsOfSeconds(4)) {
                mHiddenStep++;
            } else {
                MapObjActorUtil::resetRailMoverToInitPos(this);

                if (!MR::isJudgedToClipFrustum(mPosition, cNum_updateResetToInitPosCheck_f3) && MR::isNearPlayer(this, cNum_updateResetToInitPosCheck_isNearPlayer_1)) {
                    MR::emitEffect(this, "Appear");
                    MR::startSound(this, "SE_OJ_SPACE_SHIP_STEP_RESET");
                }

                if (MR::isNearPlayer(this, cNum_updateResetToInitPosCheck_isNearPlayer_2)) {
                    setNerve(GET_NERVE(SpaceShipStep, SpaceShipStepNrvWait));
                } else {
                    setNerve(GET_NERVE(SpaceShipStep, SpaceShipStepNrvMove));
                }

                mHiddenStep = 0;
                mHasBeenSeen = 0;
            }
        }
    }
}
