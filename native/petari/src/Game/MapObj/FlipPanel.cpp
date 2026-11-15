#include <nebula_game_api.h>
#include "Game/MapObj/FlipPanel.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/LiveActorGroupArray.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapObjActorInitInfo.hpp"
#include "Game/Util.hpp"
#include <nebula_geometry.hpp>
#include "Game/System/LumaCore.hpp"

namespace {
    // The most panels in a FlipPanel group (0x20).
    const s32 cPanelGroupSize = 0x20;
    // The same for the group the observer reads.
    const s32 cObserverGroupSize = 0x20;
};  // namespace

namespace NrvFlipPanel {
    NEW_NERVE(FlipPanelNrvFrontLand, FlipPanel, FrontLand);
    NEW_NERVE(FlipPanelNrvBackLand, FlipPanel, BackLand);
    NEW_NERVE(FlipPanelNrvFront, FlipPanel, Wait);
    NEW_NERVE(FlipPanelNrvBack, FlipPanel, Wait);
    NEW_NERVE(FlipPanelNrvEndPrepare, FlipPanel, EndPrepare);
    NEW_NERVE(FlipPanelNrvEnd, FlipPanel, End);
};  // namespace NrvFlipPanel

namespace {
    static u32 sBloomSyncStep;

    // The frame count is 0 until the bloom animation starts: the step itself is then the frame.
    void syncBloomFrame(ModelObj* pBloomModel) {
        if (pBloomModel == nullptr) {
            return;
        }

        u32 frameMax = MR::getBrkFrameMax(pBloomModel);
        u32 cycles = frameMax != 0 ? sBloomSyncStep / frameMax : 0;
        MR::setBrkFrame(pBloomModel, sBloomSyncStep - cycles * frameMax);
    }
};  // namespace

namespace NrvFlipPanelObserver {
    NEW_NERVE(FlipPanelObserverNrvWait, FlipPanelObserver, Wait);
    NEW_NERVE(FlipPanelObserverNrvComplete, FlipPanelObserver, Complete);
    NEW_NERVE(FlipPanelObserverNrvDemoWait, FlipPanelObserver, DemoWait);
};  // namespace NrvFlipPanelObserver

FlipPanel::FlipPanel(const char* pName) : MapObjActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< FlipPanel >);
    mDelegator = 0;
    mIsPlayerOn = 0;
    mIsFlipped = 0;
    mJumpCheckCount = 0;
    mIsReverse = false;
}

void FlipPanel::init(const TableIter& rIter) {
    MapObjActor::init(rIter);
    MapObjActorInitInfo info;
    info.setupDefaultPos();
    info.setupConnectToScene();
    info.setupEffect("FlipPanel");
    info.setupSound(4);
    info.setupNerve(GET_NERVE(FlipPanel, FlipPanelNrvFront));
    initialize(rIter, info);
    mIsReverse = isObjectName("FlipPanelReverse");
    mDelegator = MR::createJointDelegatorWithNullChildFunc(this, &FlipPanel::calcJointMove, "Panel");
    MR::joinToGroupArray(this, rIter, 0, ::cPanelGroupSize);
    MR::invalidateClipping(this);
    MR::startBck(this, "PanelB");
    f32 frameMax = MR::getBckFrameMax(this);
    MR::setBckFrameAndStop(this, frameMax);

    if (mIsReverse) {
        MapObjActorUtil::appearBloomModel(this);
    } else {
        MapObjActorUtil::killBloomModel(this);
    }
}

void FlipPanel::exeFrontLand() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "PanelB");

        if (mIsReverse) {
            MapObjActorUtil::appearBloomModel(this);
            syncBloomFrame(mBloomModel);
        } else {
            MapObjActorUtil::killBloomModel(this);
        }

        MR::tryRumblePadMiddle(this, PLAYER_1);
        MR::startSound(this, "SE_OJ_FLIP_PANEL_CHANGE");

        if (mIsFlipped) {
            MR::sendMsgToGroupMember(ACTMES_FAILURE, this, getSensor(0), "body");
            mIsFlipped = 0;
        }
    }

    if (MR::isOnGroundPlayer() && !MR::isOnPlayer(this)) {
        setNerve(GET_NERVE(FlipPanel, FlipPanelNrvFront));
    }
}

void FlipPanel::exeBackLand() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "PanelA");

        if (mIsReverse) {
            MapObjActorUtil::killBloomModel(this);
        } else {
            MapObjActorUtil::appearBloomModel(this);
            syncBloomFrame(mBloomModel);
        }

        MR::tryRumblePadMiddle(this, PLAYER_1);
        MR::startSound(this, "SE_OJ_FLIP_PANEL_CHANGE");

        if (!mIsFlipped) {
            MR::sendMsgToGroupMember(ACTMES_SUCCESS, this, getSensor(0), "body");
            mIsFlipped = 1;
        }
    }

    if (MR::isOnGroundPlayer() && !MR::isOnPlayer(this)) {
        setNerve(GET_NERVE(FlipPanel, FlipPanelNrvBack));
    }
}

void FlipPanel::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::validateClipping(this);
    }

    if (mIsPlayerOn && !MR::isOnPlayer(this) && MR::isOnGroundPlayer()) {
        mIsPlayerOn = 0;
    }

    if (!mIsPlayerOn) {
        if (checkPlayerOnTop()) {
            if (isNerve(GET_NERVE(FlipPanel, FlipPanelNrvFront))) {
                setNerve(GET_NERVE(FlipPanel, FlipPanelNrvBackLand));
            } else {
                setNerve(GET_NERVE(FlipPanel, FlipPanelNrvFrontLand));
            }
        }
    }
}

void FlipPanel::exeEndPrepare() {
    if (MR::isStep(this, 20)) {
        setNerve(GET_NERVE(FlipPanel, FlipPanelNrvEnd));
    }
}

void FlipPanel::exeEnd() {
    if (MR::isFirstStep(this)) {
        MR::startSystemSE("SE_OJ_FLIP_PANEL_COMPLETE");
        MR::startBck(this, "PanelEnd");
        MapObjActorUtil::killBloomModel(this);
        mIsPlayerOn = 0;
    }

    if (MR::isBckStopped(this)) {
        if (!MR::isEffectValid(this, "FlipPanelGold")) {
            MR::validateClipping(this);
        }
    }
}

void FlipPanel::appear() {
    MapObjActor::appear();
    if (mIsReverse) {
        MapObjActorUtil::appearBloomModel(this);
    } else {
        MapObjActorUtil::killBloomModel(this);
    }
}

void FlipPanel::endClipped() {
    MapObjActor::endClipped();

    if (mBloomModel != nullptr && MR::getBrkCtrl(mBloomModel)) {
        syncBloomFrame(mBloomModel);
    }
}

void FlipPanel::calcAndSetBaseMtx() {
    MapObjActor::calcAndSetBaseMtx();

    if (mIsPlayerOn) {
        mDelegator->registerCallBack();
    }
}

bool FlipPanel::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (isNerve(GET_NERVE(FlipPanel, FlipPanelNrvEnd))) {
        return false;
    }

    if (msg == ACTMES_SUCCESS || msg == ACTMES_FAILURE) {
        mIsPlayerOn = this == pSender->mHost;

        return true;
    }

    if (msg == ACTMES_GROUP_MOVE_STOP) {
        MR::invalidateClipping(this);
        setNerve(GET_NERVE(FlipPanel, FlipPanelNrvEndPrepare));

        return true;
    }

    return false;
}

bool FlipPanel::calcJointMove(Pos3f* pPos, const JointControllerInfo& /*rInfo*/) {
    if (!mIsPlayerOn) {
        return false;
    }

    Vec3f jointPos;
    MR::copyJointPos(this, "Panel", &jointPos);
    Vec3f upVec;
    MR::calcUpVec(&upVec, this);
    jointPos.scaleAdd(-25.0f, upVec, jointPos);
    pPos->setTrans(jointPos);
    return true;
}

bool FlipPanel::checkPlayerOnTop() {
    if (MR::isPlayerJumpRising() && mJumpCheckCount) {
        mJumpCheckCount = 0;
        return true;
    }

    if (!MR::isOnPlayer(this)) {
        mJumpCheckCount = 0;
        return false;
    }

    Vec3f upVec;
    MR::calcUpVec(&upVec, this);

    if (!MR::getPlayerGroundingPolygon()) {
        return false;
    }

    if (upVec != *MR::getPlayerGroundNormal()) {
        mJumpCheckCount = 0;
        return false;
    }

    if (mJumpCheckCount < 2) {
        mJumpCheckCount = mJumpCheckCount + 1;
        return false;
    }

    mJumpCheckCount = 0;
    return true;
}

FlipPanelObserver::FlipPanelObserver(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< FlipPanelObserver >);
    mGroup = 0;
    mFlippedNum = 0;
    mDemoDelay = 0;
    mPowerStarId = -1;
    mIsHeavensDoor = 0;
    ::sBloomSyncStep = 0;
}

void FlipPanelObserver::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::connectToSceneMapObjMovement(this);
    initHitSensor(1);
    MR::addBodyMessageSensorMapObj(this);
    MR::invalidateClipping(this);
    mGroup = MR::joinToGroupArray(this, rIter, 0, ::cObserverGroupSize);
    MR::useStageSwitchWriteA(this, rIter);
    initSound(2, false);
    MR::getPlacementArg7NoInit(rIter, &mPowerStarId);

    if (mPowerStarId != -1) {
        if (!mPowerStarId) {
            MR::declarePowerStar(this);
        } else {
            MR::declarePowerStar(this, mPowerStarId);
        }
    }

    MR::getPlacementArg0NoInit(rIter, &mDemoDelay);
    mDemoDelay += 0x28;

    if (MR::isEqualStageName("HeavensDoorGalaxy")) {
        mIsHeavensDoor = 1;
    } else {
        mIsHeavensDoor = 0;
    }

    initNerve(GET_NERVE(FlipPanelObserver, FlipPanelObserverNrvWait));
    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::syncStageSwitchAppear(this);
        makeActorDead();
    } else {
        makeActorAppeared();
    }
}

void FlipPanelObserver::exeWait() {
    if (MR::isFirstStep(this) && MR::isValidSwitchAppear(this)) {
        MR::callAppearAllGroupMember(this);
        MR::startSound(this, "SE_OJ_FLIP_PANEL_APPEAR");
    }

    if (mFlippedNum == mGroup->getObjNum() - 1 && MR::tryStartDemo(this, "FlipPanelComplete")) {
        setNerve(GET_NERVE(FlipPanelObserver, FlipPanelObserverNrvComplete));
    } else {
        ::sBloomSyncStep++;
    }
}

void FlipPanelObserver::exeComplete() {
    if (MR::isFirstStep(this)) {
        MR::callRequestMovementOnAllGroupMember(this);
        MR::requestMovementOn(mGroup);
        MR::sendMsgToGroupMember(ACTMES_GROUP_MOVE_STOP, this, getSensor(0), "body");
    }

    if (MR::isGreaterStep(this, mDemoDelay)) {
        MR::endDemo(this, "FlipPanelComplete");

        if (mPowerStarId != -1) {
            if (!mPowerStarId) {
                MR::requestAppearPowerStar(this, mPosition);
            } else {
                MR::requestAppearPowerStar(this, mPowerStarId, mPosition);
            }
        }

        setNerve(GET_NERVE(FlipPanelObserver, FlipPanelObserverNrvDemoWait));
    }
}

void FlipPanelObserver::exeDemoWait() {
    if (!MR::isDemoActive()) {
        if (MR::isValidSwitchA(this)) {
            MR::onSwitchA(this);
        }

        makeActorDead();
    }
}

void FlipPanelObserver::initAfterPlacement() {
    if (MR::isValidSwitchAppear(this)) {
        MR::callMakeActorDeadAllGroupMember(this);
    }
}

bool FlipPanelObserver::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (msg == ACTMES_SUCCESS) {
        if (!mFlippedNum) {
            if (mIsHeavensDoor) {
                MR::startSystemME("ME_FLIP_PANEL_INV_OFF_FIRST");
            } else {
                MR::startSystemME("ME_FLIP_PANEL_ON_FIRST");
            }
        } else {
            if (mIsHeavensDoor) {
                MR::startSystemME("ME_FLIP_PANEL_INV_OFF");
            } else {
                MR::startSystemME("ME_FLIP_PANEL_ON");
            }
        }

        mFlippedNum++;

        return true;
    } else if (msg == ACTMES_FAILURE) {
        if (mIsHeavensDoor) {
            MR::startSystemME("ME_FLIP_PANEL_INV_ON");
        } else {
            MR::startSystemME("ME_FLIP_PANEL_OFF");
        }

        mFlippedNum--;

        return true;
    }

    return false;
}
