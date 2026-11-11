#include "Game/NPC/StrayTico.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/PartsModel.hpp"
#include "Game/NPC/CollectTico.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f1, "initSensor.f1", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f2, "initSensor.f2", 45.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initShadow_f1, "initShadow.f1", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initShadow_f2, "initShadow.f2", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initShadow_f3, "initShadow.f3", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveMsgPlayerAttack_f1, "receiveMsgPlayerAttack.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveOtherMsg_f1, "receiveOtherMsg.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeLauncherCamera_f1, "exeLauncherCamera.f1", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeLauncherCamera_f2, "exeLauncherCamera.f2", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeLauncherCamera_f3, "exeLauncherCamera.f3", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeLauncherCamera_f4, "exeLauncherCamera.f4", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateWarp_f1, "updateWarp.f1", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateWarp_f2, "updateWarp.f2", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateChase_f1, "updateChase.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateChase_f2, "updateChase.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateChase_f3, "updateChase.f3", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateChase_f4, "updateChase.f4", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateChase_f5, "updateChase.f5", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeCompleteDemo_f1, "exeCompleteDemo.f1", 900.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_isNearPlayer_1, "exeWait.isNearPlayer.1", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeLauncherCamera_addVelocityMoveToTarget_1, "exeLauncherCamera.addVelocityMoveToTarget.1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateChase_reboundVelocityFromCollision_1, "updateChase.reboundVelocityFromCollision.1", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateChase_reboundVelocityFromCollision_2, "updateChase.reboundVelocityFromCollision.2", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateChase_reboundVelocityFromCollision_3, "updateChase.reboundVelocityFromCollision.3", 1.0f);

namespace {
    NEBULA_TUNABLE(f32, sPushAccel, 0.3f);
    NEBULA_TUNABLE(f32, sWaitTurnDegree, 2.0f);
    NEBULA_TUNABLE(f32, sChaseTurnDegree, 4.0f);
    NEBULA_TUNABLE(f32, sChaseFric, 0.97f);
    NEBULA_TUNABLE(f32, sChaseDistance, 400.0f);
    NEBULA_TUNABLE(f32, sWarpTryDistance, 2000.0f);
    NEBULA_TUNABLE(f32, sUpdateWarpDistance, 1600.0f);
    NEBULA_TUNABLE(f32, sAwayDistance, 300.0f);
    NEBULA_TUNABLE(f32, sUpVecBlendRate, 0.2f);
    NEBULA_TUNABLE(f32, sFrontVecBlendRate, 0.2f);
    NEBULA_TUNABLE(s32, sCompletePlacementTime, 40);
    static const s32 sCompleteRotateStartTime = NebulaFrameClock::stepsOfSeconds(1);
    NEBULA_TUNABLE(s32, sCompleteDemoTime, 200);
    NEBULA_TUNABLE(f32, sCompleteDemoRotate, 4.0f);
    NEBULA_TUNABLE(f32, sCompleteStartRadius, 450.0f);
};  // namespace

namespace NrvStrayTico {
    NEW_NERVE(StrayTicoNrvWait, StrayTico, Wait);
    NEW_NERVE(StrayTicoNrvGlad, StrayTico, Glad);
    NEW_NERVE(StrayTicoNrvChase, StrayTico, Chase);
    NEW_NERVE(StrayTicoNrvLauncherCamera, StrayTico, LauncherCamera);
    NEW_NERVE(StrayTicoNrvCompleteDemo, StrayTico, CompleteDemo);
};  // namespace NrvStrayTico

StrayTico::StrayTico(const char* pName, CollectTico* pHost)
    : LiveActor(pName), mHost(pHost), mItemBubbleModel(), mRotateQuat(0.0f, 0.0f, 0.0f, 1.0f), mFrontVec(0.0f, 0.0f, 1.0f), mWarpPos(0.0f, 0.0f, 0.0f),
      mDemoCenter(0.0f, 0.0f, 0.0f), mDemoUp(0.0f, 1.0f, 0.0f), mDemoFront(0.0f, 0.0f, 1.0f), mDemoAngle() {
    lumacore::setClass(this, lumacore::kClassId< StrayTico >);
}

void StrayTico::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("StrayTico", nullptr, false);
    MR::connectToSceneNpc(this);
    MR::makeQuatAndFrontFromRotate(&mRotateQuat, &mFrontVec, this);
    mWarpPos.set(mPosition);
    MR::calcGravity(this);
    initNerve(GET_NERVE(StrayTico, StrayTicoNrvWait));
    initSensor();
    initShadow();
    initEffectKeeper(0, nullptr);
    initSound(4, false);
    initBinder(cNum_init_f1, cNum_init_f2, 0);
    MR::offBind(this);

    mItemBubbleModel = MR::createPartsModelMapObj(this, "アワ", "ItemBubble", nullptr);
    mItemBubbleModel->initFixedPosition(Vec3f(0.0f, 0.0f, 0.0f), Vec3f(0.0f, 0.0f, 0.0f), nullptr);

    MR::registerDemoSimpleCastAll(mItemBubbleModel);
    MR::startAction(mItemBubbleModel, "Move");

    MR::useStageSwitchWriteA(this, rIter);

    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::syncStageSwitchAppear(this);
        makeActorDead();
        mItemBubbleModel->kill();
    } else {
        makeActorAppeared();
        mItemBubbleModel->appear();
    }
}

void StrayTico::initSensor() {
    initHitSensor(2);
    MR::addHitSensorNpc(this, "Bubble", 8, cNum_initSensor_f1, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorNpc(this, "Body", 8, cNum_initSensor_f2, Vec3f(0.0f, 0.0f, 0.0f));
    MR::validateHitSensor(this, "Bubble");
    MR::invalidateHitSensor(this, "Body");
}

inline void StrayTico::setShadowDrop(const char* const pos) {
    const Vec3f offset(0.0f, 0.0f, 0.0f);
    MR::setShadowDropPositionAtJoint(this, nullptr, pos, offset);
}

void StrayTico::initShadow() {
    MR::initShadowVolumeOval(this, Vec3f(cNum_initShadow_f1, cNum_initShadow_f2, cNum_initShadow_f3));
    setShadowDrop("PowerStarC");
    MR::onCalcShadow(this, nullptr);
}

void StrayTico::appear() {
    LiveActor::appear();
    startGlad();
}

void StrayTico::control() {
    MR::blendQuatUpFront(&mRotateQuat, -mGravity, mFrontVec, ::sUpVecBlendRate, ::sFrontVecBlendRate);

    if (isNerve(GET_NERVE(StrayTico, StrayTicoNrvCompleteDemo))) {
        return;
    }

    if (isNerve(GET_NERVE(StrayTico, StrayTicoNrvWait))) {
        MR::startLevelSound(this, "SE_SM_LV_STRAYTICO_WAIT");
    } else {
        MR::startLevelSound(this, "SE_SM_LV_STRAYTICO_WAIT_GRP");
    }
}

void StrayTico::calcAndSetBaseMtx() {
    MR::setBaseTRMtx(this, mRotateQuat);
}

void StrayTico::startGlad() {
    mItemBubbleModel->kill();
    MR::onCalcGravity(this);
    MR::onCalcShadow(this, nullptr);
    MR::onBind(this);
    MR::invalidateClipping(this);
    MR::validateHitSensor(this, "Body");
    MR::invalidateHitSensor(this, "Bubble");
    setNerve(GET_NERVE(StrayTico, StrayTicoNrvGlad));
}

void StrayTico::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!isNerve(GET_NERVE(StrayTico, StrayTicoNrvWait)) && MR::isSensorNpc(pReceiver) && MR::sendMsgPush(pReceiver, pSender)) {
        MR::addVelocityAwayFromTarget(this, MR::getSensorPos(pReceiver), ::sPushAccel);
    }

    if (isNerve(GET_NERVE(StrayTico, StrayTicoNrvWait)) && MR::isSensorPlayer(pReceiver)) {
        MR::tryRumblePadMiddle(this, PLAYER_1);
        MR::emitEffect(this, "Break");
        MR::startSound(this, "SE_OJ_ITEM_BUBBLE_BREAK");
        startGlad();
    }
}

bool StrayTico::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgStarPieceAttack(msg) && isNerve(GET_NERVE(StrayTico, StrayTicoNrvWait))) {
        MR::limitedStarPieceHitSound();

        return true;
    }

    if (MR::isMsgStarPieceReflect(msg) && !isNerve(GET_NERVE(StrayTico, StrayTicoNrvWait))) {
        if (isNerve(GET_NERVE(StrayTico, StrayTicoNrvChase))) {
            Vec3f sensorDir;
            MR::calcSensorDirectionNormalize(&sensorDir, pSender, pReceiver);
            MR::setVelocity(this, sensorDir * cNum_receiveMsgPlayerAttack_f1);
        }

        return true;
    }

    return false;
}

bool StrayTico::receiveMsgPush(HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (!isNerve(GET_NERVE(StrayTico, StrayTicoNrvWait)) && MR::isSensorNpc(pSender)) {
        MR::addVelocityAwayFromTarget(this, MR::getSensorPos(pSender), ::sPushAccel);

        return true;
    }

    return false;
}

bool StrayTico::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (isEnableSpin() && MR::isMsgSpinStormRange(msg)) {
        MR::addVelocityClockwiseToTarget(this, MR::getSensorPos(pSender), cNum_receiveOtherMsg_f1);
        MR::addVelocityMoveToTarget(this, MR::getSensorPos(pSender), 1.0f);

        return true;
    }

    return false;
}

bool StrayTico::requestCompleteDemo(const Vec3f& rCenter, const Vec3f& rUp, f32 angle) {
    mDemoStartPos = mPosition;
    mDemoCenter = rCenter;
    mDemoUp = rUp;
    mDemoAngle = angle;

    MR::normalize(&mDemoUp);
    MR::makeAxisVerticalZX(&mDemoFront, mDemoUp);

    setNerve(GET_NERVE(StrayTico, StrayTicoNrvCompleteDemo));

    return true;
}

void StrayTico::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
    }

    if (MR::isNearPlayer(this, cNum_exeWait_isNearPlayer_1)) {
        MR::turnDirectionToTargetDegree(this, &mFrontVec, *MR::getPlayerPos(), ::sWaitTurnDegree);
    }
}

void StrayTico::exeGlad() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Glad");
        s32 noRescuedCount = mHost->calcNoRescuedCount();
        const s32 rescuedCount = mHost->mTicoNum - noRescuedCount;
        MR::startSound(this, "SE_SM_STRAYTICO_SPIN");
        MR::startSound(this, "SE_SM_STRAYTICO_GET", rescuedCount);

        if (noRescuedCount == 1) {
            setNerve(GET_NERVE(StrayTico, StrayTicoNrvChase));
            return;
        }
    }

    if (MR::isBckStopped(this)) {
        if (MR::isValidSwitchA(this)) {
            MR::onSwitchA(this);
        }

        setNerve(GET_NERVE(StrayTico, StrayTicoNrvChase));
        MR::startSound(this, "SE_SV_STRAYTICO_GLAD");
    }
}

void StrayTico::exeChase() {
    if (MR::isFirstStep(this)) {
        MR::validateHitSensor(this, "Body");
        MR::invalidateHitSensor(this, "Bubble");
        MR::onBind(this);
        MR::tryStartBck(this, "Chase");
        mWarpPos.set(*MR::getPlayerCenterPos());
    }

    updateWarp();
    updateChase();

    if (MR::isActiveLauncherCamera() || MR::isActiveLauncherFlightCamera()) {
        setNerve(GET_NERVE(StrayTico, StrayTicoNrvLauncherCamera));
    }
}

void StrayTico::exeLauncherCamera() {
    if (MR::isFirstStep(this)) {
        MR::invalidateHitSensors(this);
        MR::offBind(this);
    }

    Vec3f vec;
    vec.scaleAdd(-cNum_exeLauncherCamera_f1, MR::getCamZdir(), *MR::getPlayerPos());

    MR::addVelocityMoveToTarget(this, vec, cNum_exeLauncherCamera_addVelocityMoveToTarget_1);
    MR::addVelocityKeepHeight(this, vec, cNum_exeLauncherCamera_f2, cNum_exeLauncherCamera_f3, cNum_exeLauncherCamera_f4);
    MR::attenuateVelocity(this, ::sChaseFric);

    if (MR::isActiveLauncherCamera()) {
        return;
    }

    if (MR::isActiveLauncherFlightCamera()) {
        return;
    }

    setNerve(GET_NERVE(StrayTico, StrayTicoNrvChase));
}

void StrayTico::updateWarp() {
    bool isNotNearPlayer = !MR::isNearPlayerAnyTime(this, ::sWarpTryDistance);

    if (!isNotNearPlayer) {
        return;
    }

    bool isNotNearPlayer2 = !MR::isNearPlayer(mWarpPos, ::sUpdateWarpDistance);

    if (isNotNearPlayer2) {
        mWarpPos.set(*MR::getPlayerCenterPos());
    }

    if (MR::isJudgedToClipFrustum(mPosition, cNum_updateWarp_f1) && MR::isJudgedToClipFrustum(mWarpPos, cNum_updateWarp_f2)) {
        MR::resetPosition(this, mWarpPos);
    }
}

void StrayTico::updateChase() {
    bool isNotNearPlayer = !MR::isNearPlayerAnyTime(this, ::sChaseDistance);

    if (isNotNearPlayer) {
        MR::addVelocityMoveToTarget(this, *MR::getPlayerPos(), cNum_updateChase_f1);
        MR::tryStartBck(this, "Chase");
    } else if (MR::isNearPlayerAnyTime(this, ::sAwayDistance)) {
        MR::addVelocityAwayFromTarget(this, *MR::getPlayerPos(), cNum_updateChase_f2);
        MR::tryStartBck(this, "Chase");
    } else {
        MR::tryStartBck(this, "Wait");
    }

    Vec3f playerFrontVec;
    MR::getPlayerFrontVec(&playerFrontVec);

    MR::turnDirectionDegree(this, &mFrontVec, playerFrontVec, ::sChaseTurnDegree);
    MR::addVelocityKeepHeight(this, *MR::getPlayerPos(), cNum_updateChase_f3, cNum_updateChase_f4, cNum_updateChase_f5);
    MR::attenuateVelocity(this, ::sChaseFric);
    MR::reboundVelocityFromCollision(this, cNum_updateChase_reboundVelocityFromCollision_1, cNum_updateChase_reboundVelocityFromCollision_2, cNum_updateChase_reboundVelocityFromCollision_3);
}

void StrayTico::exeCompleteDemo() {
    if (MR::isFirstStep(this)) {
        MR::invalidateHitSensors(this);
        MR::tryStartBck(this, "CompleteDemo");
        MR::offBind(this);
        MR::zeroVelocity(this);
    }

    if (MR::isLessStep(this, ::sCompletePlacementTime)) {
        Vec3f vec;
        MR::rotateVecDegree(&vec, mDemoFront * ::sCompleteStartRadius, mDemoUp, mDemoAngle);
        vec.add(mDemoCenter);
        MR::vecBlend(mDemoStartPos, vec, &mPosition, MR::calcNerveEaseInOutRate(this, ::sCompletePlacementTime));
    } else if (MR::isGreaterStep(this, ::sCompleteRotateStartTime)) {
        MR::rotateVecDegree(&mPosition, mDemoFront * MR::calcNerveValue(this, ::sCompleteRotateStartTime, ::sCompleteDemoTime, ::sCompleteStartRadius, 0.0f),
                            mDemoUp,
                            MR::repeatDegree(mDemoAngle +MR::calcNerveEaseInValue(this, ::sCompleteRotateStartTime, ::sCompleteDemoTime, 0.0f, cNum_exeCompleteDemo_f1)));
        mPosition.add(mDemoCenter);
    }

    Vec3f vec;
    MR::getPlayerFrontVec(&vec);
    MR::turnDirectionDegree(this, &mFrontVec, vec, ::sCompleteDemoRotate);
}

bool StrayTico::isEnableSpin() const {
    if (isNerve(GET_NERVE(StrayTico, StrayTicoNrvWait)) || isNerve(GET_NERVE(StrayTico, StrayTicoNrvGlad)) ||
        isNerve(GET_NERVE(StrayTico, StrayTicoNrvLauncherCamera))) {
        return false;
    }

    return true;
}

bool StrayTico::isRescued() const {
    if (isNerve(GET_NERVE(StrayTico, StrayTicoNrvChase)) || isNerve(GET_NERVE(StrayTico, StrayTicoNrvLauncherCamera))) {
        return true;
    }

    return false;
}

bool StrayTico::isCompleteDemoEnd() const {
    if (isNerve(GET_NERVE(StrayTico, StrayTicoNrvCompleteDemo)) && MR::isGreaterStep(this, ::sCompleteDemoTime)) {
        return true;
    }

    return false;
}

void StrayTico::noticeEnd() {
    if (MR::isValidSwitchA(this)) {
        MR::onSwitchA(this);
    }
}
