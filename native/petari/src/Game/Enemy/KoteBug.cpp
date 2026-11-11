#include "Game/Enemy/KoteBug.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/Enemy/AnimStampController.hpp"
#include "Game/Enemy/SpinHitController.hpp"
#include "Game/Enemy/WalkerStateBindStarPointer.hpp"
#include "Game/LiveActor/Binder.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/JointController.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 110.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f6, "init.f6", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f7, "init.f7", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f1, "control.f1", 3.1415927f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeOverturnFall_f1, "exeOverturnFall.f1", 0.75f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveMsgPlayerAttack_f1, "receiveMsgPlayerAttack.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveOtherMsg_f1, "receiveOtherMsg.f1", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_setOverturnFall_f1, "setOverturnFall.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_attackSensor_f1, "attackSensor.f1", 0.25f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_applyVelocityDampAndGravity_1, "exeWait.applyVelocityDampAndGravity.1", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSearch_applyVelocityDampAndGravity_1, "exeSearch.applyVelocityDampAndGravity.1", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeEscapeSearch_applyVelocityDampAndGravity_1, "exeEscapeSearch.applyVelocityDampAndGravity.1", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePreEscape_applyVelocityDampAndGravity_1, "exePreEscape.applyVelocityDampAndGravity.1", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeFlyStart_applyVelocityDampAndGravity_1, "exeFlyStart.applyVelocityDampAndGravity.1", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeFlyPursue_applyVelocityDampAndGravity_1, "exeFlyPursue.applyVelocityDampAndGravity.1", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeFlyPursue_addVelocityKeepHeightUseShadow_1, "exeFlyPursue.addVelocityKeepHeightUseShadow.1", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeFlyPursueLast_applyVelocityDampAndGravity_1, "exeFlyPursueLast.applyVelocityDampAndGravity.1", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeFlyPursueLast_addVelocityKeepHeightUseShadow_1, "exeFlyPursueLast.addVelocityKeepHeightUseShadow.1", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeOverturnFall_applyVelocityDampAndGravity_1, "exeOverturnFall.applyVelocityDampAndGravity.1", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeOverturn_applyVelocityDampAndGravity_1, "exeOverturn.applyVelocityDampAndGravity.1", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePreRecover_reboundVelocityFromCollision_1, "exePreRecover.reboundVelocityFromCollision.1", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePreRecover_reboundVelocityFromCollision_2, "exePreRecover.reboundVelocityFromCollision.2", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePreRecover_reboundVelocityFromCollision_3, "exePreRecover.reboundVelocityFromCollision.3", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePreRecover_applyVelocityDampAndGravity_1, "exePreRecover.applyVelocityDampAndGravity.1", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRecover_reboundVelocityFromCollision_1, "exeRecover.reboundVelocityFromCollision.1", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRecover_reboundVelocityFromCollision_2, "exeRecover.reboundVelocityFromCollision.2", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRecover_reboundVelocityFromCollision_3, "exeRecover.reboundVelocityFromCollision.3", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRecover_applyVelocityDampAndGravity_1, "exeRecover.applyVelocityDampAndGravity.1", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHitToPlayer_reboundVelocityFromCollision_1, "exeHitToPlayer.reboundVelocityFromCollision.1", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHitToPlayer_reboundVelocityFromCollision_2, "exeHitToPlayer.reboundVelocityFromCollision.2", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHitToPlayer_reboundVelocityFromCollision_3, "exeHitToPlayer.reboundVelocityFromCollision.3", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHitToPlayer_applyVelocityDampAndGravity_1, "exeHitToPlayer.applyVelocityDampAndGravity.1", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeStampDeath_setBinderRadius_1, "exeStampDeath.setBinderRadius.1", 10.0f);

namespace {
    NEBULA_TUNABLE(f32, hCommonGravity, 1.5f);
    NEBULA_TUNABLE(f32, hGroundDamp, 0.8f);
    NEBULA_TUNABLE(f32, hAirDampH, 0.96f);
    NEBULA_TUNABLE(f32, hAirDampV, 0.93f);
    NEBULA_TUNABLE(f32, hPushedAcc, 2.0f);
    NEBULA_TUNABLE(f32, hSearchDistance, 1000.0f);
    NEBULA_TUNABLE(f32, hFlyDistance, 2000.0f);
    NEBULA_TUNABLE(f32, hFlyPursueVel, 0.2f);
    NEBULA_TUNABLE(f32, hFlyPursueLastSubVel, 0.01f);
    NEBULA_TUNABLE(f32, hFlyHeight, 100.0f);
    NEBULA_TUNABLE(s32, hFlyStartTime, 20);
    NEBULA_TUNABLE(s32, hFlyPursueTime, 200);
    NEBULA_TUNABLE(f32, hKeepAcc, 1.8f);
    NEBULA_TUNABLE(f32, hFreqRange, 40.0f);
    NEBULA_TUNABLE(f32, hOverturnFallJumpVelV, 30.0f);
    const s32 hOverturnTime = NebulaFrameClock::stepsOfSeconds(4);
    NEBULA_TUNABLE(f32, hRecoverJumpVelV, 24.0f);
    NEBULA_TUNABLE(f32, hDistNoCalcWait, 1000.0f);
    NEBULA_TUNABLE(s32, hSeachTime, 50);
    NEBULA_TUNABLE(s32, hEscapeSeachTime, 20);
    NEBULA_TUNABLE(f32, hPreEscapeTurnLimitDegree, 9.0f);
    const s32 hFlyPursueLastTime = NebulaFrameClock::stepsOfMilliseconds(1500);
    NEBULA_TUNABLE(f32, hFlyJitabataHeightBase, 60.0f);
    NEBULA_TUNABLE(f32, hFlyJitabataHeight, 150.0f);
    NEBULA_TUNABLE(f32, hJitabataPostureRate, 0.02f);
    NEBULA_TUNABLE(f32, hJitabataAngle, -1.8f);
    NEBULA_TUNABLE(f32, hFlyPursueLastAddRate, 1.0f);
    NEBULA_TUNABLE(f32, hHitToPlayerJumpVelV, 10.0f);
    NEBULA_TUNABLE(f32, hHitToPlayerJumpVelH, -10.0f);
    NEBULA_TUNABLE(f32, hHitScaleYSpeed, 0.1f);
    NEBULA_TUNABLE(s32, hStampDeathTime, 20);
    NEBULA_TUNABLE(s32, hBlowStopSceneTime, 3);
    NEBULA_TUNABLE(f32, hSearchTurnLimitDegree, 5.7f);
    NEBULA_TUNABLE(f32, hFlyTurnLimitDegree, 5.7f);
    NEBULA_TUNABLE(f32, hFlyEscapeTurnLimitDegree, 11.5f);
    NEBULA_TUNABLE(f32, hOverturnRotateRate, 0.15f);
    NEBULA_TUNABLE(f32, hRecoverRotateRate, 0.2f);
    NEBULA_TUNABLE(f32, hBlowRotateAngle, -1.0f);
};  // namespace

namespace NrvKoteBug {
    NEW_NERVE_ONEND(HostTypeNrvNoCalcWait, KoteBug, NoCalcWait, NoCalcWait);
    NEW_NERVE_ONEND(HostTypeNrvWait, KoteBug, Wait, Wait);
    NEW_NERVE(HostTypeNrvSearch, KoteBug, Search);
    NEW_NERVE(HostTypeNrvEscapeSearch, KoteBug, EscapeSearch);
    NEW_NERVE(HostTypeNrvPreEscape, KoteBug, PreEscape);
    NEW_NERVE(HostTypeNrvFlyStart, KoteBug, FlyStart);
    NEW_NERVE(HostTypeNrvFlyPursue, KoteBug, FlyPursue);
    NEW_NERVE(HostTypeNrvFlyEscape, KoteBug, FlyPursue);
    NEW_NERVE(HostTypeNrvFlyPursueLast, KoteBug, FlyPursueLast);
    NEW_NERVE(HostTypeNrvOverturnFall, KoteBug, OverturnFall);
    NEW_NERVE(HostTypeNrvOverturn, KoteBug, Overturn);
    NEW_NERVE(HostTypeNrvPreRecover, KoteBug, PreRecover);
    NEW_NERVE(HostTypeNrvRecover, KoteBug, Recover);
    NEW_NERVE(HostTypeNrvHitToPlayer, KoteBug, HitToPlayer);
    NEW_NERVE(HostTypeNrvBlow, KoteBug, Blow);
    NEW_NERVE(HostTypeNrvStampDeath, KoteBug, StampDeath);
    NEW_NERVE_ONEND(HostTypeNrvBindStarPointer, KoteBug, BindStarPointer, BindStarPointer);
};  // namespace NrvKoteBug

KoteBug::KoteBug(const char* pName)
    : LiveActor(pName), mScaleController(), mStampController(), mSpinHitController(), mStarPointer(), mFront(0, 0, 1), mPosture(0, 0, 0, 1),
      mJointController() {
    lumacore::setClass(this, lumacore::kClassId< KoteBug >);
}

void KoteBug::init(const TableIter& rIter) {
    if (MR::isValidInfo(rIter)) {
        MR::initDefaultPos(this, rIter);
        MR::useStageSwitchWriteDead(this, rIter);
        if (MR::useStageSwitchReadAppear(this, rIter)) {
            MR::syncStageSwitchAppear(this);
        }
    }

    initModelManagerWithAnm("KoteBug", nullptr, false);
    Pos3f mtx;
    mtx.set(getBaseMtx());
    mtx.getZDir(mFront);

    MR::connectToSceneEnemy(this);
    MR::initLightCtrl(this);
    f32 scale = mScale.y;
    initBinder(cNum_init_f1 * scale, cNum_init_f2 * scale, 0);
    initHitSensor(1);
    MR::addHitSensorAtJointEnemy(this, "body", "Center", 32, cNum_init_f3 * scale, Vec3f(0.0f, cNum_init_f4 * scale, 0.0f));
    initEffectKeeper(0, nullptr);
    initSound(4, false);
    initNerve(GET_NERVE(KoteBug, HostTypeNrvWait));
    MR::initStarPointerTarget(this, cNum_init_f5, Vec3f(0.0f, cNum_init_f6, 0.0f));
    MR::declareStarPiece(this, 3);
    MR::declareCoin(this, 1);
    mSpinHitController = new SpinHitController(this, 3, false);
    mScaleController = new AnimScaleController(nullptr);
    mStampController = new AnimStampController();
    mStarPointer = new WalkerStateBindStarPointer(this, mScaleController);
    MR::onCalcGravity(this);
    MR::initShadowVolumeSphere(this, cNum_init_f7 * mScale.y);
    MR::validateClipping(this);
    mJointController = MR::createJointDelegatorWithNullChildFunc(this, &KoteBug::calcJoint, "Center");
    if (MR::isValidSwitchAppear(this)) {
        makeActorDead();
    } else {
        makeActorAppeared();
    }
}

void KoteBug::appear() {
    LiveActor::appear();
    MR::emitEffect(this, "Appear");
}

void KoteBug::kill() {
    LiveActor::kill();
    MR::emitEffect(this, "Death");
    if (MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
    }
}

void KoteBug::control() {
    if (MR::isInDeath(this, Vec3f(0.0f, 0.0f, 0.0f)) || MR::isInWater(mPosition)) {
        kill();
        return;
    }

    mScaleController->updateNerve();
    mStampController->updateNerve();
    if (isNerve(GET_NERVE(KoteBug, HostTypeNrvOverturn)) || isNerve(GET_NERVE(KoteBug, HostTypeNrvOverturnFall)) ||
        isNerve(GET_NERVE(KoteBug, HostTypeNrvPreRecover)) || isNerve(GET_NERVE(KoteBug, HostTypeNrvStampDeath))) {
        Quat4f rotation;
        rotation.setRotate(Vec3f(1, 0, 0), cNum_control_f1);
        mPosture.slerp(rotation, ::hOverturnRotateRate);
    } else if (isNerve(GET_NERVE(KoteBug, HostTypeNrvFlyPursueLast))) {
        Quat4f rotation;
        rotation.setRotate(Vec3f(1, 0, 0), ::hJitabataAngle);
        mPosture.slerp(rotation, ::hJitabataPostureRate);
    } else {
        mPosture.slerp(Quat4f(0, 0, 0, 1), ::hRecoverRotateRate);
    }

    if (isNerve(GET_NERVE(KoteBug, HostTypeNrvNoCalcWait)) || isNerve(GET_NERVE(KoteBug, HostTypeNrvWait)) ||
        isNerve(GET_NERVE(KoteBug, HostTypeNrvSearch)) || isNerve(GET_NERVE(KoteBug, HostTypeNrvEscapeSearch)) ||
        isNerve(GET_NERVE(KoteBug, HostTypeNrvPreEscape)) || isNerve(GET_NERVE(KoteBug, HostTypeNrvFlyStart)) ||
        isNerve(GET_NERVE(KoteBug, HostTypeNrvFlyPursue)) || isNerve(GET_NERVE(KoteBug, HostTypeNrvFlyEscape))) {
        if (mStarPointer->tryStartPointBind()) {
            setNerve(GET_NERVE(KoteBug, HostTypeNrvBindStarPointer));
        }
    }
}

void KoteBug::exeNoCalcWait() {
    if (MR::isFirstStep(this)) {
        MR::validateClipping(this);
        MR::offBind(this);
        MR::offCalcShadow(this, nullptr);
        MR::offCalcGravity(this);
    }

    mVelocity.zero();
    if (MR::calcDistanceToPlayer(mPosition) < ::hDistNoCalcWait) {
        setNerve(GET_NERVE(KoteBug, HostTypeNrvWait));
    }
}

void KoteBug::endNoCalcWait() {
    MR::onBind(this);
    MR::onCalcShadow(this, nullptr);
    MR::onCalcGravity(this);
}

void KoteBug::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::validateClipping(this);
        MR::startAction(this, "Wait");
    }

    f32 distance = MR::calcDistanceToPlayer(this);
    if (MR::isOnGround(this) && distance < ::hSearchDistance) {
        bool escape = !MR::isPlayerElementModeBee();
        if (escape) {
            setNerve(GET_NERVE(KoteBug, HostTypeNrvEscapeSearch));
        } else {
            setNerve(GET_NERVE(KoteBug, HostTypeNrvSearch));
        }
    } else if (MR::isOnGround(this)) {
        setNerve(GET_NERVE(KoteBug, HostTypeNrvNoCalcWait));
    } else {
        MR::applyVelocityDampAndGravity(this, ::hCommonGravity, ::hGroundDamp, ::hAirDampH, ::hAirDampV, cNum_exeWait_applyVelocityDampAndGravity_1);
    }
}

void KoteBug::endWait() {
    MR::invalidateClipping(this);
}

void KoteBug::exeSearch() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Syaka");
    }

    MR::turnDirectionToPlayerDegreeHorizon(this, &mFront, ::hSearchTurnLimitDegree);
    if (MR::isGreaterStep(this, ::hSeachTime)) {
        setNerve(GET_NERVE(KoteBug, HostTypeNrvFlyPursue));
    } else if (::hSearchDistance < MR::calcDistanceToPlayer(this)) {
        setNerve(GET_NERVE(KoteBug, HostTypeNrvWait));
    } else {
        MR::applyVelocityDampAndGravity(this, ::hCommonGravity, ::hGroundDamp, ::hAirDampH, ::hAirDampV, cNum_exeSearch_applyVelocityDampAndGravity_1);
    }
}

void KoteBug::exeEscapeSearch() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Syaka");
    }

    MR::turnDirectionToPlayerDegreeHorizon(this, &mFront, ::hPreEscapeTurnLimitDegree);
    if (MR::isGreaterStep(this, ::hEscapeSeachTime)) {
        bool escape = !MR::isPlayerElementModeBee();
        if (escape) {
            setNerve(GET_NERVE(KoteBug, HostTypeNrvPreEscape));
        } else {
            setNerve(GET_NERVE(KoteBug, HostTypeNrvFlyPursue));
        }
    } else if (::hSearchDistance < MR::calcDistanceToPlayer(this)) {
        setNerve(GET_NERVE(KoteBug, HostTypeNrvWait));
    } else {
        MR::applyVelocityDampAndGravity(this, ::hCommonGravity, ::hGroundDamp, ::hAirDampH, ::hAirDampV, cNum_exeEscapeSearch_applyVelocityDampAndGravity_1);
    }
}

void KoteBug::exePreEscape() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "PreEscape");
        MR::startSound(this, "SE_EM_KOTEBUG_FIND");
    }

    MR::applyVelocityDampAndGravity(this, ::hCommonGravity, ::hGroundDamp, ::hAirDampH, ::hAirDampV, cNum_exePreEscape_applyVelocityDampAndGravity_1);
    MR::turnDirectionToPlayerDegreeHorizon(this, &mFront, ::hPreEscapeTurnLimitDegree);
    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(KoteBug, HostTypeNrvFlyEscape));
    }
}

void KoteBug::exeFlyStart() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Wait");
    }

    MR::startLevelSound(this, "SE_EM_LV_KOTEBUG_FLY");
    MR::applyVelocityDampAndGravity(this, ::hCommonGravity, ::hGroundDamp, ::hAirDampH, ::hAirDampV, cNum_exeFlyStart_applyVelocityDampAndGravity_1);
    MR::turnDirectionToPlayerDegreeHorizon(this, &mFront, ::hFlyTurnLimitDegree);
    if (MR::isGreaterStep(this, ::hFlyStartTime)) {
        setNerve(GET_NERVE(KoteBug, HostTypeNrvFlyPursue));
    }
}

void KoteBug::exeFlyPursue() {
    if (MR::isFirstStep(this)) {
        if (isNerve(GET_NERVE(KoteBug, HostTypeNrvFlyEscape))) {
            MR::startAction(this, "Jitabata");
        } else {
            MR::startAction(this, "Fly");
        }
    }

    MR::startLevelSound(this, "SE_EM_LV_KOTEBUG_FLY");
    MR::applyVelocityDampAndGravity(this, ::hCommonGravity, ::hGroundDamp, ::hAirDampH, ::hAirDampV, cNum_exeFlyPursue_applyVelocityDampAndGravity_1);
    if (isNerve(GET_NERVE(KoteBug, HostTypeNrvFlyEscape))) {
        MR::turnDirectionFromPlayerDegreeHorizon(this, &mFront, ::hFlyEscapeTurnLimitDegree);
    } else {
        MR::turnDirectionToPlayerDegreeHorizon(this, &mFront, ::hFlyTurnLimitDegree);
    }

    mVelocity.add(mFront * ::hFlyPursueVel);
    if (MR::isShadowProjected(this, nullptr)) {
        MR::addVelocityKeepHeightUseShadow(this, ::hFlyHeight, cNum_exeFlyPursue_addVelocityKeepHeightUseShadow_1, ::hKeepAcc, ::hFreqRange, nullptr);
    }

    bool escape = !MR::isPlayerElementModeBee();
    if (escape) {
        if (isNerve(GET_NERVE(KoteBug, HostTypeNrvFlyPursue))) {
            setNerve(GET_NERVE(KoteBug, HostTypeNrvFlyEscape));
            return;
        }
    } else if (isNerve(GET_NERVE(KoteBug, HostTypeNrvFlyEscape))) {
        setNerve(GET_NERVE(KoteBug, HostTypeNrvFlyPursue));
        return;
    }

    if (::hFlyDistance < MR::calcDistanceToPlayer(this)) {
        setNerve(GET_NERVE(KoteBug, HostTypeNrvWait));
    } else if (isNerve(GET_NERVE(KoteBug, HostTypeNrvFlyEscape)) && MR::isGreaterStep(this, ::hFlyPursueTime)) {
        setNerve(GET_NERVE(KoteBug, HostTypeNrvFlyPursueLast));
    }
}

void KoteBug::exeFlyPursueLast() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Jitabata");
    }

    MR::startLevelSound(this, "SE_EM_LV_KOTEBUG_PURSUE_LAST");
    MR::applyVelocityDampAndGravity(this, ::hCommonGravity, ::hGroundDamp, ::hAirDampH, ::hAirDampV, cNum_exeFlyPursueLast_applyVelocityDampAndGravity_1);
    f32 rate = getNerveStep() / f32(::hFlyPursueLastTime);
    MR::setBckRate(this, ::hFlyPursueLastAddRate + rate);
    mVelocity.add(mFront * (::hFlyPursueVel - ::hFlyPursueLastSubVel * rate));
    if (MR::isShadowProjected(this, nullptr)) {
        MR::addVelocityKeepHeightUseShadow(this, ::hFlyJitabataHeightBase + ::hFlyJitabataHeight * rate, cNum_exeFlyPursueLast_addVelocityKeepHeightUseShadow_1, ::hKeepAcc, ::hFreqRange, nullptr);
    }

    if (::hFlyDistance < MR::calcDistanceToPlayer(this)) {
        setNerve(GET_NERVE(KoteBug, HostTypeNrvWait));
    } else if (MR::isGreaterStep(this, ::hFlyPursueLastTime)) {
        setNerve(GET_NERVE(KoteBug, HostTypeNrvOverturnFall));
    }
}

void KoteBug::exeOverturnFall() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Pote");
        MR::startSound(this, "SE_EM_KOTEBUG_OVERTURN_FALL");
        MR::addVelocityJump(this, ::hOverturnFallJumpVelV);
    }

    MR::applyVelocityDampAndGravity(this, ::hCommonGravity, ::hGroundDamp, ::hAirDampH, ::hAirDampV, cNum_exeOverturnFall_applyVelocityDampAndGravity_1);
    if (MR::isOnGroundCos(this, cNum_exeOverturnFall_f1)) {
        mScaleController->startAndAddScaleVelocityY(::hHitScaleYSpeed);
        mScaleController->startAnim();
        setNerve(GET_NERVE(KoteBug, HostTypeNrvOverturn));
        MR::emitEffect(this, "PoteSmoke");
        MR::startSound(this, "SE_EM_KOTEBUG_FALL_LAND");
    }
}

void KoteBug::exeOverturn() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Jitabata2");
    }

    MR::startLevelSound(this, "SE_EM_LV_KOTEBUG_OVERTURN");
    MR::applyVelocityDampAndGravity(this, ::hCommonGravity, ::hGroundDamp, ::hAirDampH, ::hAirDampV, cNum_exeOverturn_applyVelocityDampAndGravity_1);
    MR::killVelocityOnGroundH(this);
    mBinder->mIsStopAtFirstHit = true;
    if (MR::isGreaterStep(this, ::hOverturnTime)) {
        setNerve(GET_NERVE(KoteBug, HostTypeNrvPreRecover));
    }
}

void KoteBug::exePreRecover() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Gu");
    }

    MR::reboundVelocityFromCollision(this, cNum_exePreRecover_reboundVelocityFromCollision_1, cNum_exePreRecover_reboundVelocityFromCollision_2, cNum_exePreRecover_reboundVelocityFromCollision_3);
    MR::applyVelocityDampAndGravity(this, ::hCommonGravity, ::hGroundDamp, ::hAirDampH, ::hAirDampV, cNum_exePreRecover_applyVelocityDampAndGravity_1);
    MR::killVelocityOnGroundH(this);
    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(KoteBug, HostTypeNrvRecover));
    }
}

void KoteBug::exeRecover() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Pon");
        MR::startSound(this, "SE_EM_KOTEBUG_RECOVER");
        MR::addVelocityJump(this, ::hRecoverJumpVelV);
    }

    MR::reboundVelocityFromCollision(this, cNum_exeRecover_reboundVelocityFromCollision_1, cNum_exeRecover_reboundVelocityFromCollision_2, cNum_exeRecover_reboundVelocityFromCollision_3);
    MR::applyVelocityDampAndGravity(this, ::hCommonGravity, ::hGroundDamp, ::hAirDampH, ::hAirDampV, cNum_exeRecover_applyVelocityDampAndGravity_1);
    if (MR::isOnGround(this)) {
        MR::startSound(this, "SE_EM_KOTEBUG_LAND");
        mScaleController->startAndAddScaleVelocityY(::hHitScaleYSpeed);
        mScaleController->startAnim();
        setNerve(GET_NERVE(KoteBug, HostTypeNrvWait));
    }
}

void KoteBug::exeHitToPlayer() {
    if (MR::isFirstStep(this)) {
        mScaleController->startAndAddScaleVelocityY(::hHitScaleYSpeed);
        mScaleController->startAnim();
        MR::startAction(this, "Wait");
        MR::addVelocityJump(this, ::hHitToPlayerJumpVelV);
        mVelocity.add(mFront * ::hHitToPlayerJumpVelH);
    }

    MR::reboundVelocityFromCollision(this, cNum_exeHitToPlayer_reboundVelocityFromCollision_1, cNum_exeHitToPlayer_reboundVelocityFromCollision_2, cNum_exeHitToPlayer_reboundVelocityFromCollision_3);
    MR::applyVelocityDampAndGravity(this, ::hCommonGravity, ::hGroundDamp, ::hAirDampH, ::hAirDampV, cNum_exeHitToPlayer_applyVelocityDampAndGravity_1);
    if (MR::isOnGround(this)) {
        setNerve(GET_NERVE(KoteBug, HostTypeNrvFlyStart));
    }
}

void KoteBug::exeBlow() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Fly");
        MR::startBlowHitSound(this);
        MR::stopScene(::hBlowStopSceneTime);
        MR::onCalcShadow(this, nullptr);
        MR::onCalcGravity(this);
    }

    Quat4f rotation;
    rotation.setRotate(Vec3f(1, 0, 0), ::hBlowRotateAngle);
    mPosture.mult(mPosture, rotation);

    if (mSpinHitController->execute(this)) {
        MR::startSound(this, "SE_EM_EXPLODE_S");
    }
}

void KoteBug::exeStampDeath() {
    if (MR::isFirstStep(this)) {
        MR::stopBck(this);
        MR::invalidateClipping(this);
        mVelocity.zero();
        mStampController->startAnim(mGravity);
        MR::setBinderRadius(this, cNum_exeStampDeath_setBinderRadius_1);
        MR::forceBindOnGround(this, 0.0f, 1.0f);
    }

    mBinder->mIsStopAtFirstHit = true;
    if (MR::isGreaterStep(this, ::hStampDeathTime)) {
        MR::startSound(this, "SE_EM_EXPLODE_S");
        MR::appearCoinPop(this, mPosition, 1);
        kill();
    }
}

void KoteBug::exeBindStarPointer() {
    MR::updateActorStateAndNextNerve(this, mStarPointer, GET_NERVE(KoteBug, HostTypeNrvWait));
}

void KoteBug::endBindStarPointer() {
    mStarPointer->kill();
}

bool KoteBug::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(KoteBug, HostTypeNrvBlow)) || isNerve(GET_NERVE(KoteBug, HostTypeNrvStampDeath))) {
        return false;
    }

    if (MR::isMsgPlayerHipDrop(msg)) {
        MR::startSound(this, "SE_EM_STOMPED_S");
        setNerve(GET_NERVE(KoteBug, HostTypeNrvStampDeath));
        return true;
    }

    if (MR::isMsgPlayerTrample(msg)) {
        if (isNerve(GET_NERVE(KoteBug, HostTypeNrvOverturnFall))) {
            return false;
        }

        if (isNerve(GET_NERVE(KoteBug, HostTypeNrvOverturn)) || isNerve(GET_NERVE(KoteBug, HostTypeNrvRecover))) {
            MR::startSound(this, "SE_EM_STOMPED_S");
            setNerve(GET_NERVE(KoteBug, HostTypeNrvStampDeath));
            return true;
        }

        MR::startSound(this, "SE_EM_KOTEBUG_TRAMPLE");
        mVelocity.add(mFront * -cNum_receiveMsgPlayerAttack_f1);
        setNerve(GET_NERVE(KoteBug, HostTypeNrvOverturnFall));
        return true;
    }

    if (MR::isMsgLockOnStarPieceShoot(msg)) {
        if (isNerve(GET_NERVE(KoteBug, HostTypeNrvOverturnFall)) || isNerve(GET_NERVE(KoteBug, HostTypeNrvOverturn))) {
            return false;
        }

        return true;
    }

    if (MR::isMsgStarPieceAttack(msg)) {
        if (isNerve(GET_NERVE(KoteBug, HostTypeNrvOverturnFall)) || isNerve(GET_NERVE(KoteBug, HostTypeNrvOverturn))) {
            MR::addVelocityJump(this, ::hOverturnFallJumpVelV);
            return true;
        }

        setOverturnFall(pSender, pReceiver);
        return true;
    }

    if (MR::isMsgInvincibleAttack(msg) || MR::isMsgFireBallAttack(msg)) {
        mSpinHitController->start(this, pSender->mPosition, pReceiver->mPosition);
        setNerve(GET_NERVE(KoteBug, HostTypeNrvBlow));
        return true;
    }

    if (MR::isMsgPlayerHitAll(msg)) {
        if (isNerve(GET_NERVE(KoteBug, HostTypeNrvOverturnFall)) || isNerve(GET_NERVE(KoteBug, HostTypeNrvOverturn))) {
            mSpinHitController->start(this, pSender->mPosition, pReceiver->mPosition);
            setNerve(GET_NERVE(KoteBug, HostTypeNrvBlow));
            return true;
        }

        setOverturnFall(pSender, pReceiver);
        return true;
    }

    return false;
}

bool KoteBug::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgInhaleBlackHole(msg)) {
        kill();
        return true;
    }

    if (MR::isMsgPlayerKick(msg)) {
        if (MR::isPlayerExistUp(this, getSensor("body")->mRadius, cNum_receiveOtherMsg_f1)) {
            return false;
        }

        if (isNerve(GET_NERVE(KoteBug, HostTypeNrvOverturn)) || isNerve(GET_NERVE(KoteBug, HostTypeNrvPreRecover)) ||
            isNerve(GET_NERVE(KoteBug, HostTypeNrvRecover))) {
            mSpinHitController->start(this, pSender->mPosition, pReceiver->mPosition);
            setNerve(GET_NERVE(KoteBug, HostTypeNrvBlow));
            return true;
        }
    }

    return false;
}

void KoteBug::setOverturnFall(HitSensor* pSender, HitSensor* pReceiver) {
    MR::startSound(this, "SE_EM_KOTEBUG_TRAMPLE");

    Vec3f direction(pReceiver->mPosition);
    direction -= pSender->mPosition;
    MR::vecKillElement(direction, mGravity, &direction);
    MR::normalizeOrZero(&direction);
    mVelocity.add(direction * cNum_setOverturnFall_f1);

    setNerve(GET_NERVE(KoteBug, HostTypeNrvOverturnFall));
}

bool KoteBug::receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(KoteBug, HostTypeNrvBlow)) || isNerve(GET_NERVE(KoteBug, HostTypeNrvStampDeath))) {
        return false;
    }

    if (MR::isMsgExplosionAttack(msg)) {
        mSpinHitController->start(this, pSender->mPosition, pReceiver->mPosition);
        setNerve(GET_NERVE(KoteBug, HostTypeNrvBlow));
        return true;
    }

    if (MR::isMsgToEnemyAttackBlow(msg)) {
        mSpinHitController->start(this, pSender->mPosition, pReceiver->mPosition);
        setNerve(GET_NERVE(KoteBug, HostTypeNrvBlow));
        return true;
    }

    if (MR::isMsgToEnemyAttackTrample(msg)) {
        setNerve(GET_NERVE(KoteBug, HostTypeNrvStampDeath));
        return true;
    }

    return false;
}

void KoteBug::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(KoteBug, HostTypeNrvBlow)) || isNerve(GET_NERVE(KoteBug, HostTypeNrvStampDeath))) {
        return;
    }

    if (pSender != getSensor("body")) {
        return;
    }

    if (MR::isSensorPlayer(pReceiver)) {
        if (MR::isPlayerHipDropFalling() || MR::isPlayerHipDropLand()) {
            return;
        }

        if (isNerve(GET_NERVE(KoteBug, HostTypeNrvFlyStart)) || isNerve(GET_NERVE(KoteBug, HostTypeNrvFlyPursue))) {
            if (MR::isPlayerExistUp(this, getSensor("body")->mRadius, cNum_attackSensor_f1)) {
                MR::sendMsgPush(pReceiver, pSender);
                return;
            }

            if (MR::sendMsgEnemyAttack(pReceiver, pSender)) {
                setNerve(GET_NERVE(KoteBug, HostTypeNrvHitToPlayer));
                return;
            }
        }

        MR::sendMsgPushAndKillVelocityToTarget(this, pReceiver, pSender);
    } else if (MR::isSensorEnemy(pReceiver)) {
        MR::sendMsgPush(pReceiver, pSender);
    }
}

bool KoteBug::receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(KoteBug, HostTypeNrvBlow)) || isNerve(GET_NERVE(KoteBug, HostTypeNrvStampDeath)) ||
        isNerve(GET_NERVE(KoteBug, HostTypeNrvBindStarPointer))) {
        return false;
    }

    if (MR::isSensorEnemy(pSender) || MR::isSensorMapObj(pSender)) {
        MR::addVelocityFromPushHorizon(this, ::hPushedAcc, pSender, pReceiver);
        return true;
    }

    return false;
}

void KoteBug::calcAndSetBaseMtx() {
    Pos3f mtx;
    MR::calcMtxFromGravityAndZAxis(&mtx, this, mGravity, mFront);
    Pos3f stampMtx(mStampController->mMtx);
    mtx.concat(stampMtx, mtx);
    mtx.setTrans(mPosition);
    MR::setBaseTRMtx(this, mtx);

    Vec3f scale = mScaleController->mScale * mScale;
    MR::setBaseScale(this, scale);
    mJointController->registerCallBack();
}

bool KoteBug::calcJoint(Pos3f* pMtx, const JointControllerInfo& /*rInfo*/) {
    Pos3f rotation;
    rotation.makeQuat(mPosture);
    pMtx->concat(*pMtx, rotation);
    return true;
}
