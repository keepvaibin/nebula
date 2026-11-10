#include <nebula_game_api.h>
#include "Game/Enemy/BegomanSpike.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/BegomanSpringHead.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
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
#include "Game/System/GameRules.hpp"
#include "Game/Util/Tunable.hpp"

namespace NrvBegomanSpike {
    NEW_NERVE_ONEND(HostTypeNrvNoCalcWait, BegomanSpike, NoCalcWait, NoCalcWait);
    NEW_NERVE_ONEND(HostTypeNrvWait, BegomanSpike, Wait, Wait);
    NEW_NERVE_ONEND(HostTypeNrvSignAttack, BegomanSpike, SignAttack, HandBlur);
    NEW_NERVE_ONEND(HostTypeNrvPursue, BegomanSpike, Pursue, Pursue);
    NEW_NERVE_ONEND(HostTypeNrvTurn, BegomanSpike, Turn, Turn);
    NEW_NERVE(HostTypeNrvBrake, BegomanSpike, Brake);
    NEW_NERVE(HostTypeNrvStepBack, BegomanSpike, StepBack);
    NEW_NERVE(HostTypeNrvReturn, BegomanSpike, Return);
    NEW_NERVE_ONEND(HostTypeNrvProvoke, BegomanSpike, Provoke, HandBlur);
    NEW_NERVE_ONEND(HostTypeNrvTrampleReaction, BegomanSpike, TrampleReaction, HandBlur);
    NEW_NERVE_ONEND(HostTypeNrvHitReaction, BegomanSpike, HitReaction, HandBlur);
    NEW_NERVE(HostTypeNrvTired, BegomanSpike, Tired);
    NEW_NERVE(HostTypeNrvBlow, BegomanSpike, Blow);
    NEW_NERVE(HostTypeNrvElectricDeath, BegomanSpike, ElectricDeath);
    NEW_NERVE(HostTypeNrvBrokenPiece, BegomanSpike, BrokenPiece);
    NEW_NERVE(HostTypeNrvInWater, BegomanSpike, InWater);
    NEW_NERVE(HostTypeNrvKeepDistance, BegomanSpike, KeepDistance);
    NEW_NERVE(HostTypeNrvLaunch, BegomanSpike, Launch);
    NEW_NERVE_ONEND(HostTypeNrvBindStarPointer, BegomanSpike, BindStarPointer, BindStarPointer);
};  // namespace NrvBegomanSpike

namespace {
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, cWaitParam, {0.0f, 3.0f, 0.95f, 1.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, cSignAttackParam, {0.0f, 3.0f, 0.8f, 3.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, cPursueParam, {0.4f, 3.0f, 0.97f, 3.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, cTurnParam, {0.4f, 3.0f, 0.95f, 0.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, cStopParam, {0.0f, 3.0f, 0.95f, 0.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, cHitReactionParam, {0.0f, 3.0f, 0.95f, 0.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, cTiredParam, {0.2f, 1.0f, 0.95f, 3.0f});

    NEBULA_TUNABLE(f32, cBinderRadius, 100.0f);
    NEBULA_TUNABLE(f32, cBodyRadius, 70.0f);
    NEBULA_TUNABLE(f32, cBodyHeight, 140.0f);
    NEBULA_TUNABLE(f32, cReboundEnemyAddVel, 8.0f);
    NEBULA_TUNABLE(f32, cReboundPlayerAddVel, 2.0f);
    NEBULA_TUNABLE(f32, cPreventSwingbyDamp, 0.7f);
    // Spin speed: each state's rate, plus cCommonAddRotate.
    NEBULA_TUNABLE(f32, cCommonAddRotate, 0.5f);
    NEBULA_TUNABLE(f32, cNoCalcWaitRotate, 0.5f);
    NEBULA_TUNABLE(f32, cWaitRotate, 0.15f);
    NEBULA_TUNABLE(f32, cSignAttackRotate, 0.5f);
    NEBULA_TUNABLE(f32, cPursueRotate, 0.5f);
    NEBULA_TUNABLE(f32, cTurnRotate, 0.4f);
    NEBULA_TUNABLE(f32, cBrakeRotate, 0.2f);
    NEBULA_TUNABLE(f32, cProvokeRotate, 0.3f);
    NEBULA_TUNABLE(f32, cTrampleReactionRotate, 0.25f);
    NEBULA_TUNABLE(f32, cHitReactionRotate, 0.25f);
    // Step back, return, tired and keep distance.
    NEBULA_TUNABLE(f32, cSlowRotate, 0.2f);
    NEBULA_TUNABLE(f32, cCoinTransY, 80.0f);
    NEBULA_TUNABLE(s32, cCoinGenerateNum, 1);
    const s32 cTrampleReactionTime = NebulaFrameClock::stepsOfSeconds(2);
    // A trample within this many steps of the last one does not restart the reaction.
    const s32 cTrampleRepeatTime = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(s32, cStopSceneTime, 2);
    NEBULA_TUNABLE(s32, cBlowFrame, 20);
    NEBULA_TUNABLE(f32, cBlowVel, 40.0f);
    NEBULA_TUNABLE(f32, cBlowVerticalVel, 10.0f);
    NEBULA_TUNABLE(f32, cExplosionBlowVel, 35.0f);
    NEBULA_TUNABLE(f32, cExplosionBlowVerticalVel, 15.0f);
    NEBULA_TUNABLE(s32, cDeathStopSceneTime, 5);
    NEBULA_TUNABLE(s32, cElectricDeathTime, 20);
    NEBULA_TUNABLE(f32, cInWaterVel, 10.0f);
    const s32 cInWaterTime = NebulaFrameClock::stepsOfSeconds(1);
    NEBULA_TUNABLE(f32, cKeepDistFar, 800.0f);
    NEBULA_TUNABLE(f32, cKeepDistNear, 600.0f);
}  // namespace

BegomanSpike::BegomanSpike(const char* pName) : BegomanBase(pName), mHead(nullptr), mBrokenModel(nullptr), mTurnAfterBlow(false) {
    mHeadMatrix.identity();
    mWaterColumnMatrix.identity();
}

void BegomanSpike::init(const TableIter& rIter) {
    initCore(rIter, "BegomanSpike", false);
    initUseSwitchB(rIter, MR::Functor(this, &BegomanSpike::onSwitchB));
    MR::declareCoin(this, ::cCoinGenerateNum);
    initBinder(::cBinderRadius, ::cBinderRadius, 0);
    initNerve(GET_NERVE(BegomanSpike, HostTypeNrvWait));
    initSensor(0, ::cBodyRadius, ::cBodyHeight, "Center");
    initEffectAndStarPointerBind();
    MR::setEffectHostMtx(this, "WaterColumn", mWaterColumnMatrix);
    initSound(8, false);
    initShadow(::cBodyRadius, "Center");
    mHead = new BegomanHead(this, "トゲ頭", "BegomanSpikeHead", mHeadMatrix, MR::DrawBufferType_Enemy, false);
    MR::initLightCtrl(mHead);
    mHead->mHostScale = &mAnimScale;
    mHead->initWithoutIter();
    mBrokenModel = new ModelObj("ベーゴマン壊れモデル", "BegomanBrokenPiece", getBaseMtx(), MR::DrawBufferType_Enemy, -2, -2, false);
    mBrokenModel->mScale.set(mScale);
    mBrokenModel->initWithoutIter();
    MR::invalidateClipping(mBrokenModel);
    mBrokenModel->makeActorDead();
    if (MR::tryRegisterDemoCast(this, rIter)) {
        MR::tryRegisterDemoCast(mHead, rIter);
    }

    if (MR::isDemoCast(this, nullptr) && MR::isRegisteredDemoActionAppear(this)) {
        makeActorDead();
    } else {
        makeActorAppeared();
    }
}

void BegomanSpike::endWait() {
    MR::invalidateClipping(this);
}

void BegomanSpike::endHandBlur() {
    MR::deleteEffect(this, "HandBlur1");
    MR::deleteEffect(this, "HandBlur2");
    MR::deleteEffect(this, "HandBlur3");
}

void BegomanSpike::onSwitchB() {
    setNerve(GET_NERVE(BegomanSpike, HostTypeNrvBrokenPiece));
}

void BegomanSpike::appear() {
    BegomanBase::appear();
    MR::emitEffect(this, "Death");
    MR::startSound(this, "SE_EM_BEGOMAN_APPEAR");
    setNerve(GET_NERVE(BegomanSpike, HostTypeNrvWait));
}

void BegomanSpike::control() {
    if (isNerve(GET_NERVE(BegomanSpike, HostTypeNrvPursue)) || isNerve(GET_NERVE(BegomanSpike, HostTypeNrvTurn))) {
        if (incAndCheckTiredCounter()) {
            setNerve(GET_NERVE(BegomanSpike, HostTypeNrvTired));
            return;
        }
    } else {
        mTiredCounter = 0;
    }

    if (isNrvBlown()) {
        checkTouchElectricRail(true);
    } else if (!isNerve(GET_NERVE(BegomanSpike, HostTypeNrvElectricDeath)) && !isNerve(GET_NERVE(BegomanSpike, HostTypeNrvBrokenPiece))) {
        checkTouchElectricRail(false);
    }

    if (!isNerve(GET_NERVE(BegomanSpike, HostTypeNrvInWater)) &&
        isInWaterAndSetWaterNerve(GET_NERVE(BegomanSpike, HostTypeNrvInWater), &mWaterColumnMatrix)) {
        return;
    }

    if (!GameRules::begomanFireProof() &&MR::isBindedGroundDamageFire(this)) {
        kill();
        MR::emitEffect(this, "Fall");
        return;
    }

    if (MR::isInDeath(this, Vec3f(0.0f, 0.0f, 0.0f)) || MR::isPressedRoofAndGround(this)) {
        kill();
        return;
    }

    if (isNerve(GET_NERVE(BegomanSpike, HostTypeNrvBlow))) {
        preventSwingby(::cPreventSwingbyDamp);
    }

    if (!isNrvStarPointerBindable() || !tryAndSetStarPointerBind(GET_NERVE(BegomanSpike, HostTypeNrvBindStarPointer))) {
        BegomanBase::control();
    }
}

// Blown away, or turning to a stop after it.
bool BegomanSpike::isNrvBlown() const {
    return isNerve(GET_NERVE(BegomanSpike, HostTypeNrvBlow)) || (isNerve(GET_NERVE(BegomanSpike, HostTypeNrvTurn)) && mTurnAfterBlow);
}

bool BegomanSpike::isNrvBroken() const {
    return isNerve(GET_NERVE(BegomanSpike, HostTypeNrvBlow)) || isNerve(GET_NERVE(BegomanSpike, HostTypeNrvElectricDeath)) ||
           isNerve(GET_NERVE(BegomanSpike, HostTypeNrvBrokenPiece));
}

bool BegomanSpike::isNrvStarPointerBindable() const {
    return isNerve(GET_NERVE(BegomanSpike, HostTypeNrvNoCalcWait)) || isNerve(GET_NERVE(BegomanSpike, HostTypeNrvWait)) ||
           isNerve(GET_NERVE(BegomanSpike, HostTypeNrvSignAttack)) || isNerve(GET_NERVE(BegomanSpike, HostTypeNrvPursue)) ||
           isNerve(GET_NERVE(BegomanSpike, HostTypeNrvTurn)) || isNerve(GET_NERVE(BegomanSpike, HostTypeNrvBrake)) ||
           isNerve(GET_NERVE(BegomanSpike, HostTypeNrvReturn)) || isNerve(GET_NERVE(BegomanSpike, HostTypeNrvProvoke)) ||
           isNerve(GET_NERVE(BegomanSpike, HostTypeNrvKeepDistance));
}

bool BegomanSpike::onTouchElectric(const Vec3f& rPos, const Vec3f& rDirection) {
    if (isNerve(GET_NERVE(BegomanSpike, HostTypeNrvElectricDeath)) || isNerve(GET_NERVE(BegomanSpike, HostTypeNrvInWater)) ||
        isNerve(GET_NERVE(BegomanSpike, HostTypeNrvBrokenPiece))) {
        return false;
    }

    if (isNrvBlown()) {
        setNerve(GET_NERVE(BegomanSpike, HostTypeNrvElectricDeath));
        return false;
    }

    if (isNerve(GET_NERVE(BegomanSpike, HostTypeNrvStepBack)) && !MR::isOnGround(this)) {
        return false;
    }

    if (BegomanBase::onTouchElectric(rPos, rDirection)) {
        setNerve(GET_NERVE(BegomanSpike, HostTypeNrvStepBack));
        return true;
    }

    return false;
}

bool BegomanSpike::setNerveReturn() {
    setNerve(GET_NERVE(BegomanSpike, HostTypeNrvReturn));
    return true;
}

const Nerve* BegomanSpike::getNerveWait() {
    return GET_NERVE(BegomanSpike, HostTypeNrvWait);
}

void BegomanSpike::setNerveLaunch() {
    setNerve(GET_NERVE(BegomanSpike, HostTypeNrvLaunch));
}

void BegomanSpike::exeNoCalcWait() {
    exeNoCalcWaitCore(::cNoCalcWaitRotate, GET_NERVE(BegomanSpike, HostTypeNrvWait));
}

void BegomanSpike::endNoCalcWait() {
    finishNoCalcWait();
}

void BegomanSpike::exeWait() {
    updateRotateY(::cWaitRotate, ::cCommonAddRotate);
    MR::startLevelSound(this, "SE_EM_LV_BEGOMAN_ROT_SLOW");
    exeWaitCore(::cWaitParam, GET_NERVE(BegomanSpike, HostTypeNrvSignAttack), GET_NERVE(BegomanSpike, HostTypeNrvKeepDistance),
                GET_NERVE(BegomanSpike, HostTypeNrvNoCalcWait));
}

void BegomanSpike::exeSignAttack() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "SignAttack");
        MR::startSound(this, "SE_EM_BEGOMAN_PRE_PURSUE");
    }

    updateRotateY(::cSignAttackRotate, ::cCommonAddRotate);
    MR::startLevelSound(this, "SE_EM_LV_BEGOMAN_ROT_MIDDLE");
    exeSignAttackCore(::cSignAttackParam, GET_NERVE(BegomanSpike, HostTypeNrvPursue));
}

void BegomanSpike::exePursue() {
    if (MR::isFirstStep(this)) {
        MR::emitEffect(this, "AttackBlur");
        MR::startSound(this, "SE_EM_BEGOMAN_PURSUE_START");
        MR::emitEffect(this, "HandBlur1");
        MR::emitEffect(this, "HandBlur2");
        MR::emitEffect(this, "HandBlur3");
    }

    updateRotateY(::cPursueRotate, ::cCommonAddRotate);
    MR::startLevelSound(this, "SE_EM_LV_BEGOMAN_PURSUE");
    exePursueCore(::cPursueParam, GET_NERVE(BegomanSpike, HostTypeNrvBrake), GET_NERVE(BegomanSpike, HostTypeNrvTurn), *getSoundNormal(), 1.0f);
}

void BegomanSpike::tearDownPursue() {
    MR::deleteEffect(this, "AttackBlur");
    endHandBlur();
}

void BegomanSpike::endPursue() {
    tearDownPursue();
}

void BegomanSpike::exeTurn() {
    updateRotateY(::cTurnRotate, ::cCommonAddRotate);
    MR::startLevelSound(this, "SE_EM_LV_BEGOMAN_ROT_MIDDLE");
    MR::startLevelSound(this, "SE_EM_LV_BEGOMAN_TURN");
    // After a blow the turn ends tired; otherwise it resumes the pursuit.
    const Nerve* pNextNerve = mTurnAfterBlow ? static_cast< const Nerve* >(GET_NERVE(BegomanSpike, HostTypeNrvTired))
                                             : static_cast< const Nerve* >(GET_NERVE(BegomanSpike, HostTypeNrvPursue));
    exeTurnCore(::cTurnParam, GET_NERVE(BegomanSpike, HostTypeNrvBrake), pNextNerve, mTurnAfterBlow);
}

void BegomanSpike::endTurn() {
    mTurnAfterBlow = false;
}

void BegomanSpike::exeBrake() {
    updateRotateY(::cBrakeRotate, ::cCommonAddRotate);
    MR::startLevelSound(this, "SE_EM_LV_BEGOMAN_SPARK");
    MR::startLevelSound(this, "SE_EM_LV_BEGOMAN_ROT_MIDDLE");
    exeBrakeCore(GET_NERVE(BegomanSpike, HostTypeNrvTurn));
}

void BegomanSpike::exeStepBack() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "electricshock");
    }

    updateRotateY(::cSlowRotate, ::cCommonAddRotate);
    exeStepBackCore(::cWaitParam, GET_NERVE(BegomanSpike, HostTypeNrvWait));
}

void BegomanSpike::exeReturn() {
    updateRotateY(::cSlowRotate, ::cCommonAddRotate);
    exeReturnCore(GET_NERVE(BegomanSpike, HostTypeNrvWait));
}

void BegomanSpike::exeProvoke() {
    updateRotateY(::cProvokeRotate, ::cCommonAddRotate);
    MR::startLevelSound(this, "SE_EM_LV_BEGOMAN_ROT_MIDDLE");
    exeProvokeCore(::cWaitParam, GET_NERVE(BegomanSpike, HostTypeNrvSignAttack));
}

void BegomanSpike::exeTrampleReaction() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "TrampleReaction");
    }

    updateRotateY(::cTrampleReactionRotate, ::cCommonAddRotate);
    MR::moveAndTurnToDirection(this, &mFaceVec, mTargetVec, ::cStopParam);
    reboundWallAndGround(&mFaceVec, false);
    if (MR::isGreaterStep(this, ::cTrampleReactionTime)) {
        setNerve(GET_NERVE(BegomanSpike, HostTypeNrvProvoke));
    }
}

void BegomanSpike::exeHitReaction() {
    updateRotateY(::cHitReactionRotate, ::cCommonAddRotate);
    exeHitReactionCore(::cHitReactionParam, GET_NERVE(BegomanSpike, HostTypeNrvProvoke));
}

void BegomanSpike::exeTired() {
    updateRotateY(::cSlowRotate, ::cCommonAddRotate);
    MR::startLevelSound(this, "SE_EM_LV_BEGOMAN_TURN");
    exeTiredCore(::cTiredParam, GET_NERVE(BegomanSpike, HostTypeNrvWait));
}

void BegomanSpike::exeBlow() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_EM_BEGOMAN_ROT_STOP");
        MR::startBck(this, "Damage");
        MR::stopScene(::cStopSceneTime);
    }

    if (MR::isInWater(mPosition)) {
        setNerve(GET_NERVE(BegomanSpike, HostTypeNrvInWater));
        return;
    }

    MR::startLevelSound(this, "SE_EM_LV_BEGOMAN_SPARK");
    MR::startLevelSound(this, "SE_EM_LV_BEGOMAN_ROT_MIDDLE");
    MR::moveAndTurnToDirection(this, &mFaceVec, mTargetVec, ::cHitReactionParam);
    reboundWallAndGround(&mFaceVec, false);
    if (MR::isGreaterStep(this, ::cBlowFrame) && MR::isOnGround(this)) {
        MR::startBck(this, "Turn");
        mTurnAfterBlow = true;
        setNerve(GET_NERVE(BegomanSpike, HostTypeNrvTurn));
    }
}

void BegomanSpike::exeElectricDeath() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "electricshock");
        mVelocity.zero();
        getSensor("body")->invalidate();
        MR::startSound(this, "SE_EM_BEGOMAN_ELEC_DAMAGE");
        MR::invalidateClipping(this);
    }

    if (MR::isGreaterStep(this, ::cElectricDeathTime)) {
        MR::stopScene(::cDeathStopSceneTime);
        MR::shakeCameraWeak();
        MR::startSound(this, "SE_EM_EXPLODE_S");
        setNerve(GET_NERVE(BegomanSpike, HostTypeNrvBrokenPiece));
        MR::emitEffect(this, "Death");
    }
}

void BegomanSpike::exeBrokenPiece() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
        MR::hideModel(this);
        mBrokenModel->appear();
        MR::startBck(mBrokenModel, "Broken01");
        generateItem();
    }

    if (MR::isBckStopped(mBrokenModel)) {
        MR::showModel(this);
        kill();
    }
}

void BegomanSpike::exeInWater() {
    if (MR::isFirstStep(this)) {
        mVelocity += mGravity;
        mVelocity.setLength(::cInWaterVel);
        MR::startSound(this, "SE_EM_FALL_INTO_WATER_S");
    }

    if (MR::isGreaterStep(this, ::cInWaterTime)) {
        kill();
        MR::emitEffect(this, "DeathWater");
        MR::startSound(this, "SE_EM_BEGOMAN_DEAD_IN_WATER");
    }
}

void BegomanSpike::exeKeepDistance() {
    updateRotateY(::cSlowRotate, ::cCommonAddRotate);
    exeKeepDistanceCore(GET_NERVE(BegomanSpike, HostTypeNrvWait), GET_NERVE(BegomanSpike, HostTypeNrvSignAttack),
                        GET_NERVE(BegomanSpike, HostTypeNrvBrake), ::cKeepDistFar, ::cKeepDistNear);
}

void BegomanSpike::exeBindStarPointer() {
    BegomanBase::exeBindStarPointer();
}

void BegomanSpike::endBindStarPointer() {
    BegomanBase::finishBindStarPointer();
}

void BegomanSpike::generateItem() {
    MR::appearCoinPop(this, mPosition - mGravity * ::cCoinTransY, ::cCoinGenerateNum);
}

void BegomanSpike::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (pSender != getSensor("body")) {
        return;
    }

    if (MR::isSensorEnemy(pReceiver)) {
        MR::sendMsgToEnemyAttackBlow(pReceiver, pSender);
        return;
    }

    if (!MR::isSensorPlayer(pReceiver) || isNerve(GET_NERVE(BegomanSpike, HostTypeNrvBlow)) || MR::isPlayerSwingAction() || !MR::isOnGroundPlayer()) {
        return;
    }

    if (!MR::sendMsgEnemyAttackFlipRot(pReceiver, pSender) && !MR::sendMsgPush(pReceiver, pSender)) {
        return;
    }

    Vec3f away(pSender->mPosition);
    away -= pReceiver->mPosition;
    MR::normalizeOrZero(&away);

    if (!MR::isNearZero(away)) {
        const bool isReflected = reboundPlaneWithEffect(away, 0.0f, 0.0f, "Spark");
        mVelocity += away * ::cReboundPlayerAddVel;

        if (isReflected) {
            MR::startSound(this, "SE_EM_BEGOMAN_COLLI");
        }
    }

    setNerve(GET_NERVE(BegomanSpike, HostTypeNrvHitReaction));
}

bool BegomanSpike::receiveMsgPush(HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    return false;
}

bool BegomanSpike::receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (pReceiver != getSensor("body")) {
        return false;
    }

    if (MR::isMsgEnemyAttackElectric(msg)) {
        Vec3f direction(pSender->mHost->mVelocity);
        MR::vecKillElement(direction, mGravity, &direction);
        MR::normalizeOrZero(&direction);
        return onTouchElectric(pSender->mPosition, direction);
    }

    if (MR::isMsgExplosionAttack(msg) && !isNerve(GET_NERVE(BegomanSpike, HostTypeNrvBlow))) {
        calcBlowReaction(pSender->mPosition, pReceiver->mPosition, ::cExplosionBlowVel, ::cExplosionBlowVerticalVel);
        setNerve(GET_NERVE(BegomanSpike, HostTypeNrvBlow));
        return true;
    }

    if (!MR::isSensorEnemy(pSender)) {
        return false;
    }

    Vec3f away(pReceiver->mPosition - pSender->mPosition);
    MR::normalizeOrZero(&away);
    if (MR::isNearZero(away)) {
        return false;
    }

    // The push scales with the other's size.
    const bool isReflected = reboundPlaneWithEffect(away, 0.0f, 0.0f, "Spark");
    const f32 ratio = pSender->mRadius / pReceiver->mRadius;
    MR::addVelocityLimit(this, away * ::cReboundEnemyAddVel * ratio);
    if (isReflected) {
        MR::startSound(this, "SE_EM_BEGOMAN_COLLI_BEGOMAN");
    }

    if (!isNerve(GET_NERVE(BegomanSpike, HostTypeNrvProvoke)) && !isNerve(GET_NERVE(BegomanSpike, HostTypeNrvTrampleReaction)) &&
        !isNerve(GET_NERVE(BegomanSpike, HostTypeNrvBlow))) {
        MR::startBck(this, "Turn");
        setNerve(GET_NERVE(BegomanSpike, HostTypeNrvTurn));
    }

    return true;
}

bool BegomanSpike::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgLockOnStarPieceShoot(msg)) {
        return true;
    }

    if (MR::isMsgPlayerTrample(msg)) {
        if (isNrvBroken()) {
            return false;
        }

        // The spikes hurt the player and bounce them off.
        if (pReceiver->isType(ATYPE_BEGOMAN)) {
            MR::startSound(this, "SE_EM_TOGEGOMAN_STICK");
            MR::sendMsgEnemyAttack(pSender, pReceiver);
            if (!isNerve(GET_NERVE(BegomanSpike, HostTypeNrvTrampleReaction)) || !MR::isLessStep(this, ::cTrampleRepeatTime)) {
                setNerve(GET_NERVE(BegomanSpike, HostTypeNrvTrampleReaction));
            }
        }

        return false;
    }

    if (MR::isMsgPlayerHipDrop(msg)) {
        MR::forceJumpPlayer(-mGravity);
        return true;
    }

    if (isNrvBroken()) {
        return false;
    }

    if (pReceiver != getSensor("body") || (!MR::isMsgPlayerHitAll(msg) && !MR::isMsgStarPieceAttack(msg))) {
        return false;
    }

    Vec3f direction(pReceiver->mPosition);
    direction.sub(pSender->mPosition);
    Vec3f hitPos(-direction);
    hitPos.setLength(pReceiver->mRadius);
    hitPos.add(pReceiver->mPosition);
    MR::emitEffectHit(this, hitPos, "Hit");
    MR::startSound(this, "SE_EM_BEGOMAN_KNOCK_SUCCESS");
    finishNoCalcWait();
    setNerve(GET_NERVE(BegomanSpike, HostTypeNrvBlow));
    MR::vecKillElement(direction, mGravity, &direction);
    MR::normalize(&direction);
    mFaceVec.set(direction);
    direction *= ::cBlowVel;
    direction.sub(mGravity * ::cBlowVerticalVel);
    mVelocity = direction;
    return true;
}

bool BegomanSpike::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    return MR::isMsgHitmarkEmit(msg);
}

void BegomanSpike::calcAnim() {
    LiveActor::calcAnim();
    calcAnimCore(&mHeadMatrix);
}
