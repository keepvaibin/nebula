#include <nebula_game_api.h>
#include "Game/Enemy/BegomanSpring.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/BegomanBaby.hpp"
#include "Game/Enemy/BegomanSpringHead.hpp"
#include "Game/LiveActor/ActorCameraInfo.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/MultiEventCamera.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/System/GameRules.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvBegomanSpring {
    NEW_NERVE_ONEND(HostTypeNrvNoCalcWait, BegomanSpring, NoCalcWait, NoCalcWait);
    NEW_NERVE_ONEND(HostTypeNrvWait, BegomanSpring, Wait, Wait);
    NEW_NERVE(HostTypeNrvSignAttack, BegomanSpring, SignAttack);
    NEW_NERVE_ONEND(HostTypeNrvPursue, BegomanSpring, Pursue, Pursue);
    NEW_NERVE(HostTypeNrvTurn, BegomanSpring, Turn);
    NEW_NERVE(HostTypeNrvBrake, BegomanSpring, Brake);
    NEW_NERVE(HostTypeNrvStepBack, BegomanSpring, StepBack);
    NEW_NERVE(HostTypeNrvReturn, BegomanSpring, Return);
    NEW_NERVE(HostTypeNrvProvoke, BegomanSpring, Provoke);
    NEW_NERVE(HostTypeNrvShake, BegomanSpring, Shake);
    NEW_NERVE(HostTypeNrvTrample, BegomanSpring, Trample);
    NEW_NERVE(HostTypeNrvHitReaction, BegomanSpring, HitReaction);
    NEW_NERVE(HostTypeNrvTired, BegomanSpring, Tired);
    NEW_NERVE(HostTypeNrvBlow, BegomanSpring, Blow);
    NEW_NERVE(HostTypeNrvElectricDeath, BegomanSpring, ElectricDeath);
    NEW_NERVE_ONEND(HostTypeNrvHide, BegomanSpring, Hide, Hide);
    NEW_NERVE(HostTypeNrvHop, BegomanSpring, Hop);
    NEW_NERVE(HostTypeNrvInWater, BegomanSpring, InWater);
    NEW_NERVE(HostTypeNrvKeepDistance, BegomanSpring, KeepDistance);
    NEW_NERVE_ONEND(HostTypeNrvBindStarPointer, BegomanSpring, BindStarPointer, BindStarPointer);
};  // namespace NrvBegomanSpring

namespace {
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, cWaitParam, {0.0f, 3.0f, 0.95f, 1.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, cSignAttackParam, {0.0f, 3.0f, 0.8f, 3.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, cPursueParam, {0.4f, 3.0f, 0.97f, 3.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, cTurnParam, {0.4f, 3.0f, 0.95f, 1.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, cStopParam, {0.0f, 3.0f, 0.95f, 1.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, cHitReactionParam, {0.0f, 3.0f, 0.95f, 1.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, cTiredParam, {0.2f, 1.0f, 0.95f, 3.0f});

    NEBULA_TUNABLE(f32, cBinderRadius, 100.0f);
    NEBULA_TUNABLE(f32, cBodyRadius, 70.0f);
    NEBULA_TUNABLE(f32, cBodyHeight, 140.0f);
    // Trample sensor radius, times the placement's Y scale.
    NEBULA_TUNABLE(f32, cTrampleSensorRadius, 130.0f);
    NEBULA_TUNABLE(f32, cReboundEnemyAddVel, 8.0f);
    NEBULA_TUNABLE(f32, cPreventSwingbyDamp, 0.7f);
    NEBULA_TUNABLE(f32, cPreventSlopeOverDamp, 0.2f);
    NEBULA_TUNABLE(f32, cReboundPlayerAddVel, 2.0f);
    // The trample camera ends once the player is this far from where it started.
    NEBULA_TUNABLE(f32, cCameraLimitLength, 2500.0f);
    NEBULA_TUNABLE(f32, cCoinTransY, 80.0f);
    NEBULA_TUNABLE(s32, cCoinGenerateNum, 1);
    // Spin speed: each state's rate, plus cCommonAddRotate.
    NEBULA_TUNABLE(f32, cCommonAddRotate, 0.5f);
    NEBULA_TUNABLE(f32, cNoCalcWaitRotate, 0.5f);
    NEBULA_TUNABLE(f32, cWaitRotate, 0.15f);
    NEBULA_TUNABLE(f32, cSignAttackRotate, 0.5f);
    NEBULA_TUNABLE(f32, cPursueRotate, 0.5f);
    NEBULA_TUNABLE(f32, cTurnRotate, 0.4f);
    NEBULA_TUNABLE(f32, cBrakeRotate, 0.2f);
    NEBULA_TUNABLE(f32, cProvokeRotate, 0.3f);
    NEBULA_TUNABLE(f32, cShakeRotate, 0.17f);
    NEBULA_TUNABLE(f32, cTrampleRotate, 0.25f);
    NEBULA_TUNABLE(f32, cHitReactionRotate, 0.25f);
    // Step back, return, tired and keep distance.
    NEBULA_TUNABLE(f32, cSlowRotate, 0.2f);
    const s32 cShakeTime = NebulaFrameClock::stepsOfSeconds(3);
    const s32 cTrampleTime = NebulaFrameClock::stepsOfSeconds(3);
    NEBULA_TUNABLE(s32, cStopSceneTime, 2);
    NEBULA_TUNABLE(s32, cBlowFrame, 20);
    NEBULA_TUNABLE(f32, cBlowVel, 40.0f);
    NEBULA_TUNABLE(f32, cBlowVerticalVel, 10.0f);
    NEBULA_TUNABLE(f32, cExplosionBlowVel, 35.0f);
    NEBULA_TUNABLE(f32, cExplosionBlowVerticalVel, 15.0f);
    NEBULA_TUNABLE(s32, cDeathStopSceneTime, 5);
    NEBULA_TUNABLE(s32, cElectricDeathTime, 20);
    NEBULA_TUNABLE(f32, cBabyLaunchVelH, 10.0f);
    NEBULA_TUNABLE(f32, cBabyLaunchVelV, 50.0f);
    // A hidden one hops out once the player's center is this close.
    NEBULA_TUNABLE(f32, cHideAppearDistance, 1000.0f);
    NEBULA_TUNABLE(f32, cHopInitRotate, 0.5f);
    NEBULA_TUNABLE(f32, cHopRotateBrakeTime, 44.0f);
    // Star pieces bounce off while hidden and for this many steps of the hop.
    NEBULA_TUNABLE(s32, cHopStarPieceReflectTime, 40);
    NEBULA_TUNABLE(f32, cInWaterVel, 10.0f);
    const s32 cInWaterTime = NebulaFrameClock::stepsOfSeconds(1);
    NEBULA_TUNABLE(f32, cKeepDistFar, 800.0f);
    NEBULA_TUNABLE(f32, cKeepDistNear, 600.0f);
}  // namespace

BegomanSpring::BegomanSpring(const char* pName)
    : BegomanBase(pName), mEventCamera(nullptr), mHead(nullptr), mCameraStartPos(0.0f, 0.0f, 0.0f), mIsCameraActive(false), mCameraInfo(nullptr),
      mIsHideType(false), mBaby(nullptr), mHidePos(0.0f, 0.0f, 0.0f) {
    lumacore::setClass(this, lumacore::kClassId< BegomanSpring >);
    mHeadMatrix.identity();
    mWaterColumnMatrix.identity();
}

void BegomanSpring::init(const TableIter& rIter) {
    initCore(rIter, "BegomanSpring", false);
    mHidePos.set(mPosition);
    const char* pObjectName = "";
    initUseSwitchB(rIter, MR::Functor(this, &BegomanSpring::kill));
    if (MR::isValidInfo(rIter)) {
        MR::getObjectName(&pObjectName, rIter);
        initEventCameras(rIter);
    }

    // The hide type waits buried at its placement and carries a baby it launches when shaken.
    mIsHideType = MR::isEqualString(pObjectName, "BegomanSpringHide");
    if (mIsHideType) {
        setName("隠れバネベーゴマン");
        mBaby = new BegomanBaby("隠れバネベーゴマンベビー");
        mBaby->mPosition.set(mPosition);
        mBaby->initWithoutIter();
        mBaby->makeActorDead();
    }

    MR::declareCoin(this, ::cCoinGenerateNum);
    initBinder(::cBinderRadius, ::cBinderRadius, 0);
    if (mIsHideType) {
        initNerve(GET_NERVE(BegomanSpring, HostTypeNrvHide));
    } else {
        initNerve(GET_NERVE(BegomanSpring, HostTypeNrvWait));
    }

    initEffectAndStarPointerBind();
    MR::setEffectHostMtx(this, "WaterColumn", mWaterColumnMatrix);
    mHead = new BegomanSpringHead(this, mHeadMatrix);
    mHead->mHostScale = &mAnimScale;
    mHead->initWithoutIter();
    initSensor(1, ::cBodyRadius, ::cBodyHeight, "Center");
    MR::addHitSensorMtx(this, "trample", ATYPE_PLAYER_AUTO_JUMP, 8, ::cTrampleSensorRadius * mScale.y, MR::getJointMtx(mHead, "SpringJoint5"),
                        Vec3f(0.0f, 0.0f, 0.0f));
    initSound(8, false);
    initShadow(::cBodyRadius, "Locator1");
    if (MR::tryRegisterDemoCast(this, rIter)) {
        MR::tryRegisterDemoCast(mHead, rIter);
    }

    if (MR::isDemoCast(this, nullptr) && MR::isRegisteredDemoActionAppear(this)) {
        makeActorDead();
    } else {
        makeActorAppeared();
    }
}

void BegomanSpring::initEventCameras(const TableIter& rIter) {
    mCameraInfo = new ActorCameraInfo(rIter);
    if (mCameraInfo->mCameraSetID != -1) {
        mEventCamera = new MultiEventCamera();
        mEventCamera->setUp(mName, mCameraInfo, 2);
        mEventCamera->setEndCameraTypeSoon();
    }
}

void BegomanSpring::appear() {
    BegomanBase::appear();
    MR::emitEffect(this, "Death");
    MR::startSound(this, "SE_EM_BEGOMAN_APPEAR");
    setNerve(GET_NERVE(BegomanSpring, HostTypeNrvWait));
    mHead->forceWaitImmediately();
}

void BegomanSpring::makeActorDead() {
    BegomanBase::makeActorDead();
    if (mIsCameraActive) {
        endCamera();
        mIsCameraActive = false;
    }
}

void BegomanSpring::kill() {
    BegomanBase::kill();
    MR::emitEffect(this, "Death");
    MR::startSound(this, "SE_EM_EXPLODE_S");
}

void BegomanSpring::control() {
    if (isNerve(GET_NERVE(BegomanSpring, HostTypeNrvPursue)) || isNerve(GET_NERVE(BegomanSpring, HostTypeNrvTurn))) {
        if (incAndCheckTiredCounter()) {
            setNerve(GET_NERVE(BegomanSpring, HostTypeNrvTired));
            return;
        }
    } else {
        mTiredCounter = 0;
    }

    mHead->mGravity.set(mGravity);
    if (isNerve(GET_NERVE(BegomanSpring, HostTypeNrvBlow))) {
        checkTouchElectricRail(true);
    } else if (!isNerve(GET_NERVE(BegomanSpring, HostTypeNrvElectricDeath))) {
        checkTouchElectricRail(false);
    }

    updateCamera();
    getSensor("check")->invalidate();

    // A hide type out of view, with its hiding place also out of view, goes back into hiding.
    if (mIsHideType) {
        const f32 radius = getSensor("body")->mRadius;
        if (MR::isJudgedToClipFrustum(mPosition, radius) && MR::isJudgedToClipFrustum(mHidePos, radius)) {
            mPosition.set(mHidePos);
            setNerve(GET_NERVE(BegomanSpring, HostTypeNrvHide));
            return;
        }
    }

    if (!isNerve(GET_NERVE(BegomanSpring, HostTypeNrvInWater)) && !isNerve(GET_NERVE(BegomanSpring, HostTypeNrvNoCalcWait)) &&
        isInWaterAndSetWaterNerve(GET_NERVE(BegomanSpring, HostTypeNrvInWater), &mWaterColumnMatrix)) {
        return;
    }

    if (!GameRules::begomanFireProof() &&MR::isBindedGroundDamageFire(this)) {
        makeActorDead();
        MR::emitEffect(this, "Fall");
        return;
    }

    if (MR::isInDeath(this, Vec3f(0.0f, 0.0f, 0.0f)) || MR::isPressedRoofAndGround(this)) {
        kill();
        return;
    }

    if (isNerve(GET_NERVE(BegomanSpring, HostTypeNrvBlow))) {
        preventSwingby(::cPreventSwingbyDamp);
        preventSlopeBlow(::cPreventSlopeOverDamp);
    }

    if (!isNrvStarPointerBindable() || !tryAndSetStarPointerBind(GET_NERVE(BegomanSpring, HostTypeNrvBindStarPointer))) {
        BegomanBase::control();
    }
}

bool BegomanSpring::isNrvStarPointerBindable() const {
    return isNerve(GET_NERVE(BegomanSpring, HostTypeNrvNoCalcWait)) || isNerve(GET_NERVE(BegomanSpring, HostTypeNrvWait)) ||
           isNerve(GET_NERVE(BegomanSpring, HostTypeNrvSignAttack)) || isNerve(GET_NERVE(BegomanSpring, HostTypeNrvPursue)) ||
           isNerve(GET_NERVE(BegomanSpring, HostTypeNrvTurn)) || isNerve(GET_NERVE(BegomanSpring, HostTypeNrvBrake)) ||
           isNerve(GET_NERVE(BegomanSpring, HostTypeNrvReturn)) || isNerve(GET_NERVE(BegomanSpring, HostTypeNrvProvoke)) ||
           isNerve(GET_NERVE(BegomanSpring, HostTypeNrvKeepDistance));
}

bool BegomanSpring::isNrvBroken() const {
    return isNerve(GET_NERVE(BegomanSpring, HostTypeNrvBlow)) || isNerve(GET_NERVE(BegomanSpring, HostTypeNrvElectricDeath));
}

bool BegomanSpring::onTouchElectric(const Vec3f& rPos, const Vec3f& rDirection) {
    if (isNerve(GET_NERVE(BegomanSpring, HostTypeNrvElectricDeath)) || isNerve(GET_NERVE(BegomanSpring, HostTypeNrvHide)) ||
        isNerve(GET_NERVE(BegomanSpring, HostTypeNrvHop)) || isNerve(GET_NERVE(BegomanSpring, HostTypeNrvInWater))) {
        return false;
    }

    if (isNerve(GET_NERVE(BegomanSpring, HostTypeNrvBlow))) {
        setNerve(GET_NERVE(BegomanSpring, HostTypeNrvElectricDeath));
        return false;
    }

    if (isNerve(GET_NERVE(BegomanSpring, HostTypeNrvStepBack)) && !MR::isOnGround(this)) {
        return false;
    }

    if (BegomanBase::onTouchElectric(rPos, rDirection)) {
        setNerve(GET_NERVE(BegomanSpring, HostTypeNrvStepBack));
        return true;
    }

    return false;
}

bool BegomanSpring::setNerveReturn() {
    setNerve(GET_NERVE(BegomanSpring, HostTypeNrvReturn));
    return true;
}

const Nerve* BegomanSpring::getNerveWait() {
    return GET_NERVE(BegomanSpring, HostTypeNrvWait);
}

void BegomanSpring::exeNoCalcWait() {
    exeNoCalcWaitCore(::cNoCalcWaitRotate, GET_NERVE(BegomanSpring, HostTypeNrvWait));
}

void BegomanSpring::endNoCalcWait() {
    finishNoCalcWait();
}

void BegomanSpring::exeWait() {
    updateRotateY(::cWaitRotate, ::cCommonAddRotate);
    MR::startLevelSound(this, "SE_EM_LV_BEGOMAN_ROT_SLOW");
    exeWaitCore(::cWaitParam, GET_NERVE(BegomanSpring, HostTypeNrvSignAttack), GET_NERVE(BegomanSpring, HostTypeNrvKeepDistance),
                GET_NERVE(BegomanSpring, HostTypeNrvNoCalcWait));
}

void BegomanSpring::endWait() {
    MR::invalidateClipping(this);
}

void BegomanSpring::exeSignAttack() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "SignAttack");
        MR::startSound(this, "SE_EM_BEGOMAN_PRE_PURSUE");
    }

    updateRotateY(::cSignAttackRotate, ::cCommonAddRotate);
    MR::startLevelSound(this, "SE_EM_LV_BEGOMAN_ROT_MIDDLE");
    exeSignAttackCore(::cSignAttackParam, GET_NERVE(BegomanSpring, HostTypeNrvPursue));
}

void BegomanSpring::exePursue() {
    if (MR::isFirstStep(this)) {
        MR::emitEffect(this, "AttackBlur");
        MR::startSound(this, "SE_EM_BEGOMAN_PURSUE_START");
    }

    updateRotateY(::cPursueRotate, ::cCommonAddRotate);
    MR::startLevelSound(this, "SE_EM_LV_BEGOMAN_PURSUE");
    exePursueCore(::cPursueParam, GET_NERVE(BegomanSpring, HostTypeNrvBrake), GET_NERVE(BegomanSpring, HostTypeNrvTurn), *getSoundNormal(), 1.0f);
}

void BegomanSpring::endPursue() {
    MR::deleteEffect(this, "AttackBlur");
}

void BegomanSpring::exeTurn() {
    if (MR::isFirstStep(this)) {
        mHead->tryHopEnd();
    }

    updateRotateY(::cTurnRotate, ::cCommonAddRotate);
    MR::startLevelSound(this, "SE_EM_LV_BEGOMAN_ROT_MIDDLE");
    MR::startLevelSound(this, "SE_EM_LV_BEGOMAN_TURN");
    exeTurnCore(::cTurnParam, GET_NERVE(BegomanSpring, HostTypeNrvBrake), GET_NERVE(BegomanSpring, HostTypeNrvPursue), false);
}

void BegomanSpring::exeBrake() {
    updateRotateY(::cBrakeRotate, ::cCommonAddRotate);
    MR::startLevelSound(this, "SE_EM_LV_BEGOMAN_SPARK");
    MR::startLevelSound(this, "SE_EM_LV_BEGOMAN_ROT_MIDDLE");
    exeBrakeCore(GET_NERVE(BegomanSpring, HostTypeNrvTurn));
}

void BegomanSpring::exeStepBack() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "electricshock");
    }

    updateRotateY(::cSlowRotate, ::cCommonAddRotate);
    exeStepBackCore(::cWaitParam, GET_NERVE(BegomanSpring, HostTypeNrvWait));
}

void BegomanSpring::exeReturn() {
    updateRotateY(::cSlowRotate, ::cCommonAddRotate);
    exeReturnCore(GET_NERVE(BegomanSpring, HostTypeNrvWait));
}

void BegomanSpring::exeProvoke() {
    updateRotateY(::cProvokeRotate, ::cCommonAddRotate);
    MR::startLevelSound(this, "SE_EM_LV_BEGOMAN_ROT_MIDDLE");
    exeProvokeCore(::cWaitParam, GET_NERVE(BegomanSpring, HostTypeNrvSignAttack));
}

// Keeps only the vertical part of the velocity while the spring head bounces.
void BegomanSpring::keepVelocityAlongGravity() {
    mVelocity.scale(mGravity.dot(mVelocity), mGravity);
}

// The head's hop-end animation starts so that it finishes on the last step.
void BegomanSpring::tryHopEndBefore(s32 endStep) {
    if (MR::isStep(this, endStep - mHead->getHopEndBckFrameMax())) {
        MR::startBck(this, "HopEnd");
        mHead->tryHopEnd();
    }
}

void BegomanSpring::exeShake() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Shake");
        if (!mHead->isSpringHop()) {
            mHead->tryHopStart();
        }

        if (mIsHideType && MR::isDead(mBaby)) {
            mBaby->mPosition.set(mPosition);
            Vec3f side(mFaceVec);
            side.cross(mGravity, side);
            mBaby->mVelocity.set(-mGravity * ::cBabyLaunchVelV - side * ::cBabyLaunchVelH);
            mBaby->appear();
            MR::startSound(this, "SE_EM_BEGOMAN_LAUNCH_BABY");
        }
    }

    updateRotateY(::cShakeRotate, ::cCommonAddRotate);
    MR::startLevelSound(this, "SE_EM_LV_BEGOMAN_SHAKE");
    MR::moveAndTurnToDirection(this, &mFaceVec, mTargetVec, ::cWaitParam);
    reboundWallAndGround(&mFaceVec, false);
    keepVelocityAlongGravity();
    tryHopEndBefore(::cShakeTime);

    if (MR::isGreaterStep(this, ::cShakeTime)) {
        setNerve(GET_NERVE(BegomanSpring, HostTypeNrvWait));
    }
}

void BegomanSpring::exeTrample() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Shake");
        mHead->tryHopJump();
        MR::startSound(this, "SE_EM_BEGOMAN_TRAMPLE");
    }

    updateRotateY(::cTrampleRotate, ::cCommonAddRotate);
    MR::moveAndTurnToDirection(this, &mFaceVec, mTargetVec, ::cStopParam);
    reboundWallAndGround(&mFaceVec, false);
    keepVelocityAlongGravity();
    tryHopEndBefore(::cTrampleTime);

    if (MR::isGreaterStep(this, ::cTrampleTime)) {
        setNerve(GET_NERVE(BegomanSpring, HostTypeNrvSignAttack));
    }
}

void BegomanSpring::exeHitReaction() {
    if (MR::isFirstStep(this)) {
        mHead->tryHopEnd();
    }

    updateRotateY(::cHitReactionRotate, ::cCommonAddRotate);
    exeHitReactionCore(::cHitReactionParam, GET_NERVE(BegomanSpring, HostTypeNrvProvoke));
}

void BegomanSpring::exeTired() {
    updateRotateY(::cSlowRotate, ::cCommonAddRotate);
    MR::startLevelSound(this, "SE_EM_LV_BEGOMAN_TURN");
    exeTiredCore(::cTiredParam, GET_NERVE(BegomanSpring, HostTypeNrvWait));
}

void BegomanSpring::exeBlow() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_EM_BEGOMAN_ROT_STOP");
        MR::startBck(this, "Damage");
        MR::stopScene(::cStopSceneTime);
        mHead->tryHopStart();
    }

    MR::startLevelSound(this, "SE_EM_LV_BEGOMAN_SPARK");
    MR::startLevelSound(this, "SE_EM_LV_BEGOMAN_ROT_MIDDLE");
    MR::moveAndTurnToDirection(this, &mFaceVec, mTargetVec, ::cHitReactionParam);
    reboundWallAndGround(&mFaceVec, false);
    tryHopEndBefore(::cBlowFrame);

    if (MR::isGreaterStep(this, ::cBlowFrame) && MR::isOnGround(this)) {
        MR::startBck(this, "Turn");
        setNerve(GET_NERVE(BegomanSpring, HostTypeNrvShake));
    }
}

void BegomanSpring::exeElectricDeath() {
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
        kill();
        generateItem();
    }
}

void BegomanSpring::exeHide() {
    if (MR::isFirstStep(this)) {
        MR::offBind(this);
        MR::offCalcGravity(this);
        MR::onCalcShadowOneTime(this, nullptr);
        MR::validateClipping(this);
        MR::startBck(this, "Hide");
        mHead->tryHopEnd();
        MR::deleteEffect(this, "OnGroundSpark");
    }

    updateRotateY(0.0f, 1.0f);
    mVelocity.zero();
    if (MR::getPlayerCenterPos()->squared(mPosition) < ::cHideAppearDistance * ::cHideAppearDistance) {
        MR::onBind(this);
        MR::onCalcGravity(this);
        MR::onCalcShadow(this, nullptr);
        setNerve(GET_NERVE(BegomanSpring, HostTypeNrvHop));
    }
}

void BegomanSpring::endHide() {
    MR::invalidateClipping(this);
}

void BegomanSpring::exeHop() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Appear");
        MR::startSound(this, "SE_EM_BEGOMAN_FLYOUT");
        updateRotateY(::cHopInitRotate, ::cCommonAddRotate);
    }

    updateRotateY(0.0f, 1.0f / ::cHopRotateBrakeTime);
    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(BegomanSpring, HostTypeNrvWait));
    }
}

void BegomanSpring::exeInWater() {
    if (MR::isFirstStep(this)) {
        mVelocity.add(mGravity);
        mVelocity.setLength(::cInWaterVel);
        MR::startSound(this, "SE_EM_FALL_INTO_WATER_S");
    }

    if (MR::isGreaterStep(this, ::cInWaterTime)) {
        makeActorDead();
        MR::emitEffect(this, "DeathWater");
        MR::startSound(this, "SE_EM_BEGOMAN_DEAD_IN_WATER");
    }
}

void BegomanSpring::exeKeepDistance() {
    updateRotateY(::cSlowRotate, ::cCommonAddRotate);
    exeKeepDistanceCore(GET_NERVE(BegomanSpring, HostTypeNrvWait), GET_NERVE(BegomanSpring, HostTypeNrvSignAttack),
                        GET_NERVE(BegomanSpring, HostTypeNrvBrake), ::cKeepDistFar, ::cKeepDistNear);
}

void BegomanSpring::exeBindStarPointer() {
    BegomanBase::exeBindStarPointer();
}

void BegomanSpring::endBindStarPointer() {
    BegomanBase::finishBindStarPointer();
}

void BegomanSpring::generateItem() {
    MR::appearCoinPop(this, mPosition - mGravity * ::cCoinTransY, ::cCoinGenerateNum);
}

void BegomanSpring::startCamera() {
    if (mEventCamera != nullptr) {
        mEventCamera->start(0);
    }
}

void BegomanSpring::updateCamera() {
    if (!mIsCameraActive || mEventCamera == nullptr || mEventCamera->isEnd()) {
        return;
    }

    mEventCamera->update();
    if (mEventCamera->isActive(0) && !MR::isPlayerJumpRising()) {
        mEventCamera->nextForce();
    }

    if (::cCameraLimitLength * ::cCameraLimitLength < mCameraStartPos.squared(*MR::getPlayerCenterPos()) || MR::isOnGroundPlayer() ||
        MR::isPlayerInRush()) {
        endCamera();
        mIsCameraActive = false;
    }
}

void BegomanSpring::endCamera() {
    if (mEventCamera != nullptr) {
        mEventCamera->endForceSoon();
    }
}

void BegomanSpring::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (pSender != getSensor("body")) {
        return;
    }

    if (MR::isSensorEnemy(pReceiver)) {
        MR::sendMsgToEnemyAttackBlow(pReceiver, pSender);
        return;
    }

    if (!MR::isSensorPlayer(pReceiver) || isNerve(GET_NERVE(BegomanSpring, HostTypeNrvBlow)) || MR::isPlayerSwingAction() ||
        !MR::isOnGroundPlayer()) {
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

    setNerve(GET_NERVE(BegomanSpring, HostTypeNrvHitReaction));
}

bool BegomanSpring::receiveMsgPush(HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    return false;
}

bool BegomanSpring::receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (pReceiver != getSensor("body")) {
        return false;
    }

    if (MR::isMsgEnemyAttackElectric(msg)) {
        Vec3f direction(pSender->mHost->mVelocity);
        MR::vecKillElement(direction, mGravity, &direction);
        MR::normalizeOrZero(&direction);
        return onTouchElectric(pSender->mPosition, direction);
    }

    if (isNerve(GET_NERVE(BegomanSpring, HostTypeNrvShake))) {
        return false;
    }

    if (MR::isMsgExplosionAttack(msg) && !isNerve(GET_NERVE(BegomanSpring, HostTypeNrvBlow))) {
        calcBlowReaction(pSender->mPosition, pReceiver->mPosition, ::cExplosionBlowVel, ::cExplosionBlowVerticalVel);
        setNerve(GET_NERVE(BegomanSpring, HostTypeNrvBlow));
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

    if (!isNerve(GET_NERVE(BegomanSpring, HostTypeNrvTrample)) && !isNerve(GET_NERVE(BegomanSpring, HostTypeNrvShake)) &&
        !isNerve(GET_NERVE(BegomanSpring, HostTypeNrvProvoke)) && !isNerve(GET_NERVE(BegomanSpring, HostTypeNrvBlow))) {
        MR::startBck(this, "Turn");
        setNerve(GET_NERVE(BegomanSpring, HostTypeNrvTurn));
    }

    return true;
}

bool BegomanSpring::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgLockOnStarPieceShoot(msg)) {
        return true;
    }

    if (MR::isMsgStarPieceReflect(msg) && (isNerve(GET_NERVE(BegomanSpring, HostTypeNrvHide)) ||
                                           (isNerve(GET_NERVE(BegomanSpring, HostTypeNrvHop)) && MR::isLessStep(this, ::cHopStarPieceReflectTime)))) {
        return true;
    }

    if (MR::isMsgPlayerTrample(msg)) {
        if (isNrvBroken()) {
            return false;
        }

        // The first trample on the body sets it shaking; while it shakes the spring launches the player.
        if (!isNerve(GET_NERVE(BegomanSpring, HostTypeNrvShake))) {
            if (pReceiver->isType(ATYPE_BEGOMAN)) {
                setNerve(GET_NERVE(BegomanSpring, HostTypeNrvShake));
                return true;
            }

            return false;
        }

        if (!pReceiver->isType(ATYPE_PLAYER_AUTO_JUMP)) {
            return false;
        }

        if (!mIsCameraActive) {
            startCamera();
            mIsCameraActive = true;
            mCameraStartPos.set(mPosition);
        }

        setNerve(GET_NERVE(BegomanSpring, HostTypeNrvTrample));
        return true;
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
    setNerve(GET_NERVE(BegomanSpring, HostTypeNrvBlow));
    MR::vecKillElement(direction, mGravity, &direction);
    MR::normalize(&direction);
    mFaceVec.set(direction);
    direction *= ::cBlowVel;
    direction.sub(mGravity * ::cBlowVerticalVel);
    mVelocity = direction;
    return true;
}

bool BegomanSpring::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    return MR::isMsgHitmarkEmit(msg);
}

void BegomanSpring::calcAnim() {
    LiveActor::calcAnim();
    calcAnimCore(&mHeadMatrix);
}
