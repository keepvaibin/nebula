#include "Game/Boss/BossBegoman.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Boss/BossBegomanHead.hpp"
#include "Game/Enemy/BegomanBaby.hpp"
#include "Game/Enemy/BegomanSpike.hpp"
#include "Game/LiveActor/ActorCameraInfo.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/ElectricRailHolder.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ParabolicPath.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_control_1, "control.1", 1);

namespace {
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hWaitParam, {0.0f, 3.0f, 0.95f, 1.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hSignAttackParam, {0.0f, 3.0f, 0.8f, 3.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hPursueParam, {1.3f, 3.0f, 0.97f, 3.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hTurnParam, {0.4f, 3.0f, 0.95f, 0.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hHitReactionParam, {0.0f, 3.0f, 0.95f, 0.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hOnWeakParam, {-0.5f, 3.0f, 0.95f, 3.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hOnWeakNoMoveParam, {0.0f, 3.0f, 0.95f, 3.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hTrampleReactionParam, {0.0f, 3.0f, 0.95f, 3.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hAwareParam, {0.0f, 3.0f, 0.95f, 0.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hElectricReturnParam, {0.0f, 3.0f, 0.8f, 0.0f});
    NEBULA_TUNABLE(f32, sLaunchRadius, 100.0f);
    NEBULA_TUNABLE(f32, sBabyLaunchVelH, 10.0f);
    NEBULA_TUNABLE(f32, sBabyLaunchVelV, 15.0f);
    NEBULA_TUNABLE(f32, hSpikeLaunchVelH, 10.0f);
    NEBULA_TUNABLE(f32, hSpikeLaunchVelV, 20.0f);
    NEBULA_TUNABLE(f32, hSideCheckOffsetY, 80.0f);
    NEBULA_TUNABLE(f32, hSideCheckDot, 0.25f);
    NEBULA_TUNABLE(s32, sDefaultLife, 3);
    NEBULA_TUNABLE(f32, sWaitRotate, 0.15f);
    NEBULA_TUNABLE(f32, sSignAttackRotate, 0.5f);
    NEBULA_TUNABLE(f32, sPursueRotate, 0.5f);
    NEBULA_TUNABLE(f32, sTurnRotate, 0.4f);
    NEBULA_TUNABLE(f32, sOnWeakRotate, 0.4f);
    const s32 sOnWeakTime = NebulaFrameClock::stepsOfSeconds(7);
    const s32 hOnWeakSignSubTime = NebulaFrameClock::stepsOfSeconds(4);
    const s32 hOnWeakSignFastSubTime = NebulaFrameClock::stepsOfSeconds(6);
    NEBULA_TUNABLE(f32, hSignSlowRate, 0.5f);
    NEBULA_TUNABLE(f32, hSignFastRate, 2.0f);
    const s32 hOnWeakTurnTime = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(f32, hOnWeakSideVel, 0.35f);
    // Inside this distance a weak boss backs away from the player.
    NEBULA_TUNABLE(f32, hOnWeakEscapeDist, 600.0f);
    NEBULA_TUNABLE(f32, sBrakeRotate, 0.2f);
    NEBULA_TUNABLE(f32, sStepBackRotate, 0.2f);
    NEBULA_TUNABLE(f32, sReturnRotate, 0.2f);
    NEBULA_TUNABLE(f32, sKeepDistanceRotate, 0.2f);
    NEBULA_TUNABLE(f32, sProvokeRotate, 0.3f);
    NEBULA_TUNABLE(s32, sTrampleReactionOnSwitchTime, 80);
    NEBULA_TUNABLE(f32, sTrampleReactionRotate, 0.25f);
    // A trample within this many steps of the last one is ignored.
    NEBULA_TUNABLE(s32, sTrampleRepeatGuardTime, 5);
    // A body hit within this many steps of a trample is ignored.
    const s32 sTrampleHitGuardTime = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(f32, hTrampleSlideSpeed, 25.0f);
    const s32 sAwareTime = NebulaFrameClock::stepsOfSeconds(1);
    NEBULA_TUNABLE(f32, sAwareRotate, 0.1f);
    NEBULA_TUNABLE(f32, sHitReactionRotate, 0.25f);
    NEBULA_TUNABLE(s32, sStopSceneTime, 2);
    NEBULA_TUNABLE(s32, sBlowFrame, 20);
    NEBULA_TUNABLE(f32, hBlowVel, 40.0f);
    NEBULA_TUNABLE(f32, hBlowVerticalVel, 40.0f);
    NEBULA_TUNABLE(s32, sDeathStopSceneTime, 5);
    const s32 sElectricDeathTime = NebulaFrameClock::stepsOfSeconds(1);
    const s32 sElectricDeathStarPieceTime = NebulaFrameClock::stepsOfMilliseconds(500);
    // Star pieces burst on the first and second electric hits; the declared count is their sum.
    NEBULA_TUNABLE(s32, sStarPieceFirstHit, 8);
    NEBULA_TUNABLE(s32, sStarPieceSecondHit, 16);
    NEBULA_TUNABLE(f32, hStarPieceHeight, 200.0f);
    NEBULA_TUNABLE(f32, hStarPieceSpeedMin, 20.0f);
    NEBULA_TUNABLE(f32, hStarPieceSpeedMax, 40.0f);
    const s32 sElectricReturnTime = NebulaFrameClock::stepsOfSeconds(1);
    NEBULA_TUNABLE(f32, hElectricReturnSpeed, 25.0f);
    const s32 sJumpToInitPosTime = NebulaFrameClock::stepsOfSeconds(1);
    NEBULA_TUNABLE(f32, sJumpToInitPosRotate, 0.1f);
    NEBULA_TUNABLE(f32, hJumpToInitPosHeight, 700.0f);
    // The rest position sits this far below the placement.
    NEBULA_TUNABLE(f32, hInitPosDrop, 10.0f);
    NEBULA_TUNABLE(f32, hKeepDistFar, 800.0f);
    NEBULA_TUNABLE(f32, hKeepDistNear, 600.0f);
    NEBULA_TUNABLE(f32, hNoCalcWaitTolerance, 0.005f);
    NEBULA_TUNABLE(s32, sEdgeRecoverTime, 55);
    NEBULA_TUNABLE(f32, hRecoverRumbleNear, 800.0f);
    NEBULA_TUNABLE(f32, hRecoverRumbleMiddle, 1200.0f);
    NEBULA_TUNABLE(f32, hRecoverRumbleFar, 2000.0f);
    NEBULA_TUNABLE(f32, hPursueSpeedRateMin, 0.2f);
    NEBULA_TUNABLE(f32, hPursueSpeedRateMax, 1.0f);
    NEBULA_TUNABLE(f32, hPursueSpeedRateStep, 0.001f);
    NEBULA_TUNABLE(f32, hBinderRadius, 160.0f);
    NEBULA_TUNABLE(f32, hBinderOffsetY, 160.0f);
    NEBULA_TUNABLE(f32, hBodyRadius, 190.0f);
    NEBULA_TUNABLE(f32, hBodyRadiusOnWeak, 160.0f);
    NEBULA_TUNABLE(f32, hPushedRailRadius, 250.0f);
    NEBULA_TUNABLE(f32, hTrampleRadius, 180.0f);
    NEBULA_TUNABLE(f32, hShadowRadius, 200.0f);
    // The shadow shrinks by this share of the head edge's retraction.
    NEBULA_TUNABLE(f32, hShadowEdgeShrink, 0.35f);
    NEBULA_TUNABLE(f32, hShadowDropLength, 4000.0f);
    NEBULA_TUNABLE(f32, hEdgeSparkOffset, 180.0f);
    NEBULA_TUNABLE(f32, hPushBackSpeed, 2.0f);
    NEBULA_TUNABLE(f32, hEnemyPushSpeed, 6.0f);
    // Needle sound volume over squared speed 0 .. 11^2.
    NEBULA_TUNABLE(f32, hNeedleSoundSpeedSqMax, 121.0f);
    NEBULA_TUNABLE(f32, hNeedleSoundVolumeMin, 70.0f);
    NEBULA_TUNABLE(f32, hNeedleSoundVolumeMax, 100.0f);
    NEBULA_TUNABLE(s32, sBabyFollowerNum, 3);
    const s32 sSpikeFollowerNum = 2;
    const s32 sStageBgmFadeTime = NebulaFrameClock::stepsOfSeconds(1);
    NEBULA_TUNABLE(s32, sOpeningOverlayTime, 2);

    template < typename T >
    bool isDeadAll(T* const* pList, s32 num) {
        for (s32 i = 0; i < num; i++) {
            if (!MR::isDead(pList[i])) {
                return false;
            }
        }

        return true;
    }

    template < typename T >
    void killAll(T* const* pList, s32 num) {
        for (s32 i = 0; i < num; i++) {
            if (!MR::isDead(pList[i])) {
                MR::emitEffect(pList[i], "Death");
                pList[i]->kill();
            }
        }
    }
};  // namespace

namespace NrvBossBegoman {
    NEW_NERVE_ONEND(HostTypeNrvPreDemoWait, BossBegoman, PreDemoWait, PreDemoWait);
    NEW_NERVE(HostTypeNrvFirstContactDemo, BossBegoman, FirstContactDemo);
    NEW_NERVE(HostTypeNrvReady, BossBegoman, Ready);
    NEW_NERVE_ONEND(HostTypeNrvNoCalcWait, BossBegoman, NoCalcWait, NoCalcWait);
    NEW_NERVE(HostTypeNrvWait, BossBegoman, Wait);
    NEW_NERVE(HostTypeNrvSignAttack, BossBegoman, SignAttack);
    NEW_NERVE(HostTypeNrvPursue, BossBegoman, Pursue);
    NEW_NERVE_ONEND(HostTypeNrvTurn, BossBegoman, Turn, Turn);
    NEW_NERVE_ONEND(HostTypeNrvOnWeak, BossBegoman, OnWeak, OnWeak);
    NEW_NERVE(HostTypeNrvOnWeakTurn, BossBegoman, OnWeak);
    NEW_NERVE(HostTypeNrvBrake, BossBegoman, Brake);
    NEW_NERVE(HostTypeNrvStepBack, BossBegoman, StepBack);
    NEW_NERVE(HostTypeNrvStepBackOnWeak, BossBegoman, StepBack);
    NEW_NERVE(HostTypeNrvReturn, BossBegoman, Return);
    NEW_NERVE(HostTypeNrvProvoke, BossBegoman, Provoke);
    NEW_NERVE(HostTypeNrvTrampleReaction, BossBegoman, TrampleReaction);
    NEW_NERVE(HostTypeNrvAware, BossBegoman, Aware);
    NEW_NERVE(HostTypeNrvHitReaction, BossBegoman, HitReaction);
    NEW_NERVE(HostTypeNrvBlow, BossBegoman, Blow);
    NEW_NERVE(HostTypeNrvElectricDeath, BossBegoman, ElectricDeath);
    NEW_NERVE(HostTypeNrvElectricReturn, BossBegoman, ElectricReturn);
    NEW_NERVE(HostTypeNrvJumpToInitPos, BossBegoman, JumpToInitPos);
    NEW_NERVE(HostTypeNrvKeepDistance, BossBegoman, KeepDistance);
};  // namespace NrvBossBegoman

BossBegoman::BossBegoman(const char* pName)
    : BegomanBase(pName), mBabyFollowers(), mSpikeFollowers(), mBabyFollowerNum(), mSpikeFollowerNum(), mFollowerKind(FollowerKind_BothFollower),
      mPath(), mHead(), mHealth(::sDefaultLife), mPursueSpeedRate(::hPursueSpeedRateMin), mOpeningDemoInfo() {
    mHeadMtx.identity();
}

void BossBegoman::init(const TableIter& rIter) {
    initCore(rIter, "BossBegoman", true);

    MR::calcGravity(this);

    mInitPos.sub(mGravity * ::hInitPosDrop);
    mHead = new BossBegomanHead(this, mHeadMtx);
    mHead->initWithoutIter();

    MR::startBrk(mHead, "OffWait");
    initBinder(::hBinderRadius, ::hBinderOffsetY, false);
    initNerve(GET_NERVE(BossBegoman, HostTypeNrvPreDemoWait));

    f32 scaleY = mScale.y;
    initSensor(1, ::hBodyRadius, ::hPushedRailRadius, "Center");
    MR::addHitSensorAtJoint(this, "trample", "Center", ATYPE_BEGOMAN, 8, ::hTrampleRadius * scaleY, Vec3f(0.0f, 0.0f, 0.0f));

    initEffect(0);
    MR::setEffectHostSRT(this, "EdgeSpark", nullptr, nullptr, nullptr);
    initSound(6, false);

    initShadow(::hShadowRadius, "Center");
    MR::setShadowDropLength(this, nullptr, ::hShadowDropLength);

    MR::declarePowerStar(this);
    MR::declareStarPiece(this, ::sStarPieceFirstHit + ::sStarPieceSecondHit);

    mPath = new ParabolicPath();

    // Placement arg 7 set: the boss is guarded by spikes instead of babies.
    s32 spikeFollower = -1;
    MR::getPlacementArg7NoInit(rIter, &spikeFollower);
    mFollowerKind = spikeFollower == -1 ? FollowerKind_BabyFollower : FollowerKind_SpikeFollower;

    mBabyFollowerNum = ::sBabyFollowerNum;
    mBabyFollowers = new BegomanBaby*[mBabyFollowerNum];

    for (s32 i = 0; i < mBabyFollowerNum; i++) {
        BegomanBaby* baby = new BegomanBaby("子分ベビー");
        mBabyFollowers[i] = baby;
        baby->mHost = this;
        baby->mPosition.set(mPosition);
        baby->initWithoutIter();
        baby->makeActorDead();
    }

    mSpikeFollowerNum = ::sSpikeFollowerNum;
    mSpikeFollowers = new BegomanSpike*[mSpikeFollowerNum];

    for (s32 i = 0; i < mSpikeFollowerNum; i++) {
        BegomanSpike* spike = new BegomanSpike("子分トゲ");
        mSpikeFollowers[i] = spike;
        spike->mPosition.set(mPosition);
        spike->initWithoutIter();
        spike->makeActorDead();
    }

    if (MR::tryRegisterDemoCast(this, rIter)) {
        MR::registerDemoActionNerve(this, GET_NERVE(BossBegoman, HostTypeNrvFirstContactDemo), nullptr);

        for (s32 i = 0; i < mBabyFollowerNum; i++) {
            MR::tryRegisterDemoCast(mBabyFollowers[i], rIter);
        }

        for (s32 i = 0; i < mSpikeFollowerNum; i++) {
            MR::tryRegisterDemoCast(mSpikeFollowers[i], rIter);
        }

        MR::tryRegisterDemoCast(mHead, rIter);
    }

    mOpeningDemoInfo = new ActorCameraInfo(rIter);
    MR::initAnimCamera(this, mOpeningDemoInfo, "OpeningDemo");

    makeActorAppeared();
}

void BossBegoman::appear() {
    BegomanBase::appear();
    MR::emitEffect(this, "Death");
    setNerve(GET_NERVE(BossBegoman, HostTypeNrvWait));
}

void BossBegoman::kill() {
    BegomanBase::kill();
    killAllFollower(FollowerKind_BothFollower);
    MR::startAfterBossBGM();
    MR::requestAppearPowerStar(this, mPosition);
}

bool BossBegoman::isNerveOnWeak() const {
    return isNerve(GET_NERVE(BossBegoman, HostTypeNrvOnWeak)) || isNerve(GET_NERVE(BossBegoman, HostTypeNrvOnWeakTurn));
}

bool BossBegoman::isNerveElectricOrJump() const {
    return isNerve(GET_NERVE(BossBegoman, HostTypeNrvElectricDeath)) || isNerve(GET_NERVE(BossBegoman, HostTypeNrvElectricReturn)) ||
           isNerve(GET_NERVE(BossBegoman, HostTypeNrvJumpToInitPos));
}

bool BossBegoman::isNerveIgnoreAttack() const {
    return isNerve(GET_NERVE(BossBegoman, HostTypeNrvBlow)) || isNerveElectricOrJump();
}

void BossBegoman::control() {
    Vec3f headEdgeScale;
    MR::copyJointScale(mHead, "Edge", &headEdgeScale);
    MR::setShadowVolumeSphereRadius(this, nullptr, ::hShadowRadius * (1.0f - ::hShadowEdgeShrink * (1 - headEdgeScale.x)));

    getSensor("body")->mRadius = isNerveOnWeak() ? ::hBodyRadiusOnWeak : ::hBodyRadius;

    checkTouchElectricRail(!isNerveIgnoreAttack() && !isNerve(GET_NERVE(BossBegoman, HostTypeNrvOnWeakTurn)));

    if (isNerve(GET_NERVE(BossBegoman, HostTypeNrvPursue)) || isNerve(GET_NERVE(BossBegoman, HostTypeNrvTurn))) {
        f32 rate = mPursueSpeedRate + ::hPursueSpeedRateStep;
        mPursueSpeedRate = rate >= ::hPursueSpeedRateMax ? ::hPursueSpeedRateMax : rate;
    } else {
        mPursueSpeedRate = ::hPursueSpeedRateMin;
    }

    if (MR::isStep(this, cStep_control_1)) {
        if (isNerve(GET_NERVE(BossBegoman, HostTypeNrvWait)) || isNerve(GET_NERVE(BossBegoman, HostTypeNrvNoCalcWait))) {
            MR::validateClipping(this);
        } else {
            MR::invalidateClipping(this);
        }
    }

    BegomanBase::control();

    if (!isNerve(GET_NERVE(BossBegoman, HostTypeNrvPreDemoWait)) && !isNerve(GET_NERVE(BossBegoman, HostTypeNrvFirstContactDemo))) {
        startRotationLevelSound();
    }
}

void BossBegoman::setStepBackNerve() {
    if (mHead->isEdgeOut()) {
        setNerve(GET_NERVE(BossBegoman, HostTypeNrvStepBack));
    } else {
        setNerve(GET_NERVE(BossBegoman, HostTypeNrvStepBackOnWeak));
    }
}

bool BossBegoman::onTouchElectric(const Vec3f& rRailPos, const Vec3f& rRailDir) {
    if (isNerveElectricOrJump()) {
        return false;
    }

    if (isNerve(GET_NERVE(BossBegoman, HostTypeNrvBlow)) || isNerve(GET_NERVE(BossBegoman, HostTypeNrvOnWeakTurn))) {
        setNerve(GET_NERVE(BossBegoman, HostTypeNrvElectricDeath));
        return false;
    }

    // A step back only bounces off the rail again once it has landed.
    if ((isNerve(GET_NERVE(BossBegoman, HostTypeNrvStepBack)) || isNerve(GET_NERVE(BossBegoman, HostTypeNrvStepBackOnWeak))) && !MR::isOnGround(this)) {
        return false;
    }

    if (BegomanBase::onTouchElectric(rRailPos, rRailDir)) {
        setStepBackNerve();
        return true;
    }

    return false;
}

bool BossBegoman::setNerveReturn() {
    setNerve(GET_NERVE(BossBegoman, HostTypeNrvReturn));
    return true;
}

const Nerve* BossBegoman::getNerveWait() {
    return GET_NERVE(BossBegoman, HostTypeNrvWait);
}

void BossBegoman::addVelocityOnPushedFromElectricRail(const Vec3f&, const Vec3f&) {
    Vec3f railPushVelH(mInitPos);
    railPushVelH.sub(getSensor("body")->mPosition);
    MR::vecKillElement(railPushVelH, mGravity, &railPushVelH);
    railPushVelH.setLength(getRailPushVelHBoss());

    mVelocity.add(railPushVelH);
    mVelocity.add(mGravity * -getRailPushJumpBoss());
}

bool BossBegoman::requestAttack() {
    return isDeadAllFollower() && BegomanBase::requestAttack();
}

void BossBegoman::exePreDemoWait() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "PreDemoWait");
    }

    MR::startLevelSound(this, "SE_BM_LV_BBEGO_PRE_DEMO_FLY");
    exeNoCalcWaitCore(::hNoCalcWaitTolerance, nullptr);
}

void BossBegoman::endPreDemoWait() {
    finishNoCalcWait();
}

void BossBegoman::exeFirstContactDemo() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "OpeningDemo");
        MR::startAnimCameraTargetSelf(this, mOpeningDemoInfo, "OpeningDemo", 0, 1.0f);

        MR::showModel(this);
        MR::showModel(mHead);

        mHead->setOpeningDemo();
        mVelocity.zero();

        MR::stopStageBGM(::sStageBgmFadeTime);
        MR::overlayWithPreviousScreen(::sOpeningOverlayTime);
    }

    if (MR::isActionEnd(this)) {
        setNerve(GET_NERVE(BossBegoman, HostTypeNrvReady));
    }
}

void BossBegoman::exeReady() {
    if (MR::isFirstStep(this)) {
        MR::startBossBGM(MR::BossBgmID_Begoman);
        mVelocity.zero();
    }

    edgeRecoverCore();

    if (MR::isActionEnd(this) && MR::isAnimCameraEnd(this, mOpeningDemoInfo, "OpeningDemo")) {
        MR::endAnimCamera(this, mOpeningDemoInfo, "OpeningDemo", -1, true);
        tryLaunchFollower();
        setNerve(GET_NERVE(BossBegoman, HostTypeNrvWait));
    }
}

void BossBegoman::exeNoCalcWait() {
    if (MR::isFirstStep(this)) {
        tryLaunchFollower();
    }

    exeNoCalcWaitCore(::hNoCalcWaitTolerance, GET_NERVE(BossBegoman, HostTypeNrvWait));
}

void BossBegoman::endNoCalcWait() {
    finishNoCalcWait();
}

void BossBegoman::exeWait() {
    updateRotateY(::sWaitRotate);
    exeWaitCore(::hWaitParam, GET_NERVE(BossBegoman, HostTypeNrvSignAttack), GET_NERVE(BossBegoman, HostTypeNrvKeepDistance),
                GET_NERVE(BossBegoman, HostTypeNrvNoCalcWait));
}

void BossBegoman::exeSignAttack() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Attack");
        MR::startSound(this, "SE_BM_BBEGO_PRE_PURSUE");
    }

    updateRotateY(::sSignAttackRotate);
    exeSignAttackCore(::hSignAttackParam, GET_NERVE(BossBegoman, HostTypeNrvPursue));
}

void BossBegoman::exePursue() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_BM_BBEGO_PURSUE_START");
    }

    updateRotateY(::sPursueRotate);
    exePursueCore(::hPursueParam, GET_NERVE(BossBegoman, HostTypeNrvBrake), GET_NERVE(BossBegoman, HostTypeNrvTurn), *getSoundBoss(), mPursueSpeedRate);
}

void BossBegoman::exeTurn() {
    if (MR::isFirstStep(this)) {
        mHead->tryTurn();
    }

    if (MR::isBckPlaying(this, "Turn")) {
        MR::emitEffectHit(this, mPosition + mFaceVec * ::hEdgeSparkOffset, "EdgeSpark");
    }

    updateRotateY(::sTurnRotate);
    MR::startLevelSound(this, "SE_BM_LV_BBEGO_TURN");
    exeTurnCore(::hTurnParam, GET_NERVE(BossBegoman, HostTypeNrvBrake), GET_NERVE(BossBegoman, HostTypeNrvPursue), false);
}

void BossBegoman::endTurn() {
    mHead->tryTurnEnd();
}

void BossBegoman::exeOnWeak() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Shake");
        MR::startBrk(mHead, "OnWait");
    }

    updateRotateY(::sOnWeakRotate);

    if (MR::calcDistanceToPlayer(mPosition) < ::hOnWeakEscapeDist) {
        MR::moveAndTurnToPlayer(this, &mFaceVec, ::hOnWeakParam);
        addVelocityEscapeToSide(::hOnWeakSideVel);
    } else {
        MR::moveAndTurnToPlayer(this, &mFaceVec, ::hOnWeakNoMoveParam);
    }

    if (isNerve(GET_NERVE(BossBegoman, HostTypeNrvOnWeakTurn))) {
        if (MR::isGreaterStep(this, ::hOnWeakTurnTime)) {
            setNerve(GET_NERVE(BossBegoman, HostTypeNrvOnWeak));
        }

        return;
    }

    if (MR::isGreaterStep(this, ::sOnWeakTime)) {
        setNerve(GET_NERVE(BossBegoman, HostTypeNrvAware));
    } else if (MR::isStep(this, ::hOnWeakSignSubTime)) {
        MR::startBrk(mHead, "SignWait");
        MR::setBrkRate(mHead, ::hSignSlowRate);
    } else if (MR::isStep(this, ::hOnWeakSignFastSubTime)) {
        MR::startBrk(mHead, "SignWait");
        MR::setBrkRate(mHead, ::hSignFastRate);
    }

    if (MR::isGreaterEqualStep(this, ::hOnWeakSignFastSubTime)) {
        MR::startLevelSound(this, "SE_BM_LV_BBEGO_ALARM_FAST");
    } else if (MR::isGreaterEqualStep(this, ::hOnWeakSignSubTime)) {
        MR::startLevelSound(this, "SE_BM_LV_BBEGO_ALARM");
    }
}

void BossBegoman::endOnWeak() {
    MR::startBrk(mHead, "OffWait");
}

void BossBegoman::exeBrake() {
    updateRotateY(::sBrakeRotate);
    MR::startLevelSound(this, "SE_EM_LV_BEGOMAN_SPARK");
    exeBrakeCore(GET_NERVE(BossBegoman, HostTypeNrvTurn));
}

void BossBegoman::exeStepBack() {
    updateRotateY(::sStepBackRotate);

    if (isNerve(GET_NERVE(BossBegoman, HostTypeNrvStepBackOnWeak))) {
        exeStepBackCore(::hWaitParam, GET_NERVE(BossBegoman, HostTypeNrvOnWeak));
    } else {
        exeStepBackCore(::hWaitParam, GET_NERVE(BossBegoman, HostTypeNrvWait));
    }
}

void BossBegoman::exeReturn() {
    updateRotateY(::sReturnRotate);
    exeReturnCore(GET_NERVE(BossBegoman, HostTypeNrvWait));
}

void BossBegoman::exeProvoke() {
    updateRotateY(::sProvokeRotate);
    exeProvokeCore(::hWaitParam, GET_NERVE(BossBegoman, HostTypeNrvSignAttack));
}

void BossBegoman::exeTrampleReaction() {
    if (MR::isFirstStep(this)) {
        mHead->trySwitchPushTrample();

        if (!mHead->isSwitchOn()) {
            MR::startAction(this, "TrampleReaction");
            MR::startSound(this, "SE_BM_BBEGO_NEEDLE_ON");
        } else {
            MR::startAction(this, "HopEnd");
            MR::startSound(this, "SE_BM_BBEGO_STOMPED");
            MR::startSound(this, "SE_BM_BBEGO_NEEDLE_OFF");
        }
    }

    updateRotateY(::sTrampleReactionRotate);
    MR::moveAndTurnToPlayer(this, &mFaceVec, ::hTrampleReactionParam);

    // Switch on: the needles are down and the boss is weak.
    if (MR::isGreaterStep(this, ::sTrampleReactionOnSwitchTime)) {
        if (mHead->isSwitchOn()) {
            setNerve(GET_NERVE(BossBegoman, HostTypeNrvOnWeak));
        } else {
            setNerve(GET_NERVE(BossBegoman, HostTypeNrvSignAttack));
        }
    }
}

void BossBegoman::exeAware() {
    if (MR::isFirstStep(this)) {
        mVelocity.zero();
    }

    updateRotateY(::sAwareRotate);
    MR::moveAndTurnToPlayer(this, &mFaceVec, ::hAwareParam);
    edgeRecoverCore();

    if (MR::isGreaterStep(this, ::sAwareTime)) {
        tryLaunchFollower();
        setNerve(GET_NERVE(BossBegoman, HostTypeNrvSignAttack));
    }
}

void BossBegoman::exeHitReaction() {
    updateRotateY(::sHitReactionRotate);
    exeHitReactionCore(::hHitReactionParam, GET_NERVE(BossBegoman, HostTypeNrvProvoke));
}

void BossBegoman::exeBlow() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_EM_BEGOMAN_ROT_STOP");
        MR::startAction(this, "Damage");
        MR::stopScene(::sStopSceneTime);
    }

    MR::startLevelSound(this, "SE_EM_LV_BEGOMAN_SPARK");

    MR::moveAndTurnToDirection(this, &mFaceVec, mTargetVec, ::hHitReactionParam);
    reboundWallAndGround(&mFaceVec, false);

    if (MR::isGreaterStep(this, ::sBlowFrame) && MR::isOnGround(this)) {
        MR::startAction(this, "Turn");

        if (mHead->isSwitchOn()) {
            setNerve(GET_NERVE(BossBegoman, HostTypeNrvOnWeakTurn));
        } else {
            setNerve(GET_NERVE(BossBegoman, HostTypeNrvTurn));
        }
    }
}

void BossBegoman::exeElectricDeath() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "electricshock");
        MR::startBrk(mHead, "Damage");
        mVelocity.zero();

        getSensor("body")->invalidate();
        MR::startSound(this, "SE_EM_BEGOMAN_ELEC_DAMAGE");
        MR::invalidateClipping(this);
        mHealth--;
    }

    if (MR::isStep(this, ::sElectricDeathStarPieceTime)) {
        s32 starPieceNum = 0;

        if (mHealth == 2) {
            starPieceNum = ::sStarPieceFirstHit;
        } else if (mHealth == 1) {
            starPieceNum = ::sStarPieceSecondHit;
        }

        if (starPieceNum > 0) {
            // Up and toward the rest position.
            Vec3f dir(mInitPos);
            dir.sub(mPosition);
            MR::vecKillElement(dir, mGravity, &dir);
            MR::normalizeOrZero(&dir);
            dir.sub(mGravity);
            MR::normalizeOrZero(&dir);

            MR::appearStarPieceToDirection(this, mPosition - mGravity * ::hStarPieceHeight, dir, starPieceNum, ::hStarPieceSpeedMin, ::hStarPieceSpeedMax,
                                           false);
            MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
        }
    }

    if (MR::isGreaterStep(this, ::sElectricDeathTime)) {
        MR::stopScene(::sDeathStopSceneTime);
        MR::shakeCameraWeak();

        if (mHealth == 0) {
            MR::startSound(this, "SE_BM_BBEGO_DEAD");
            kill();
            MR::emitEffect(this, "Death");
        } else {
            // The last life is guarded by spikes.
            if (mHealth == 1) {
                killAllFollower(FollowerKind_SpikeFollower);
                mFollowerKind = FollowerKind_SpikeFollower;
            }

            setNerve(GET_NERVE(BossBegoman, HostTypeNrvJumpToInitPos));
        }

        getSensor("body")->validate();
        MR::validateClipping(this);
    }
}

void BossBegoman::recoverEdge() {
    MR::startSound(this, "SE_BM_BBEGO_NEEDLE_ON");
    mHead->tryForceRecover();
    MR::tryRumblePadAndCameraDistanceMiddle(this, ::hRecoverRumbleNear, ::hRecoverRumbleMiddle, ::hRecoverRumbleFar);
}

void BossBegoman::edgeRecoverCore() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Recover");
    }

    if (MR::isStep(this, ::sEdgeRecoverTime)) {
        recoverEdge();
    }
}

void BossBegoman::exeElectricReturn() {
    if (MR::isFirstStep(this)) {
        Vec3f toRail;
        Vec3f railDir;

        if (ElectricRailFunction::isTouchRail(getSensor("check"), &toRail, &railDir)) {
            toRail.sub(mPosition);

            // The rail's in-plane normal, turned to point away from the rail.
            Vec3f railNormal = toRail;
            railNormal.cross(railDir, railNormal);

            Vec3f away = railNormal;
            away.cross(railDir, away);

            if (0.0f < toRail.dot(away)) {
                away = -away;
            }

            away.setLength(::hElectricReturnSpeed);
            mVelocity.set(away);
        }
    }

    reboundWallAndGround(&mFaceVec, false);
    MR::moveAndTurnToPlayer(this, &mFaceVec, ::hElectricReturnParam);

    if (MR::isGreaterStep(this, ::sElectricReturnTime)) {
        mFaceVec.set(mTargetVec);
        setNerve(GET_NERVE(BossBegoman, HostTypeNrvJumpToInitPos));
    }
}

void BossBegoman::exeJumpToInitPos() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Shake");
        MR::startSound(this, "SE_BM_BBEGO_BIG_JUMP");

        Vec3f up(mGravity);
        up = -up;

        mPath->initFromUpVector(mPosition, mInitPos - mGravity * ::hInitPosDrop, up, ::hJumpToInitPosHeight);
        mVelocity.zero();
    }

    updateRotateY(::sJumpToInitPosRotate);
    mPath->calcPosition(&mPosition, MR::calcNerveRate(this, ::sJumpToInitPosTime));

    if (MR::isGreaterStep(this, ::sJumpToInitPosTime)) {
        setNerve(GET_NERVE(BossBegoman, HostTypeNrvSignAttack));
        tryLaunchFollower();
        recoverEdge();
    }
}

void BossBegoman::exeKeepDistance() {
    updateRotateY(::sKeepDistanceRotate);
    exeKeepDistanceCore(GET_NERVE(BossBegoman, HostTypeNrvWait), GET_NERVE(BossBegoman, HostTypeNrvSignAttack),
                        GET_NERVE(BossBegoman, HostTypeNrvBrake), ::hKeepDistFar, ::hKeepDistNear);
}

void BossBegoman::tryLaunchFollower() {
    if (!isDeadAllFollower()) {
        return;
    }

    Vec3f toPlayer(*MR::getPlayerCenterPos());
    toPlayer.sub(mPosition);
    MR::vecKillElement(toPlayer, mGravity, &toPlayer);
    MR::normalizeOrZero(&toPlayer);

    if (MR::isNearZero(toPlayer)) {
        toPlayer.set(mFaceVec);
    }

    if (mFollowerKind == FollowerKind_BabyFollower) {
        launchBegomanBabyFromGuarder(this, mBabyFollowers, mBabyFollowerNum, ::sLaunchRadius, ::sBabyLaunchVelH, ::sBabyLaunchVelV, &toPlayer);
    } else if (mFollowerKind == FollowerKind_SpikeFollower) {
        BegomanBase* spikes[::sSpikeFollowerNum];

        for (s32 i = 0; i < mSpikeFollowerNum; i++) {
            spikes[i] = mSpikeFollowers[i];
        }

        launchBegoman(this, spikes, mSpikeFollowerNum, ::sLaunchRadius, ::hSpikeLaunchVelH, ::hSpikeLaunchVelV, &toPlayer);
    }
}

void BossBegoman::killAllFollower(FollowerKind kind) {
    if (kind == FollowerKind_BabyFollower || kind == FollowerKind_BothFollower) {
        killAll(mBabyFollowers, mBabyFollowerNum);
    }

    if (kind == FollowerKind_SpikeFollower || kind == FollowerKind_BothFollower) {
        killAll(mSpikeFollowers, mSpikeFollowerNum);
    }
}

bool BossBegoman::isDeadAllFollower() {
    if (mFollowerKind == FollowerKind_BabyFollower) {
        return isDeadAll(mBabyFollowers, mBabyFollowerNum);
    }

    return isDeadAll(mSpikeFollowers, mSpikeFollowerNum);
}

void BossBegoman::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (pSender != getSensor("body")) {
        return;
    }

    if (MR::isSensorEnemy(pReceiver)) {
        MR::sendMsgEnemyAttack(pReceiver, pSender);
        return;
    }

    if (!MR::isSensorPlayer(pReceiver)) {
        return;
    }

    if (isNerve(GET_NERVE(BossBegoman, HostTypeNrvBlow)) || isNerve(GET_NERVE(BossBegoman, HostTypeNrvElectricDeath)) ||
        isNerve(GET_NERVE(BossBegoman, HostTypeNrvElectricReturn)) || isNerve(GET_NERVE(BossBegoman, HostTypeNrvAware))) {
        MR::sendMsgPush(pReceiver, pSender);
        return;
    }

    if (isNerve(GET_NERVE(BossBegoman, HostTypeNrvJumpToInitPos))) {
        if (!MR::sendMsgEnemyAttackFlipRot(pReceiver, pSender)) {
            MR::sendMsgPush(pReceiver, pSender);
        }

        return;
    }

    if (MR::isPlayerSwingAction() || !MR::isOnGroundPlayer()) {
        return;
    }

    // Only the side edge burns; the face flips the player away.
    if (!mHead->isEdgeOut() || !MR::isPlayerExistSide(this, ::hSideCheckOffsetY, ::hSideCheckDot)) {
        MR::sendMsgEnemyAttackFlipRot(pReceiver, pSender);
        return;
    }

    if (!MR::sendMsgEnemyAttackFire(pReceiver, pSender)) {
        MR::sendMsgPushAndKillVelocityToTarget(this, pReceiver, pSender);
        return;
    }

    Vec3f awayFromPlayer(pSender->mPosition);
    awayFromPlayer.sub(pReceiver->mPosition);
    MR::normalizeOrZero(&awayFromPlayer);

    if (!MR::isNearZero(awayFromPlayer)) {
        bool rebounded = reboundPlaneWithEffect(awayFromPlayer, 0.0f, 0.0f, "Spark");
        mVelocity.add(awayFromPlayer * ::hPushBackSpeed);

        if (rebounded) {
            MR::startSound(this, "SE_EM_BEGOMAN_COLLI");
        }
    }

    setNerve(GET_NERVE(BossBegoman, HostTypeNrvHitReaction));
}

bool BossBegoman::receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (pReceiver != getSensor("body")) {
        return false;
    }

    if (MR::isMsgEnemyAttackElectric(msg)) {
        Vec3f railDir(pSender->mHost->mVelocity);
        MR::vecKillElement(railDir, mGravity, &railDir);
        MR::normalizeOrZero(&railDir);

        return onTouchElectric(pSender->mPosition, railDir);
    }

    if (isNerveIgnoreAttack() || !MR::isSensorEnemy(pSender)) {
        return false;
    }

    Vec3f awayFromSender(pReceiver->mPosition);
    awayFromSender.sub(pSender->mPosition);
    MR::normalizeOrZero(&awayFromSender);

    if (MR::isNearZero(awayFromSender)) {
        return false;
    }

    bool rebounded = reboundPlaneWithEffect(awayFromSender, 0.0f, 0.0f, "Spark");
    HitSensor* pBody = getSensor("body");
    // Larger senders push harder.
    MR::addVelocityLimit(this, awayFromSender * ::hEnemyPushSpeed * (pSender->mRadius / pBody->mRadius));

    if (rebounded) {
        MR::startSound(this, "SE_EM_BEGOMAN_COLLI_BEGOMAN");
    }

    return true;
}

bool BossBegoman::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgStarPieceReflect(msg)) {
        return true;
    }

    if (MR::isMsgPlayerTrample(msg) && pReceiver == getSensor("trample")) {
        return receiveMsgTrample(pSender, pReceiver);
    }

    if (isNerveIgnoreAttack()) {
        return false;
    }

    if (MR::isMsgPlayerHipDrop(msg) && pReceiver == getSensor("trample")) {
        MR::sendMsgAwayJump(pSender, pReceiver);
        setNerve(GET_NERVE(BossBegoman, HostTypeNrvTrampleReaction));
        return true;
    }

    if (pReceiver != getSensor("body")) {
        return false;
    }

    if (MR::isMsgPlayerSpinAttack(msg) && !mHead->isSwitchOn() && MR::isPlayerExistSide(this, ::hSideCheckOffsetY, ::hSideCheckDot)) {
        MR::sendMsgEnemyAttackFire(pSender, pReceiver);
        setNerve(GET_NERVE(BossBegoman, HostTypeNrvProvoke));
        return false;
    }

    if (isNerve(GET_NERVE(BossBegoman, HostTypeNrvTrampleReaction)) && !MR::isGreaterStep(this, ::sTrampleHitGuardTime)) {
        return false;
    }

    if (!mHead->isEdgeOut() && MR::isMsgPlayerHitAll(msg)) {
        calcBlowReaction(pSender->mPosition, pReceiver->mPosition, ::hBlowVel, ::hBlowVerticalVel);
        setNerve(GET_NERVE(BossBegoman, HostTypeNrvBlow));
        return true;
    }

    return false;
}

bool BossBegoman::receiveMsgTrample(HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerveIgnoreAttack()) {
        return false;
    }

    if (MR::isPlayerDamaging() || MR::isPlayerJumpRising()) {
        return false;
    }

    if (isNerve(GET_NERVE(BossBegoman, HostTypeNrvTrampleReaction)) && MR::isLessStep(this, ::sTrampleRepeatGuardTime)) {
        return false;
    }

    if (pReceiver == getSensor("body")) {
        return false;
    }

    if (pReceiver != getSensor("trample")) {
        return true;
    }

    setNerve(GET_NERVE(BossBegoman, HostTypeNrvTrampleReaction));

    Vec3f awayFromPlayer(pReceiver->mPosition);
    awayFromPlayer.sub(pSender->mPosition);
    MR::vecKillElement(awayFromPlayer, mGravity, &awayFromPlayer);
    MR::normalizeOrZero(&awayFromPlayer);

    // Straight above the boss there is no side to face; keep the facing.
    if (!MR::isNearZero(awayFromPlayer)) {
        mFaceVec.set(awayFromPlayer);
    }

    // Slide sideways to the player's motion, or to the player's facing when standing.
    Vec3f slide(*MR::getPlayerVelocity());
    MR::vecKillElement(slide, mGravity, &slide);

    if (MR::isNearZero(slide)) {
        MR::getPlayerFrontVec(&slide);
    }

    slide.cross(mGravity, slide);
    slide.setLength(::hTrampleSlideSpeed);
    mVelocity = slide;

    return true;
}

bool BossBegoman::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    return !isNerveIgnoreAttack() && MR::isMsgHitmarkEmit(msg);
}

void BossBegoman::calcAnim() {
    LiveActor::calcAnim();
    mHeadMtx.setInline(MR::getJointMtx(this, "Center"));

    Vec3f up;
    mHeadMtx.getYDir(up);

    if (!MR::isSameDirection(up, mTargetVec)) {
        MR::makeMtxUpFront(&mHeadMtx, up, mTargetVec);
    }
}

void BossBegoman::startRotationLevelSound() {
    if (isNerve(GET_NERVE(BossBegoman, HostTypeNrvWait))) {
        MR::startLevelSound(this, "SE_BM_LV_BBEGO_ROT_SLOW");
    } else if (isNerve(GET_NERVE(BossBegoman, HostTypeNrvPursue))) {
        MR::startLevelSound(this, "SE_BM_LV_BBEGO_PURSUE");
    } else if (isNerve(GET_NERVE(BossBegoman, HostTypeNrvTrampleReaction)) || isNerve(GET_NERVE(BossBegoman, HostTypeNrvOnWeak))) {
        MR::startLevelSound(this, "SE_BM_LV_BBEGO_ROT_WEAK");
    } else {
        MR::startLevelSound(this, "SE_BM_LV_BBEGO_ROT_MIDDLE");
    }

    if (!mHead->isSwitchOn()) {
        f32 speed = mVelocity.length();
        MR::startLevelSound(this, "SE_BM_LV_BBEGO_ROT_NEEDLE",
                            MR::getLinerValueFromMinMax(speed * speed, 0.0f, ::hNeedleSoundSpeedSqMax, ::hNeedleSoundVolumeMin, ::hNeedleSoundVolumeMax));
    }
}
