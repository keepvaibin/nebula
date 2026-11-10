#include <nebula_game_api.h>
#include "Game/Enemy/BegomanBaby.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/JointController.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/System/GameRules.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 45.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 45.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 35.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_killWithGenItem_f1, "killWithGenItem.f1", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_killWithGenItem_f2, "killWithGenItem.f2", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_killWithGenItem_f3, "killWithGenItem.f3", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_killWithGenItem_f4, "killWithGenItem.f4", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_killWithGenItem_f5, "killWithGenItem.f5", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeNoCalcWait_f1, "exeNoCalcWait.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f1, "exeWait.f1", 0.15f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f2, "exeWait.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSignAttack_f1, "exeSignAttack.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSignAttack_f2, "exeSignAttack.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePursue_f1, "exePursue.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePursue_f2, "exePursue.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeTurn_f1, "exeTurn.f1", 0.4f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeTurn_f2, "exeTurn.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeBrake_f1, "exeBrake.f1", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeBrake_f2, "exeBrake.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeStepBack_f1, "exeStepBack.f1", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeStepBack_f2, "exeStepBack.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeReturn_f1, "exeReturn.f1", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeReturn_f2, "exeReturn.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeProvoke_f1, "exeProvoke.f1", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeProvoke_f2, "exeProvoke.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeTrample_f1, "exeTrample.f1", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeTrample_f2, "exeTrample.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHitReaction_f1, "exeHitReaction.f1", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHitReaction_f2, "exeHitReaction.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeTired_f1, "exeTired.f1", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeTired_f2, "exeTired.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeBlow_f1, "exeBlow.f1", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeBlow_f2, "exeBlow.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAfterLaunch_f1, "exeAfterLaunch.f1", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAfterLaunch_f2, "exeAfterLaunch.f2", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAfterLaunch_f3, "exeAfterLaunch.f3", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeLaunchTurn_f1, "exeLaunchTurn.f1", 0.4f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeLaunchTurn_f2, "exeLaunchTurn.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeInWater_f1, "exeInWater.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeKeepDistance_f1, "exeKeepDistance.f1", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeKeepDistance_f2, "exeKeepDistance.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeKeepDistance_f3, "exeKeepDistance.f3", 600.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeKeepDistance_f4, "exeKeepDistance.f4", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_attackSensor_f1, "attackSensor.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveMsgEnemyAttack_f1, "receiveMsgEnemyAttack.f1", 35.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveMsgEnemyAttack_f2, "receiveMsgEnemyAttack.f2", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveMsgEnemyAttack_f3, "receiveMsgEnemyAttack.f3", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveMsgEnemyAttack_f4, "receiveMsgEnemyAttack.f4", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveMsgPlayerAttack_f1, "receiveMsgPlayerAttack.f1", 35.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveMsgPlayerAttack_f2, "receiveMsgPlayerAttack.f2", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveMsgPlayerAttack_f3, "receiveMsgPlayerAttack.f3", 35.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveMsgPlayerAttack_f4, "receiveMsgPlayerAttack.f4", 15.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_exeAfterLaunch_applyVelocityDampAndGravity_1, "exeAfterLaunch.applyVelocityDampAndGravity.1", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAfterLaunch_applyVelocityDampAndGravity_2, "exeAfterLaunch.applyVelocityDampAndGravity.2", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAfterLaunch_applyVelocityDampAndGravity_3, "exeAfterLaunch.applyVelocityDampAndGravity.3", 0.98f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAfterLaunch_applyVelocityDampAndGravity_4, "exeAfterLaunch.applyVelocityDampAndGravity.4", 0.98f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAfterLaunch_applyVelocityDampAndGravity_5, "exeAfterLaunch.applyVelocityDampAndGravity.5", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAfterLaunchOnGround_applyVelocityDampAndGravity_1, "exeAfterLaunchOnGround.applyVelocityDampAndGravity.1", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAfterLaunchOnGround_applyVelocityDampAndGravity_2, "exeAfterLaunchOnGround.applyVelocityDampAndGravity.2", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAfterLaunchOnGround_applyVelocityDampAndGravity_3, "exeAfterLaunchOnGround.applyVelocityDampAndGravity.3", 0.98f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAfterLaunchOnGround_applyVelocityDampAndGravity_4, "exeAfterLaunchOnGround.applyVelocityDampAndGravity.4", 0.98f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAfterLaunchOnGround_applyVelocityDampAndGravity_5, "exeAfterLaunchOnGround.applyVelocityDampAndGravity.5", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveMsgEnemyAttack_addVelocityLimit_1, "receiveMsgEnemyAttack.addVelocityLimit.1", 4.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_control_1, "control.1", 1);
NEBULA_TUNABLE_NAMED(s32, cStep_exeBlow_1, "exeBlow.1", 20);
NEBULA_TUNABLE_NAMED(s32, cStep_exeAfterLaunch_1, "exeAfterLaunch.1", 80);
NEBULA_TUNABLE_NAMED(s32, cStep_exeAfterLaunch_2, "exeAfterLaunch.2", 85);

namespace NrvBegomanBaby {
    NEW_NERVE_ONEND(HostTypeNrvNoCalcWait, BegomanBaby, NoCalcWait, NoCalcWait);
    NEW_NERVE(HostTypeNrvWait, BegomanBaby, Wait);
    NEW_NERVE(HostTypeNrvSignAttack, BegomanBaby, SignAttack);
    NEW_NERVE(HostTypeNrvPursue, BegomanBaby, Pursue);
    NEW_NERVE(HostTypeNrvTurn, BegomanBaby, Turn);
    NEW_NERVE(HostTypeNrvBrake, BegomanBaby, Brake);
    NEW_NERVE(HostTypeNrvStepBack, BegomanBaby, StepBack);
    NEW_NERVE(HostTypeNrvReturn, BegomanBaby, Return);
    NEW_NERVE(HostTypeNrvProvoke, BegomanBaby, Provoke);
    NEW_NERVE(HostTypeNrvTrample, BegomanBaby, Trample);
    NEW_NERVE(HostTypeNrvHitReaction, BegomanBaby, HitReaction);
    NEW_NERVE(HostTypeNrvTired, BegomanBaby, Tired);
    NEW_NERVE(HostTypeNrvBlow, BegomanBaby, Blow);
    NEW_NERVE(HostTypeNrvAfterLaunch, BegomanBaby, AfterLaunch);
    NEW_NERVE(HostTypeNrvAfterLaunchOnGround, BegomanBaby, AfterLaunchOnGround);
    NEW_NERVE(HostTypeNrvLaunchFromGuarder, BegomanBaby, LaunchFromGuarder);
    NEW_NERVE(HostTypeNrvLaunchTurn, BegomanBaby, LaunchTurn);
    NEW_NERVE(HostTypeNrvInWater, BegomanBaby, InWater);
    NEW_NERVE(HostTypeNrvKeepDistance, BegomanBaby, KeepDistance);
    NEW_NERVE_ONEND(HostTypeNrvBindStarPointer, BegomanBaby, BindStarPointer, BindStarPointer);
};  // namespace NrvBegomanBaby

namespace {
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hWaitParam, {0.0f, 3.0f, 0.95f, 1.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hSignAttackParam, {0.0f, 3.0f, 0.8f, 3.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hPursueParam, {0.4f, 3.0f, 0.97f, 3.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hTurnParam, {0.4f, 3.0f, 0.95f, 0.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hStopParam, {0.0f, 3.0f, 0.95f, 0.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hHitReactionParam, {0.0f, 3.0f, 0.95f, 0.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hTiredParam, {0.2f, 1.0f, 0.95f, 3.0f});

};  // namespace

BegomanBaby::BegomanBaby(const char* pName) : BegomanBase(pName), mBabyDelegator(), mHost(), mAppearThreeStarPiece() {
    mWaterColumnMatrix.identity();
}

BegomanBaby::BegomanBaby(NameObj* pHost, const char* pName) : BegomanBase(pName), mBabyDelegator(), mHost(pHost), mAppearThreeStarPiece() {
    mWaterColumnMatrix.identity();
}

void BegomanBaby::init(const TableIter& rIter) {
    initCore(rIter, "BegomanBaby", false);
    MR::declareStarPiece(this, 3);

    if (mHost == nullptr) {
        mHost = this;
    }

    initBinder(cNum_init_f1, cNum_init_f2, 0);
    initNerve(GET_NERVE(BegomanBaby, HostTypeNrvWait));
    initSensor(0, cNum_init_f3, cNum_init_f4, "Locator1");

    mBabyDelegator = MR::createJointDelegatorWithNullChildFunc(this, &BegomanBaby::calcHeadJoint, "BabyBegomanFace");

    initEffectAndStarPointerBind();
    MR::setEffectHostMtx(this, "WaterColumn", mWaterColumnMatrix);
    initSound(8, false);
    initShadow(cNum_init_f5, "Locator1");

    MR::tryRegisterDemoCast(this, rIter);
    if (MR::isDemoCast(this, nullptr) && MR::isRegisteredDemoActionAppear(this)) {
        makeActorDead();
    } else {
        makeActorAppeared();
    }
}

void BegomanBaby::appear() {
    BegomanBase::appear();
    setNerve(GET_NERVE(BegomanBaby, HostTypeNrvWait));
    MR::validateExCollisionParts(this);
}

void BegomanBaby::appearFromLaunch(const Vec3f& rVec1, const Vec3f& rVec2) {
    BegomanBase::appear();
    MR::emitEffect(this, "Death");
    MR::startSound(this, "SE_EM_BEGOMAN_APPEAR");
    setNerve(GET_NERVE(BegomanBaby, HostTypeNrvAfterLaunch));

    Vec3f vec(mPosition);

    vec -= rVec1;
    MR::turnVecToPlane(&vec, vec, rVec2);
    MR::normalizeOrZero(&vec);

    mTargetVec.set(vec);
    mFaceVec.set(vec);
}

void BegomanBaby::appearFromGuarder() {
    BegomanBase::appear();
    setNerve(GET_NERVE(BegomanBaby, HostTypeNrvLaunchFromGuarder));
}

void BegomanBaby::kill() {
    BegomanBase::kill();

    MR::emitEffect(this, "Death");
    MR::startSound(this, "SE_EM_EXPLODE_S");
}

void BegomanBaby::killWithGenItem() {
    Vec3f minusGravity(-mGravity);
    Vec3f starPieceAppearPos(minusGravity * cNum_killWithGenItem_f1);
    starPieceAppearPos += mPosition;

    bool appearedStarPiece;
    if (mAppearThreeStarPiece) {
        appearedStarPiece = MR::appearStarPiece(this, starPieceAppearPos, 3, cNum_killWithGenItem_f2, cNum_killWithGenItem_f3, false);
    } else {
        appearedStarPiece = MR::appearStarPiece(this, starPieceAppearPos, 1, cNum_killWithGenItem_f4, cNum_killWithGenItem_f5, false);
    }

    if (appearedStarPiece) {
        MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
    }

    kill();
}

void BegomanBaby::control() {
    if (isNerve(GET_NERVE(BegomanBaby, HostTypeNrvPursue)) || isNerve(GET_NERVE(BegomanBaby, HostTypeNrvTurn))) {
        if (incAndCheckTiredCounter()) {
            setNerve(GET_NERVE(BegomanBaby, HostTypeNrvTired));
            return;
        }
    } else {
        mTiredCounter = 0;
    }

    isNerve(GET_NERVE(BegomanBaby, HostTypeNrvBlow));
    checkTouchElectricRail(true);

    if (MR::isStep(this, cStep_control_1)) {
        if (isNerve(GET_NERVE(BegomanBaby, HostTypeNrvWait)) || isNerve(GET_NERVE(BegomanBaby, HostTypeNrvNoCalcWait))) {
            MR::validateClipping(this);
        } else {
            MR::invalidateClipping(this);
        }
    }

    if (!isNerve(GET_NERVE(BegomanBaby, HostTypeNrvInWater)) && !isNerve(GET_NERVE(BegomanBaby, HostTypeNrvNoCalcWait))) {
        if (isInWaterAndSetWaterNerve(GET_NERVE(BegomanBaby, HostTypeNrvInWater), &mWaterColumnMatrix)) {
            return;
        }
    }

    if (!GameRules::begomanFireProof() &&MR::isBindedGroundDamageFire(this)) {
        killWithGenItem();
        MR::emitEffect(this, "Fall");
        return;
    }

    if (MR::isInDeath(this, Vec3f(0.0f, 0.0f, 0.0f))) {
        kill();
        return;
    }

    if (MR::isPressedRoofAndGround(this)) {
        kill();
        return;
    }

    if (isNerve(GET_NERVE(BegomanBaby, HostTypeNrvBlow))) {
        preventSwingby(1.0f);
    }

    if ((!isNerve(GET_NERVE(BegomanBaby, HostTypeNrvNoCalcWait)) && !isNerve(GET_NERVE(BegomanBaby, HostTypeNrvWait)) &&
            !isNerve(GET_NERVE(BegomanBaby, HostTypeNrvSignAttack)) && !isNerve(GET_NERVE(BegomanBaby, HostTypeNrvPursue)) &&
            !isNerve(GET_NERVE(BegomanBaby, HostTypeNrvTurn)) && !isNerve(GET_NERVE(BegomanBaby, HostTypeNrvBrake)) &&
            !isNerve(GET_NERVE(BegomanBaby, HostTypeNrvReturn)) && !isNerve(GET_NERVE(BegomanBaby, HostTypeNrvProvoke)) &&
            !isNerve(GET_NERVE(BegomanBaby, HostTypeNrvLaunchTurn)) && !isNerve(GET_NERVE(BegomanBaby, HostTypeNrvKeepDistance))) ||
        !BegomanBase::tryAndSetStarPointerBind(GET_NERVE(BegomanBaby, HostTypeNrvBindStarPointer))) {
        BegomanBase::control();
    }
}

void BegomanBaby::exeNoCalcWait() {
    exeNoCalcWaitCore(cNum_exeNoCalcWait_f1, GET_NERVE(BegomanBaby, HostTypeNrvWait));
}

void BegomanBaby::endNoCalcWait() {
    finishNoCalcWait();
}

void BegomanBaby::exeWait() {
    updateRotateY(cNum_exeWait_f1, cNum_exeWait_f2);
    MR::startLevelSound(this, "SE_EM_LV_BABYBEGO_ROT_SLOW");
    exeWaitCore(::hWaitParam, GET_NERVE(BegomanBaby, HostTypeNrvSignAttack), GET_NERVE(BegomanBaby, HostTypeNrvKeepDistance),
                GET_NERVE(BegomanBaby, HostTypeNrvNoCalcWait));
}

void BegomanBaby::exeSignAttack() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "SignAttack");
        MR::startSound(this, "SE_EM_BABYBEGO_PRE_PURSUE");
    }

    updateRotateY(cNum_exeSignAttack_f1, cNum_exeSignAttack_f2);
    MR::startLevelSound(this, "SE_EM_LV_BABYBEGO_ROT_MIDDLE");
    return exeSignAttackCore(::hSignAttackParam, GET_NERVE(BegomanBaby, HostTypeNrvPursue));
}

void BegomanBaby::exePursue() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_EM_BABYBEGO_PURSUE_START");
    }

    updateRotateY(cNum_exePursue_f1, cNum_exePursue_f2);
    MR::startLevelSound(this, "SE_EM_LV_BEGOMAN_PURSUE");
    exePursueCore(::hPursueParam, GET_NERVE(BegomanBaby, HostTypeNrvBrake), GET_NERVE(BegomanBaby, HostTypeNrvTurn), *getSoundNormal(), 1.0f);
}

void BegomanBaby::exeTurn() {
    updateRotateY(cNum_exeTurn_f1, cNum_exeTurn_f2);
    MR::startLevelSound(this, "SE_EM_LV_BABYBEGO_TURN");
    MR::startLevelSound(this, "SE_EM_LV_BABYBEGO_ROT_MIDDLE");
    exeTurnCore(::hTurnParam, GET_NERVE(BegomanBaby, HostTypeNrvBrake), GET_NERVE(BegomanBaby, HostTypeNrvPursue), false);
}

void BegomanBaby::exeBrake() {
    updateRotateY(cNum_exeBrake_f1, cNum_exeBrake_f2);
    MR::startLevelSound(this, "SE_EM_LV_BEGOMAN_SPARK");
    MR::startLevelSound(this, "SE_EM_LV_BABYBEGO_ROT_MIDDLE");
    exeBrakeCore(GET_NERVE(BegomanBaby, HostTypeNrvTurn));
}

void BegomanBaby::exeStepBack() {
    updateRotateY(cNum_exeStepBack_f1, cNum_exeStepBack_f2);
    exeStepBackCore(::hWaitParam, GET_NERVE(BegomanBaby, HostTypeNrvWait));
}

void BegomanBaby::exeReturn() {
    updateRotateY(cNum_exeReturn_f1, cNum_exeReturn_f2);
    exeReturnCore(GET_NERVE(BegomanBaby, HostTypeNrvWait));
}

void BegomanBaby::exeProvoke() {
    updateRotateY(cNum_exeProvoke_f1, cNum_exeProvoke_f2);
    MR::startLevelSound(this, "SE_EM_LV_BABYBEGO_ROT_MIDDLE");
    exeProvokeCore(::hWaitParam, GET_NERVE(BegomanBaby, HostTypeNrvSignAttack));
}

void BegomanBaby::exeTrample() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Trample");
        MR::startSound(this, "SE_EM_BABYBEGO_STOMPED");
    }

    updateRotateY(cNum_exeTrample_f1, cNum_exeTrample_f2);
    MR::moveAndTurnToDirection(this, &mFaceVec, mTargetVec, ::hStopParam);
    reboundWallAndGround(&mFaceVec, false);
    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(3))) {
        setNerve(GET_NERVE(BegomanBaby, HostTypeNrvSignAttack));
    }
}

void BegomanBaby::exeHitReaction() {
    updateRotateY(cNum_exeHitReaction_f1, cNum_exeHitReaction_f2);
    exeHitReactionCore(::hHitReactionParam, GET_NERVE(BegomanBaby, HostTypeNrvProvoke));
}

void BegomanBaby::exeTired() {

    updateRotateY(cNum_exeTired_f1, cNum_exeTired_f2);
    MR::startLevelSound(this, "SE_EM_LV_BEGOMAN_TURN");
    exeTiredCore(::hTiredParam, GET_NERVE(BegomanBaby, HostTypeNrvWait));
}

void BegomanBaby::exeBlow() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_EM_BEGOMAN_ROT_STOP");
        MR::startBck(this, "Stop");
        MR::stopScene(2);
        MR::invalidateExCollisionParts(this);
    }

    updateRotateY(cNum_exeBlow_f1, cNum_exeBlow_f2);
    MR::startLevelSound(this, "SE_EM_LV_BEGOMAN_SPARK");
    MR::startLevelSound(this, "SE_EM_LV_BABYBEGO_ROT_MIDDLE");
    MR::moveAndTurnToDirection(this, &mFaceVec, mTargetVec, ::hHitReactionParam);
    reboundWallAndGround(&mFaceVec, false);

    if (MR::isGreaterStep(this, cStep_exeBlow_1)) {
        killWithGenItem();
    }
}

void BegomanBaby::exeAfterLaunch() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Fly");
    }

    if (MR::isLessStep(this, cStep_exeAfterLaunch_1)) {
        // Eight bobs over the 80 steps.
        f32 bob = cNum_exeAfterLaunch_f1 * MR::cos(getNerveStep() * (16 * PI) / cNum_exeAfterLaunch_f2);
        mVelocity.set(mFaceVec * cNum_exeAfterLaunch_f3 - mGravity * bob);
    } else if (MR::isLessStep(this, cStep_exeAfterLaunch_2)) {
        mVelocity.zero();
    } else {
        MR::applyVelocityDampAndGravity(this, cNum_exeAfterLaunch_applyVelocityDampAndGravity_1, cNum_exeAfterLaunch_applyVelocityDampAndGravity_2, cNum_exeAfterLaunch_applyVelocityDampAndGravity_3, cNum_exeAfterLaunch_applyVelocityDampAndGravity_4, cNum_exeAfterLaunch_applyVelocityDampAndGravity_5);
    }

    if (MR::isOnGround(this)) {
        setNerve(GET_NERVE(BegomanBaby, HostTypeNrvAfterLaunchOnGround));
    }
}

void BegomanBaby::exeAfterLaunchOnGround() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Bound");
    }

    reboundWallAndGround(&mFaceVec, false);
    MR::applyVelocityDampAndGravity(this, cNum_exeAfterLaunchOnGround_applyVelocityDampAndGravity_1, cNum_exeAfterLaunchOnGround_applyVelocityDampAndGravity_2, cNum_exeAfterLaunchOnGround_applyVelocityDampAndGravity_3, cNum_exeAfterLaunchOnGround_applyVelocityDampAndGravity_4, cNum_exeAfterLaunchOnGround_applyVelocityDampAndGravity_5);

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(BegomanBaby, HostTypeNrvWait));
    }
}

void BegomanBaby::exeLaunchFromGuarder() {
    exeLaunch();
}

void BegomanBaby::exeLaunchTurn() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Turn");
        mFaceVec.set(mTargetVec);
    }

    updateRotateY(cNum_exeLaunchTurn_f1, cNum_exeLaunchTurn_f2);
    MR::startLevelSound(this, "SE_EM_LV_BABYBEGO_ROT_MIDDLE");
    exeTurnCore(::hTurnParam, GET_NERVE(BegomanBaby, HostTypeNrvBrake), GET_NERVE(BegomanBaby, HostTypeNrvPursue), false);
}

void BegomanBaby::exeInWater() {
    if (MR::isFirstStep(this)) {
        mVelocity += mGravity;
        mVelocity.setLength(cNum_exeInWater_f1);
        MR::startSound(this, "SE_EM_FALL_INTO_WATER_S");
    }

    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
        killWithGenItem();
        MR::emitEffect(this, "DeathWater");
        MR::startSound(this, "SE_EM_BEGOMAN_DEAD_IN_WATER");
    }
}

void BegomanBaby::exeKeepDistance() {
    updateRotateY(cNum_exeKeepDistance_f1, cNum_exeKeepDistance_f2);
    exeKeepDistanceCore(GET_NERVE(BegomanBaby, HostTypeNrvWait), GET_NERVE(BegomanBaby, HostTypeNrvSignAttack),
                        GET_NERVE(BegomanBaby, HostTypeNrvBrake), cNum_exeKeepDistance_f3, cNum_exeKeepDistance_f4);
}

void BegomanBaby::exeBindStarPointer() {
    BegomanBase::exeBindStarPointer();
}

void BegomanBaby::endBindStarPointer() {
    BegomanBase::finishBindStarPointer();
}

void BegomanBaby::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (pSender != getSensor("body")) {
        return;
    }

    if (MR::isSensorEnemy(pReceiver) && MR::isOnGround(this)) {
        MR::sendMsgEnemyAttack(pReceiver, pSender);
        return;
    }

    if (!MR::isSensorPlayer(pReceiver) || isNerve(GET_NERVE(BegomanBaby, HostTypeNrvBlow)) || MR::isPlayerSwingAction() || !MR::isOnGroundPlayer()) {
        return;
    }

    bool msgResult = MR::sendArbitraryMsg(ACTMES_ENEMY_ATTACK_FLIP_ROT, pReceiver, pSender);

    if (msgResult) {
        Vec3f dirFromSenderToReceiver(pSender->mPosition);

        dirFromSenderToReceiver -= pReceiver->mPosition;
        MR::normalizeOrZero(&dirFromSenderToReceiver);

        if (!MR::isNearZero(dirFromSenderToReceiver)) {
            bool reflected = reboundPlaneWithEffect(dirFromSenderToReceiver, 0.0f, 0.0f, "Spark");
            mVelocity += dirFromSenderToReceiver * cNum_attackSensor_f1;

            if (reflected) {
                MR::startSound(this, "SE_EM_BABYBEGO_COLLI");
            }
        }

        setNerve(GET_NERVE(BegomanBaby, HostTypeNrvHitReaction));
    }
}

bool BegomanBaby::receiveMsgPush(HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    return false;
}

bool BegomanBaby::receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (pReceiver != getSensor("body")) {
        return false;
    }

    if (MR::isMsgEnemyAttackElectric(msg)) {
        Vec3f senderVelocity(pSender->mHost->mVelocity);

        MR::vecKillElement(senderVelocity, mGravity, &senderVelocity);
        MR::normalizeOrZero(&senderVelocity);
        return onTouchElectric(pSender->mPosition, senderVelocity);
    }

    if (MR::isMsgExplosionAttack(msg)) {
        if (!isNerve(GET_NERVE(BegomanBaby, HostTypeNrvBlow))) {
            mAppearThreeStarPiece = false;
            calcBlowReaction(pSender->mPosition, pReceiver->mPosition, cNum_receiveMsgEnemyAttack_f1, cNum_receiveMsgEnemyAttack_f2);
            setNerve(GET_NERVE(BegomanBaby, HostTypeNrvBlow));
            return true;
        } else {
            return false;
        }
    }

    if (isNerve(GET_NERVE(BegomanBaby, HostTypeNrvAfterLaunch)) || isNerve(GET_NERVE(BegomanBaby, HostTypeNrvLaunchFromGuarder)) ||
        isNerve(GET_NERVE(BegomanBaby, HostTypeNrvLaunchTurn))) {
        return false;
    }

    if (MR::isSensorEnemy(pSender)) {
        Vec3f dirFromReceiverToSender(pReceiver->mPosition);

        dirFromReceiverToSender -= pSender->mPosition;
        MR::normalizeOrZero(&dirFromReceiverToSender);

        if (MR::isNearZero(dirFromReceiverToSender)) {
            return false;
        }

        bool reflected = reboundPlaneWithEffect(dirFromReceiverToSender, 0.0f, 0.0f, "Spark");

        if (isFallNextMove(cNum_receiveMsgEnemyAttack_f3, cNum_receiveMsgEnemyAttack_f4)) {
            setNerve(GET_NERVE(BegomanBaby, HostTypeNrvBrake));
            return true;
        }

        const f32 bodyRadius = getSensor("body")->mRadius;
        const f32 senderRadius = pSender->mRadius;
        f32 sizeRatio = senderRadius / bodyRadius;

        MR::addVelocityLimit(this, dirFromReceiverToSender * cNum_receiveMsgEnemyAttack_addVelocityLimit_1 * sizeRatio);

        if (reflected) {
            MR::startSound(this, "SE_EM_BABYBEGO_COLLI_BEGOMAN");
        }

        if (!isNerve(GET_NERVE(BegomanBaby, HostTypeNrvTrample)) && !isNerve(GET_NERVE(BegomanBaby, HostTypeNrvProvoke)) &&
            !isNerve(GET_NERVE(BegomanBaby, HostTypeNrvBlow)) && MR::isOnGround(this)) {
            MR::startBck(this, "Turn");
            setNerve(GET_NERVE(BegomanBaby, HostTypeNrvTurn));
        }

        return true;
    }

    return false;
}

bool BegomanBaby::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgLockOnStarPieceShoot(msg)) {
        return true;
    }

    if (MR::isMsgPlayerTrample(msg)) {
        return receiveMsgTrample(pSender, pReceiver);
    }

    if (MR::isMsgPlayerHipDrop(msg)) {
        MR::forceJumpPlayer(-mGravity);
        return true;
    }

    if (pReceiver != getSensor("body")) {
        return false;
    }

    if (isNerve(GET_NERVE(BegomanBaby, HostTypeNrvBlow))) {
        return false;
    }

    if (MR::isMsgStarPieceAttack(msg)) {
        mAppearThreeStarPiece = true;
        calcBlowReaction(pSender->mPosition, pReceiver->mPosition, cNum_receiveMsgPlayerAttack_f1, cNum_receiveMsgPlayerAttack_f2);
        setNerve(GET_NERVE(BegomanBaby, HostTypeNrvBlow));
        return true;
    }

    if (MR::isMsgPlayerHitAll(msg)) {
        mAppearThreeStarPiece = false;
        calcBlowReaction(pSender->mPosition, pReceiver->mPosition, cNum_receiveMsgPlayerAttack_f3, cNum_receiveMsgPlayerAttack_f4);
        setNerve(GET_NERVE(BegomanBaby, HostTypeNrvBlow));
        return true;
    }

    return false;
}

bool BegomanBaby::receiveMsgTrample(HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(BegomanBaby, HostTypeNrvBlow))) {
        return false;
    }

    if (!pReceiver->isType(ATYPE_BEGOMAN)) {
        return false;
    }

    setNerve(GET_NERVE(BegomanBaby, HostTypeNrvTrample));
    return true;
}

bool BegomanBaby::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    return MR::isMsgHitmarkEmit(msg);
}

bool BegomanBaby::onTouchElectric(const Vec3f& rVec1, const Vec3f& rVec2) {
    if (isNerve(GET_NERVE(BegomanBaby, HostTypeNrvBlow)) || isNerve(GET_NERVE(BegomanBaby, HostTypeNrvInWater))) {
        return false;
    }

    if (isNerve(GET_NERVE(BegomanBaby, HostTypeNrvStepBack))) {
        if (!MR::isOnGround(this)) {
            return false;
        }

        kill();
        return true;
    }

    if (BegomanBase::onTouchElectric(rVec1, rVec2)) {
        setNerve(GET_NERVE(BegomanBaby, HostTypeNrvStepBack));
        return true;
    }

    return false;
}

bool BegomanBaby::setNerveReturn() {
    setNerve(GET_NERVE(BegomanBaby, HostTypeNrvReturn));
    return true;
}

const Nerve* BegomanBaby::getNerveWait() {
    return GET_NERVE_ANON(NrvBegomanBaby::HostTypeNrvWait);
}

bool BegomanBaby::calcHeadJoint(Pos3f* pPos, const JointControllerInfo& /*rInfo*/) {
    if (isNerve(GET_NERVE(BegomanBaby, HostTypeNrvBlow))) {
        return true;
    }

    Vec3f yDir;
    pPos->getYDir(yDir);

    if (!MR::isSameDirection(yDir, mTargetVec)) {
        MR::makeMtxUpFront(pPos, yDir, mTargetVec);
    }

    return true;
}

void BegomanBaby::calcAndSetBaseMtx() {
    BegomanBase::calcAndSetBaseMtx();
    mBabyDelegator->registerCallBack();
}
