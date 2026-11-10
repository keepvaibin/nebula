#include "Game/Boss/Dodoryu.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Boss/DodoryuDemo.hpp"
#include "Game/Boss/DodoryuHill.hpp"
#include "Game/Boss/DodoryuMove.hpp"
#include "Game/Boss/DodoryuStateBase.hpp"
#include "Game/Boss/DodoryuStateLv1.hpp"
#include "Game/Boss/DodoryuStateLv2.hpp"
#include "Game/Boss/DodoryuStateWait.hpp"
#include "Game/Camera/CameraTargetArg.hpp"
#include "Game/Camera/CameraTargetMtx.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/JointController.hpp"
#include "Game/Util/JointRumbler.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/TalkUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_DodoryuRabbit_f1, "DodoryuRabbit.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_DodoryuRabbit_f2, "DodoryuRabbit.f2", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_DodoryuRabbit_f3, "DodoryuRabbit.f3", 100.0f);

namespace {
    NEBULA_TUNABLE_RECORD(Vec, sHeadOffset, {50.0f, 30.0f, 0.0f});
    NEBULA_TUNABLE_RECORD(Vec, sBodyOffset, {0.0f, 0.0f, 30.0f});
    NEBULA_TUNABLE_RECORD(Vec, sLegOffset, {40.0f, 0.0f, 0.0f});
    NEBULA_TUNABLE_RECORD(Vec, sArmOffset, {0.0f, 0.0f, 0.0f});
    NEBULA_TUNABLE_RECORD(Vec, sTailOffset, {50.0f, 0.0f, 0.0f});
    NEBULA_TUNABLE_RECORD(Vec, sStarPieceSensorOffset, {-150.0f, 0.0f, 0.0f});
    const char* const sHeadSensorName = "head";
    const char* const sHeadJointName = "Head";
    NEBULA_TUNABLE(f32, sHeadRadius, 210.0f);
    const char* const sBodySensorName = "body";
    NEBULA_TUNABLE(f32, sBodyRadius, 250.0f);
    const char* const sLegLSensorName = "leg_left";
    const char* const sLegRSensorName = "leg_right";
    NEBULA_TUNABLE(f32, sLegRadius, 110.0f);
    const char* const sArmLSensorName = "arm_left";
    const char* const sArmRSensorName = "arm_right";
    NEBULA_TUNABLE(f32, sArmRadius, 50.0f);
    const char* const sTailSensorName = "tail";
    NEBULA_TUNABLE(f32, sTailRadius, 120.0f);
    const char* const sMapObjSensorName = "eye";
    NEBULA_TUNABLE(f32, sMapObjSensorRadius, 100.0f);
    const char* const sStarPieceSensorName = "star";
    NEBULA_TUNABLE(f32, sStarPieceSensorRadius, 400.0f);
    NEBULA_TUNABLE(s32, sSensorCount, 9);
    NEBULA_TUNABLE(u16, sEnemySensorGroupSize, 8);
    NEBULA_TUNABLE(u16, sMapObjSensorGroupSize, 16);
    // Along the head joint's x axis: a reflect from further out hit the needle.
    NEBULA_TUNABLE(f32, sHeadNeedleReach, 105.0f);
    NEBULA_TUNABLE(s32, sJumpOutStopSceneFrame, 8);
    const char* const sSpinOutCamera = "ドドリュウ吹っ飛び";
    const s32 sSpinOutCameraSteps = NebulaFrameClock::stepsOfSeconds(2);
    NEBULA_TUNABLE(f32, sRumblePadNearDist, 500.0f);
    NEBULA_TUNABLE(f32, sRumblePadFarDist, 1000.0f);
    NEBULA_TUNABLE(f32, sCameraRegisterHeight, 150.0f);
    NEBULA_TUNABLE(f32, sCameraHipWeight, 0.7f);
    NEBULA_TUNABLE(f32, sCameraPlayerWeight, 0.3f);
    NEBULA_TUNABLE(f32, sGroundProbeRise, 200.0f);
    NEBULA_TUNABLE(f32, sGroundProbeLength, 1200.0f);
    NEBULA_TUNABLE(f32, sStateStartBlur, 0.75f);
    NEBULA_TUNABLE(s32, sStarPieceCount, 24);
    NEBULA_TUNABLE(s32, sSoundCount, 8);
    NEBULA_TUNABLE(s32, sHillStartPieces, 5);
    // Pieces left off the hill at its fullest.
    NEBULA_TUNABLE(s32, sHillMaxSpare, 10);
    NEBULA_TUNABLE(f32, sRabbitDistMax, 1500.0f);
    NEBULA_TUNABLE(f32, sRabbitDistMin, 650.0f);
    NEBULA_TUNABLE(f32, sRabbitRestDist, 2000.0f);
    NEBULA_TUNABLE(f32, sRabbitFastSpeed, 8.0f);
    NEBULA_TUNABLE(f32, sRabbitSlowSpeed, 4.0f);
    // Rail distance the rabbit starts ahead of Dodoryu.
    NEBULA_TUNABLE(f32, sRabbitLead, 1075.0f);
    NEBULA_TUNABLE(f32, sRabbitHopSoundFrame, 3.0f);
    const s32 sRabbitTalkSwitchSteps = NebulaFrameClock::stepsOfSeconds(5);
    NEBULA_TUNABLE(s32, sRabbitTalkNodeCount, 2);
    NEBULA_TUNABLE(s32, sRabbitPleasureTalkNode, 2);
    NEBULA_TUNABLE(f32, sRabbitTalkDistance, 1500.0f);
    NEBULA_TUNABLE(f32, sRabbitPleasureTalkDistance, 350.0f);
    // Pleasure stops following the spine joint after this step.
    const s32 sRabbitPleasureTalkFollowSteps = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(f32, sRabbitBodyRadius, 50.0f);
    NEBULA_TUNABLE(f32, sRabbitRumbleRadius, 4.0f);
    NEBULA_TUNABLE(f32, sRabbitRumbleSpeed, 0.2f);
    const s32 sRabbitTrampleRumbleSteps = NebulaFrameClock::stepsOfSeconds(1);
    const s32 sRabbitStarPieceRumbleSteps = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(f32, sLeadHillSpinSpeed, 0.07f);

    const DodoryuChaseParam sDodoryuChaseDataLv2 = {
        .mReadyChaseStep = NebulaFrameClock::stepsOfSeconds(1),
        .mUndergroundMaxSpeed = 16.0f,
        .mUndergroundAccel = 0.1f,
        .mUndergroundTurnAngle = 1.5f,
        .mUndergroundLateralDamping = 0.0f,
        .mChaseAppearDistance = 600.0f,
        .mAppearSignStep = NebulaFrameClock::stepsOfMilliseconds(500),
        .mExposingHeadMaxSpeed = 9.0f,
        .mExposingHeadAccel = 0.1f,
        .mExposingHeadTurnAngle = 1.0f,
        .mExposingHeadLateralDamping = 0.0f,
        .mReadyAppearStepMin = 45,
        .mReadyAppearStepMax = 55,
        .mChaseHideDistance = 800.0f,
        .mAppearSignFaceAngle = 180.0f,
        .mExposingBustMaxSpeed = 5.0f,
        .mExposingBustAccel = 0.2f,
        .mExposingBustTurnAngle = 2.0f,
        .mExposingBustLateralDamping = 0.0f,
        .mChaseAppearStepMin = NebulaFrameClock::stepsOfSeconds(2),
        .mChaseAppearStepMax = NebulaFrameClock::stepsOfMilliseconds(2500),
        .mChaseAppearEndStep = 5,
        .mEscapeStepMax = 250,
        .mEscapeEndDistance = 1500.0f,
        .mEscapeDirStepMin = 50,
        .mEscapeDirStepMax = 70,
        .mEscapeSpeed = 8.0f,
        .mEscapeDirAngleMax = 50.0f,
        .mEscapeTurnAngle = 1.5f,
        .mEscapeStepMin = NebulaFrameClock::stepsOfSeconds(1),
        .mLaughStep = NebulaFrameClock::stepsOfSeconds(2),
        .mFindPosStepMin = NebulaFrameClock::stepsOfSeconds(1),
        .mFindPosStepMax = NebulaFrameClock::stepsOfMilliseconds(1500),
        .mRandomMoveDistanceMin = 1200.0f,
        .mRandomMoveDistanceMax = 1500.0f,
        .mJumpOutSpeed = 5.0f,
        .mKnockDownStopStep = 80,
        .mKnockDownStep = -1,
        .mHasChaseMore = false,
        .mInAngerMaxSpeed = 10.0f,
        .mInAngerAccel = 1.0f,
        .mInAngerTurnAngle = 3.0f,
        .mInAngerLateralDamping = 0.92f,
        .mChaseMoreStep = NebulaFrameClock::stepsOfSeconds(6),
        .mChaseMoreAccelCounterMin = 50,
        .mChaseMoreAccelCounterMax = 60,
        .mChaseMoreBrakeCounterMin = 10,
        .mChaseMoreBrakeCounterMax = 20,
        // {bck, btk, brk, bva, lead hill bck}
        .mAnimSetStart = {"Burrow", nullptr, "Normal", "Normal", nullptr},
        .mAnimSetReadyChase = {"UnderGroundWalk", nullptr, "Normal", "Normal", nullptr},
        .mAnimSetUndergroundWalk = {"UnderGroundWalk", nullptr, nullptr, "Normal", nullptr},
        .mAnimSetReadyAppear = {"EffectWalkAppear", nullptr, "Normal", nullptr, "EffectWalkAppear"},
        .mAnimSetReadyAppearWalk = {"EffectWalk", nullptr, nullptr, nullptr, "EffectWalk"},
        .mAnimSetChaseHide = {"EffectWalkHide", nullptr, nullptr, nullptr, "EffectWalkHide"},
        .mAnimSetChaseAppearStart = {"Appear", nullptr, nullptr, "Careless", "Appear"},
        .mAnimSetChaseAppear = {"Walk", nullptr, nullptr, "Normal", "Walk"},
        .mAnimSetChaseAppearEnd = {"Hide", nullptr, nullptr, nullptr, "Hide"},
        .mAnimSetJumpOut = {"SwoonWalkStart", nullptr, nullptr, "Cry", nullptr},
        .mAnimSetEscape = {"SwoonWalk", nullptr, nullptr, nullptr, nullptr},
        .mAnimSetRecover = {"SwoonWalkEnd", nullptr, nullptr, nullptr, nullptr},
        .mAnimSetDive = {"Dive", nullptr, nullptr, "Normal", nullptr},
        .mAnimSetKnockDown = {"AngryPunchDamage", nullptr, nullptr, nullptr, nullptr},
        .mAnimSetLaugh = {"AttackHit", nullptr, nullptr, nullptr, "AttackHit"},
        .mAnimSetLaughEnd = {"Hide", nullptr, nullptr, nullptr, "Hide"},
        .mAnimSetHideJump = {"Jump", nullptr, nullptr, "Careless", "Jump"},
        .mAnimSetChaseMoreStart = {},
        .mAnimSetChaseMore = {},
        .mAnimSetChaseMoreEnd = {},
    };

    const DodoryuChaseParam sDodoryuChaseDataLv3 = {
        .mReadyChaseStep = NebulaFrameClock::stepsOfSeconds(1),
        .mUndergroundMaxSpeed = 20.0f,
        .mUndergroundAccel = 0.2f,
        .mUndergroundTurnAngle = 1.0f,
        .mUndergroundLateralDamping = 0.0f,
        .mChaseAppearDistance = 500.0f,
        .mAppearSignStep = NebulaFrameClock::stepsOfMilliseconds(500),
        .mExposingHeadMaxSpeed = 9.0f,
        .mExposingHeadAccel = 0.1f,
        .mExposingHeadTurnAngle = 1.0f,
        .mExposingHeadLateralDamping = 0.0f,
        .mReadyAppearStepMin = 55,
        .mReadyAppearStepMax = 65,
        .mChaseHideDistance = 750.0f,
        .mAppearSignFaceAngle = 180.0f,
        .mExposingBustMaxSpeed = 6.0f,
        .mExposingBustAccel = 0.2f,
        .mExposingBustTurnAngle = 2.0f,
        .mExposingBustLateralDamping = 0.99f,
        .mChaseAppearStepMin = NebulaFrameClock::stepsOfSeconds(1),
        .mChaseAppearStepMax = 75,
        .mChaseAppearEndStep = 5,
        .mEscapeStepMax = 140,
        .mEscapeEndDistance = 1500.0f,
        .mEscapeDirStepMin = 40,
        .mEscapeDirStepMax = NebulaFrameClock::stepsOfSeconds(1),
        .mEscapeSpeed = 9.0f,
        .mEscapeDirAngleMax = 70.0f,
        .mEscapeTurnAngle = 2.0f,
        .mEscapeStepMin = NebulaFrameClock::stepsOfSeconds(1),
        .mLaughStep = NebulaFrameClock::stepsOfSeconds(2),
        .mFindPosStepMin = NebulaFrameClock::stepsOfSeconds(1),
        .mFindPosStepMax = NebulaFrameClock::stepsOfMilliseconds(1500),
        .mRandomMoveDistanceMin = 1500.0f,
        .mRandomMoveDistanceMax = 1600.0f,
        .mJumpOutSpeed = 5.0f,
        .mKnockDownStopStep = -1,
        .mKnockDownStep = 45,
        .mHasChaseMore = true,
        .mInAngerMaxSpeed = 18.0f,
        .mInAngerAccel = 0.5f,
        .mInAngerTurnAngle = 0.1f,
        .mInAngerLateralDamping = 0.96f,
        .mChaseMoreStep = NebulaFrameClock::stepsOfSeconds(4),
        .mChaseMoreAccelCounterMin = 50,
        .mChaseMoreAccelCounterMax = 60,
        .mChaseMoreBrakeCounterMin = 10,
        .mChaseMoreBrakeCounterMax = 20,
        // {bck, btk, brk, bva, lead hill bck}
        .mAnimSetStart = {"Burrow", nullptr, "Angry", "Angry", nullptr},
        .mAnimSetReadyChase = {"UnderGroundWalk", nullptr, "Angry", "Angry", nullptr},
        .mAnimSetUndergroundWalk = {"UnderGroundWalk", nullptr, "Angry", "Angry", nullptr},
        .mAnimSetReadyAppear = {"AngryEffectWalkAppear", nullptr, nullptr, nullptr, "EffectWalkAppear"},
        .mAnimSetReadyAppearWalk = {"AngryEffectWalk", nullptr, nullptr, nullptr, "EffectWalk"},
        .mAnimSetChaseHide = {"AngryEffectWalkHide", nullptr, nullptr, nullptr, "EffectWalkHide"},
        .mAnimSetChaseAppearStart = {"AngryAppear", nullptr, nullptr, nullptr, "Appear"},
        .mAnimSetChaseAppear = {"AngryWalk", nullptr, nullptr, nullptr, "Walk"},
        .mAnimSetChaseAppearEnd = {"AngryHide", nullptr, nullptr, nullptr, "Hide"},
        .mAnimSetJumpOut = {"SwoonWalkStart", nullptr, "CoolDown", "Cry", nullptr},
        .mAnimSetEscape = {"SwoonWalk", nullptr, "Normal", nullptr, nullptr},
        .mAnimSetRecover = {"SwoonWalkEnd", nullptr, nullptr, nullptr, nullptr},
        .mAnimSetDive = {"AngryDive", nullptr, "AngryStart", "Angry", nullptr},
        .mAnimSetKnockDown = {"AngryPunchDamage", nullptr, nullptr, nullptr, nullptr},
        .mAnimSetLaugh = {"AngryAttackHit", nullptr, nullptr, nullptr, "AttackHit"},
        .mAnimSetLaughEnd = {"AngryHide", nullptr, nullptr, nullptr, "Hide"},
        .mAnimSetHideJump = {"AngryJump", nullptr, nullptr, "Cry", "Jump"},
        .mAnimSetChaseMoreStart = {"AngryChaseStart", nullptr, nullptr, "Cry", "AngryChaseStart"},
        .mAnimSetChaseMore = {"AngryChase", nullptr, nullptr, "Angry", "AngryChase"},
        .mAnimSetChaseMoreEnd = {"AngryChaseEnd", nullptr, nullptr, nullptr, "AngryChaseEnd"},
    };
};  // namespace

namespace {
    NEW_NERVE(DodoryuBankNrvBankAppear, DodoryuBank, Appear);
};  // namespace

namespace {
    NEW_NERVE(DodoryuRabbitNrvRabbitEscapeWaiting, DodoryuRabbit, EscapeWaiting);
    NEW_NERVE(DodoryuRabbitNrvRabbitEscape, DodoryuRabbit, Escape);
    NEW_NERVE(DodoryuRabbitNrvRabbitEscapeSlow, DodoryuRabbit, EscapeSlow);
    NEW_NERVE(DodoryuRabbitNrvRabbitRest, DodoryuRabbit, Rest);
    NEW_NERVE(DodoryuRabbitNrvRabbitJump, DodoryuRabbit, Jump);
    NEW_NERVE(DodoryuRabbitNrvRabbitWait, DodoryuRabbit, Wait);
    NEW_NERVE(DodoryuRabbitNrvRabbitReturn, DodoryuRabbit, Return);
    NEW_NERVE(DodoryuRabbitNrvRabbitPleasure, DodoryuRabbit, Pleasure);
};  // namespace

Dodoryu::Dodoryu(const char* pName)
    : LiveActor(pName), mCameraRegisterPos(0.0f, 0.0f, 0.0f), mStateIndex(), mState(), mMoveStateHolder(), mHill(), mLeadHill(), mBank(), mRabbit(),
      mGroundNormal(0.0f, 1.0f, 0.0f), mWallNormal(1.0f, 0.0f, 0.0f), mClosedAreaObj(), mIsSwitchOn(false), mDemoCameraTarget(new CameraTargetMtx("カメラターゲットダミー")),
      mSpinOutCameraTarget(new CameraTargetMtx("カメラターゲットダミー")), mIsRumbling(false), mAnimScaleCtrl(new AnimScaleController(nullptr)) {
    mBaseMtx.identity();
}

void Dodoryu::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);

    mScale.set(1.0f, 1.0f, 1.0f);

    initModelManagerWithAnm("Dodoryu", nullptr, false);

    mMoveStateHolder = new DodoryuSub::MoveStateHolder(this);

    initState();
    MR::connectToSceneEnemy(this);
    MR::initLightCtrl(this);
    MR::initFur(this);
    initHitSensor();
    initSound(::sSoundCount, false);
    initRailRider(rIter);
    MR::moveCoordToNearestPos(this, mPosition);
    initSwitch(rIter);
    initEffectKeeper(0, nullptr);
    MR::initShadowFromCSV(this, "Shadow");
    MR::declareCameraRegisterVec(this, 0, &mCameraRegisterPos);
    MR::declareGlobalEventCameraFixedThere(::sSpinOutCamera, true, 0.0f);
    createMogucchiHill();
    mLeadHill = new DodoryuLeadHill(this);
    createDodoryuBank();
    mRabbit = new DodoryuRabbit(this, rIter);
    MR::declarePowerStar(this);
    MR::createDepthOfFieldBlur();
    MR::declareStarPiece(this, ::sStarPieceCount);
    MR::createCenterScreenBlur();
    mDemoCameraTarget->mMatrix.setInline(MR::getZonePlacementMtx(rIter));
    MR::startBckWithInterpole(this, "UnderGroundWalk", 0);
    MR::startBva(this, "Normal");
    MR::startBrk(this, "Normal");
    mState[mStateIndex]->start();

    if (MR::isValidSwitchB(this)) {
        MR::onSwitchB(this);
    }

    makeActorAppeared();
}

void Dodoryu::initAfterPlacement() {
    snapToGround();
}

void Dodoryu::control() {
    mMoveStateHolder->execute();
    checkHipDrop();
    mState[mStateIndex]->movement();
    updateRumblePad();
    updateCameraTarget();
    mAnimScaleCtrl->updateNerve();
}

void Dodoryu::startClipped() {
    LiveActor::startClipped();
    MR::validateClipping(mRabbit);
    mState[mStateIndex]->catchStartClipped();
}

void Dodoryu::endClipped() {
    LiveActor::endClipped();
    MR::invalidateClipping(mRabbit);
    mState[mStateIndex]->catchEndClipped();
}

void Dodoryu::snapToGround() {
    MR::calcGravityVector(this, &mGravity, nullptr, 0);

    Triangle triangle;
    Vec3f groundPos;

    if (MR::getFirstPolyOnLineToMap(&groundPos, &triangle, mPosition - mGravity.scaleInline(::sGroundProbeRise), mGravity.scaleInline(::sGroundProbeLength))) {
        mGroundNormal = *triangle.getNormal(0);

        turnUpVecTo(-mGravity);
        mPosition.set(groundPos);
        mBaseMtx.setTrans(groundPos);
    }
}

bool Dodoryu::snapToWall() {
    Vec3f cylinderPos;
    MR::calcCylinderPos(&cylinderPos, mClosedAreaObj);

    Vec3f cylinderUpVec;
    MR::calcCylinderUpVec(&cylinderUpVec, mClosedAreaObj);

    f32 cylinderRadius = MR::getCylinderRadius(mClosedAreaObj);

    Vec3f toWall = mPosition - cylinderPos;
    toWall.orthogonalize(cylinderUpVec);

    if (MR::isNearZero(toWall)) {
        return false;
    }

    MR::normalize(&toWall);
    mWallNormal.set(toWall);
    toWall *= cylinderRadius;
    Vec3f wallPos = cylinderPos + toWall;
    mPosition.set(wallPos);
    mBaseMtx.setTrans(wallPos);

    return true;
}

void Dodoryu::setMtx(const Pos3f& rMtx) {
    mBaseMtx.setInline(&rMtx);
    mBaseMtx.getTrans(mPosition);

    mCameraRegisterPos = mPosition - mGravity * ::sCameraRegisterHeight;
}

void Dodoryu::notifyOnSwitchA() {
    MR::invalidateClipping(this);
    MR::invalidateClipping(mHill);
    MR::invalidateClipping(mLeadHill);
    MR::invalidateClipping(mBank);
    MR::invalidateClipping(mRabbit);
    mIsSwitchOn = true;
    mHill->setAppearNum(mHill->mPieceNum);
}

void Dodoryu::nextState() {
    mState[mStateIndex]->end();

    mStateIndex++;

    if (mStateIndex < static_cast< u32 >(mState.size())) {
        MR::turnOnDepthOfField(true);
        MR::setDepthOfFieldBlurIntensity(::sStateStartBlur);
        mState[mStateIndex]->start();
        return;
    }

    MR::startAfterBossBGM();
    MR::requestAppearPowerStar(this, mPosition);
    mRabbit->setNerve(GET_NERVE_ANON(DodoryuRabbitNrvRabbitReturn));

    if (MR::isValidSwitchB(this)) {
        MR::onSwitchB(this);
    }

    MR::setImageEffectControlAuto();
    kill();
}

void Dodoryu::pauseOff() {
    MR::requestMovementOn(this);
    mHill->pauseOff();
    MR::requestMovementOn(mLeadHill);
    MR::requestMovementOn(mBank);
    MR::requestMovementOn(mRabbit);
    MR::requestEffectStopSceneEnd();
}

void Dodoryu::startPlayerCenterCamera() {
    if (MR::isValidSwitchB(this)) {
        MR::onSwitchB(this);
    }
}

void Dodoryu::endPlayerCenterCamera() {
    if (MR::isValidSwitchB(this)) {
        MR::offSwitchB(this);
    }
}

void Dodoryu::getPlayerResetMtx(Pos3f* pMtx, u32 index) const {
    MR::tryFindNamePos(MR::FormattedName("マリオ再セット%d", index + 1).c_str(), pMtx->toMtxPtr());
}

bool Dodoryu::checkWallWithVelocity() {
    mClosedAreaObj = nullptr;

    AreaObj* closedAreaObj = MR::getAreaObj("DodoryuClosedCylinder", mPosition);

    if (closedAreaObj == nullptr) {
        return false;
    }

    if (MR::isNearZero(mVelocity)) {
        return false;
    }

    Vec3f cylinderPos;
    MR::calcCylinderPos(&cylinderPos, closedAreaObj);

    Vec3f cylinderUpVec;
    MR::calcCylinderUpVec(&cylinderUpVec, closedAreaObj);

    Vec3f deltaPos = mPosition - cylinderPos;
    deltaPos.orthogonalize(cylinderUpVec);

    if (MR::isNearZero(deltaPos)) {
        return false;
    }

    if (deltaPos.dot(mVelocity) >= 0.0f) {
        return false;
    }

    MR::normalize(&deltaPos);

    mWallNormal = deltaPos;
    mClosedAreaObj = closedAreaObj;

    return true;
}

void Dodoryu::reactJumpOutCommon() const {
    MR::stopSceneForDefaultHit(::sJumpOutStopSceneFrame);
}

void Dodoryu::startSpinOutCamera() {
    MR::startGlobalEventCamera(::sSpinOutCamera, CameraTargetArg(mSpinOutCameraTarget), ::sSpinOutCameraSteps);
}

void Dodoryu::endSpinOutCamera() {
    MR::endGlobalEventCamera(::sSpinOutCamera, 0, true);
}

bool Dodoryu::keepOffFromClosedArea(Vec3f* pDirection) {
    AreaObj* pArea = MR::getAreaObj("DodoryuClosedCylinder", mPosition);
    if (pArea == nullptr) {
        return false;
    }

    Vec3f center;
    MR::calcCylinderPos(&center, pArea);
    Vec3f up;
    MR::calcCylinderUpVec(&up, pArea);
    Vec3f direction = mPosition - center;
    f32 height = up.dot(direction);
    direction.orthogonalize(up);

    if (MR::isNearZero(direction)) {
        Vec3f oldUp;
        mBaseMtx.getYDir(oldUp);
        Quat4f rotation;
        rotation.setRotate(oldUp, up);
        Vec3f side;
        mBaseMtx.getXDir(side);
        rotation.transform(side);
        direction.set(side);
    } else {
        MR::normalize(&direction);
    }

    if (pDirection != nullptr) {
        pDirection->set(direction);
    }

    direction.setLength(MR::getCylinderRadius(pArea));
    Pos3f mtx;
    mtx.set(mBaseMtx);
    mtx.setTrans(center + up * height + direction);
    setMtx(mtx);
    return true;
}

void Dodoryu::tryRumblePad() {
    f32 playerDistance = MR::calcDistanceToPlayer(this);

    if (playerDistance < ::sRumblePadNearDist) {
        MR::tryRumblePadWeak(this, PLAYER_1);
    } else if (playerDistance < ::sRumblePadFarDist) {
        MR::tryRumblePadVeryWeak(this, PLAYER_1);
    }
}

void Dodoryu::invalidateEyeSensor() {
    MR::invalidateHitSensor(this, ::sMapObjSensorName);
}

void Dodoryu::validateEyeSensor() {
    MR::validateHitSensor(this, ::sMapObjSensorName);
}

void Dodoryu::invalidateStarPieceSensor() {
    MR::invalidateHitSensor(this, ::sStarPieceSensorName);
}

void Dodoryu::validateStarPieceSensor() {
    MR::validateHitSensor(this, ::sStarPieceSensorName);
}

bool Dodoryu::isHeadNeedle(HitSensor* pReceiver, HitSensor* pSender) const {
    if (pReceiver != getSensor(::sHeadSensorName)) {
        return false;
    }

    Vec3f deltaPos = pSender->mPosition - pReceiver->mPosition;

    Vec3f xDir;
    Pos3f jointMtx;
    jointMtx.set(MR::getJointMtx(this, ::sHeadJointName));
    jointMtx.getXDir(xDir);

    return xDir.dot(deltaPos) >= ::sHeadNeedleReach;
}

void Dodoryu::shiftMoveStateRail(f32 speed) {
    mMoveStateHolder->shiftRail(speed);
}

void Dodoryu::shiftMoveStateChase(f32 accel, f32 lateralDamping, f32 maxSpeed, f32 turnAngle) {
    mMoveStateHolder->shiftChase(accel, lateralDamping, maxSpeed, turnAngle);
}

void Dodoryu::shiftMoveStateEscape(f32 speed, s32 dirStepMin, s32 dirStepMax, f32 dirAngleMax, f32 turnAngle) {
    mMoveStateHolder->shiftEscape(speed, dirStepMin, dirStepMax, dirAngleMax, turnAngle);
}

void Dodoryu::shiftMoveStateJumpOut(f32 speed, f32 turnAngle) {
    mMoveStateHolder->shiftJumpOut(speed, turnAngle);
}

void Dodoryu::shiftMoveStateNull() {
    mMoveStateHolder->shiftNull();
}

void Dodoryu::setMoveRailSpeed(f32 railSpeed) {
    mMoveStateHolder->setRailSpeed(railSpeed);
}

void Dodoryu::startHill() {
    mHill->startNaturally();
}

void Dodoryu::stopHill() {
    mHill->endNaturally();
}

void Dodoryu::stopHillForce() {
    mHill->end();
}

void Dodoryu::killAllHillPieces() {
    mHill->killAllPieces();
}

void Dodoryu::setHillAppearNumHalf() {
    mHill->setAppearNum(mHill->mPieceNum / 2);
}

void Dodoryu::setHillAppearNumMax() {
    mHill->setAppearNum(mHill->mPieceNum - ::sHillMaxSpare);
}

void Dodoryu::startLeadHillBck(const char* pBckName) {
    if (MR::isDead(mLeadHill)) {
        mLeadHill->appear();
    }

    MR::startBck(mLeadHill, pBckName);
}

void Dodoryu::killLeadHill() {
    mLeadHill->kill();
}

void Dodoryu::appearBank() {
}

void Dodoryu::leaveRabbit() {
    if (MR::isDead(mRabbit)) {
        return;
    }

    mRabbit->setNerve(GET_NERVE_ANON(DodoryuRabbitNrvRabbitJump));
}

void Dodoryu::resetRabbit() {
    mRabbit->reset(true);
}

void Dodoryu::calcRabbitDir(Vec3f* pDir) const {
    pDir->set(mRabbit->mPosition - mPosition);
    pDir->orthogonalize(mGravity);
    MR::normalizeOrZero(pDir);
}

void Dodoryu::displayRabbitMessage() {
    mRabbit->mIsDisplayMessage = true;
}

void Dodoryu::calcAndSetBaseMtx() {
    MR::setBaseTRMtx(this, mBaseMtx);
    Vec3f scale = mAnimScaleCtrl->mScale * mScale;
    MR::setBaseScale(this, scale);
}

void Dodoryu::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensor(pSender, ::sStarPieceSensorName)) {
        return;
    }

    if (!MR::isSensorMapObj(pReceiver)) {
        mState[mStateIndex]->catchAttackSensor(pSender, pReceiver);
        return;
    }

    if (MR::isSensor(pSender, ::sMapObjSensorName)) {
        MR::sendMsgEnemyAttack(pReceiver, pSender);
    }
}

bool Dodoryu::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensor(pReceiver, ::sStarPieceSensorName)) {
        if (!MR::isMsgStarPieceAttack(msg)) {
            return false;
        }

        MR::startSound(this, "SE_OJ_STAR_PIECE_HIT_STOP");
        mAnimScaleCtrl->startHitReaction();
        mMoveStateHolder->brake();
        return true;
    }

    if (MR::isMsgStarPieceReflect(msg)) {
        return isHeadNeedle(pReceiver, pSender);
    }

    if (MR::isMsgStarPieceAttack(msg)) {
        return true;
    }

    return mState[mStateIndex]->catchPlayerAttack(msg, pSender, pReceiver);
}

void Dodoryu::initHitSensor() {
    LiveActor::initHitSensor(::sSensorCount);

    MR::addHitSensorAtJointEnemy(this, ::sHeadSensorName, ::sHeadJointName, ::sEnemySensorGroupSize, ::sHeadRadius, ::sHeadOffset);
    MR::addHitSensorAtJointEnemy(this, ::sBodySensorName, "Hip", ::sEnemySensorGroupSize, ::sBodyRadius, ::sBodyOffset);
    MR::addHitSensorAtJointEnemy(this, ::sLegLSensorName, "LegL2", ::sEnemySensorGroupSize, ::sLegRadius, ::sLegOffset);
    MR::addHitSensorAtJointEnemy(this, ::sLegRSensorName, "LegR2", ::sEnemySensorGroupSize, ::sLegRadius, ::sLegOffset);
    MR::addHitSensorAtJointEnemy(this, ::sArmLSensorName, "HandL", ::sEnemySensorGroupSize, ::sArmRadius, ::sArmOffset);
    MR::addHitSensorAtJointEnemy(this, ::sArmRSensorName, "HandR", ::sEnemySensorGroupSize, ::sArmRadius, ::sArmOffset);
    MR::addHitSensorAtJointEnemy(this, ::sTailSensorName, "Tail", ::sEnemySensorGroupSize, ::sTailRadius, ::sTailOffset);
    MR::addHitSensorEye(this, ::sMapObjSensorName, ::sMapObjSensorGroupSize, ::sMapObjSensorRadius, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorAtJointMapObjSimple(this, ::sStarPieceSensorName, "Hip", ::sMapObjSensorGroupSize, ::sStarPieceSensorRadius,
                                        ::sStarPieceSensorOffset);
}

void Dodoryu::initSwitch(const TableIter& rIter) {
    if (MR::useStageSwitchReadA(this, rIter)) {
        MR::listenStageSwitchOnA(this, MR::Functor(this, &Dodoryu::notifyOnSwitchA));
    }

    MR::useStageSwitchWriteB(this, rIter);
}

void Dodoryu::createMogucchiHill() {
    mHill = new DodoryuHill(this);
    mHill->setAppearNum(::sHillStartPieces);
}

void Dodoryu::createDodoryuBank() {
    mBank = new DodoryuBank();
    mBank->initWithoutIter();
}

void Dodoryu::turnUpVecTo(const Vec3f& rUp) {
    Vec3f up;
    mBaseMtx.getYDir(up);

    Pos3f rotation;
    rotation.setRotate(up, rUp);
    mBaseMtx.concat(rotation, mBaseMtx);

    // Re-orthonormalize: y = z x x, then z = x x y.
    Vec3f xDir, yDir, zDir;
    mBaseMtx.getXYZDir(xDir, yDir, zDir);
    NebulaVecCrossProduct(&zDir, &xDir, &yDir);
    NebulaVecCrossProduct(&xDir, &yDir, &zDir);
    NebulaVecNormalize(&yDir, &yDir);
    NebulaVecNormalize(&zDir, &zDir);
    mBaseMtx.setXDir(xDir);
    mBaseMtx.setYDir(yDir);
    mBaseMtx.setZDir(zDir);
}

void Dodoryu::checkHipDrop() {
    if (MR::isPlayerHipDropLand()) {
        mState[mStateIndex]->catchHipDrop();
    }
}

void Dodoryu::initState() {
    mState.push_back(new DodoryuStateWait(this, "DodoryuStateWait"));
    mState.push_back(new DodoryuDemoOpening(this, "DodoryuDemoOpening"));
    mState.push_back(new DodoryuStateLv1(this, "DodoryuStateLv1"));
    mState.push_back(new DodoryuDemoAppear(this, "DodoryuDemoAppear"));
    mState.push_back(new DodoryuStateLv2(this, &::sDodoryuChaseDataLv2, "DodoryuStateLv2"));
    mState.push_back(new DodoryuDemoAngry(this, "DodoryuDemoAngry"));
    mState.push_back(new DodoryuStateLv2(this, &::sDodoryuChaseDataLv3, "DodoryuStateLv3"));
    mState.push_back(new DodoryuDemoDown(this, "DodoryuDemoDown"));
    mStateIndex = 0;
}

void Dodoryu::updateRumblePad() {
    if (mIsRumbling) {
        MR::tryRumblePadWeak(this, PLAYER_1);
    }
}

void Dodoryu::updateCameraTarget() {
    Pos3f hipMtx;
    hipMtx.setInline(MR::getJointMtx(this, "Hip"));

    Vec3f hipPos;
    hipMtx.getTrans(hipPos);

    Vec3f target = hipPos * ::sCameraHipWeight + *MR::getPlayerPos() * ::sCameraPlayerWeight;
    hipMtx.setTrans(target);
    mSpinOutCameraTarget->mMatrix.setInline(hipMtx);
}

DodoryuBank::DodoryuBank() : ModelObj("ドドリュウ盛土", "DodoryuBank", mBankMtx.mMtx, MR::DrawBufferType_MapObjStrongLight, -2, -2, false) {
    mBankMtx.identity();
}

void DodoryuBank::init(const TableIter& /*rIter*/) {
    initNerve(GET_NERVE_ANON(DodoryuBankNrvBankAppear));
    makeActorDead();
}

void DodoryuBank::exeAppear() {
    if (MR::isBckStopped(this)) {
        kill();
        MR::validateClipping(this);
    }
}

DodoryuRabbit::DodoryuRabbit(Dodoryu* pHost, const TableIter& rIter)
    : ModelObj("ドドリュウに追われるウサギ", "DodoryuRabbit", mRabbitMtx.mMtx, MR::DrawBufferType_NPC, -2, -2, false), mHost(pHost),
      mRailCoord(), mTalkCtrl(), mTalkSwitchStep(::sRabbitTalkSwitchSteps), mTalkNode(), mTrampleRumbler(), mStarPieceRumbler(), mIsDisplayMessage() {
    mRabbitMtx.identity();

    initWithoutIter();

    mTalkMtx.identity();
    mTalkMtx.setTrans(mPosition);

    mTalkCtrl = MR::createTalkCtrl(this, rIter, "DodoryuRabbit", Vec3f(cNum_DodoryuRabbit_f1, cNum_DodoryuRabbit_f2, cNum_DodoryuRabbit_f3), mTalkMtx);
    MR::setDistanceToTalk(mTalkCtrl, ::sRabbitTalkDistance);
}

void DodoryuRabbit::init(const TableIter& /*rIter*/) {
    initNerve(GET_NERVE_ANON(DodoryuRabbitNrvRabbitEscape));
    initHitSensor(4);
    MR::addHitSensorAtJointEnemy(this, "body", "Spine", 8, ::sRabbitBodyRadius, Vec3f(0.0f, 0.0f, 0.0f));
    MR::initShadowFromCSV(this, "Shadow");

    mTrampleRumbler = new JointRumbler(this, "Spine", ::sRabbitRumbleRadius, ::sRabbitRumbleSpeed, ::sRabbitTrampleRumbleSteps, true, 0);
    mStarPieceRumbler = new JointRumbler(this, "Spine", ::sRabbitRumbleRadius, ::sRabbitRumbleSpeed, ::sRabbitStarPieceRumbleSteps, true, 0);

    MR::initLightCtrl(this);
    reset(false);
    MR::invalidateClipping(this);
    makeActorAppeared();
}

void DodoryuRabbit::control() {
    bool isEscape = isNerve(GET_NERVE_ANON(DodoryuRabbitNrvRabbitEscapeWaiting)) || isNerve(GET_NERVE_ANON(DodoryuRabbitNrvRabbitEscape)) ||
                    isNerve(GET_NERVE_ANON(DodoryuRabbitNrvRabbitEscapeSlow));

    if (isEscape) {
        mTalkSwitchStep--;

        if (mTalkSwitchStep <= 0) {
            mTalkSwitchStep = ::sRabbitTalkSwitchSteps;
            mTalkNode = (mTalkNode + 1) % ::sRabbitTalkNodeCount;

            MR::resetAndForwardNode(mTalkCtrl, mTalkNode);
        }
    }

    if (mTrampleRumbler->isRumbling()) {
        mTrampleRumbler->update();
    }

    if (mStarPieceRumbler->isRumbling()) {
        mStarPieceRumbler->update();
    }

    if (isNerve(GET_NERVE_ANON(DodoryuRabbitNrvRabbitPleasure)) && getNerveStep() > ::sRabbitPleasureTalkFollowSteps) {
        return;
    }

    mTalkMtx.setInline(MR::getJointMtx(this, "Spine"));

    if (MR::isShadowProjected(this, nullptr)) {
        Vec3f shadowPos;
        MR::getShadowProjectionPos(this, nullptr, &shadowPos);
        mTalkMtx.setTrans(shadowPos);
    }
}

void DodoryuRabbit::exeEscapeWaiting() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Run");
        MR::startBtp(this, "Blink");
    }

    tryTalk();
}

void DodoryuRabbit::exeEscape() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Run");
        MR::startBtp(this, "Blink");
    }

    if (MR::checkPassBckFrame(this, ::sRabbitHopSoundFrame)) {
        MR::startSound(this, "SE_SM_RABBIT_JUMP");
    }

    updatePos(::sRabbitFastSpeed);
    tryTalk();

    if (calcCoordDiff() > ::sRabbitDistMax) {
        setNerve(GET_NERVE_ANON(DodoryuRabbitNrvRabbitEscapeSlow));
    }
}

void DodoryuRabbit::exeEscapeSlow() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Walk");
        MR::startBtp(this, "blink");
    }

    if (MR::checkPassBckFrame(this, ::sRabbitHopSoundFrame)) {
        MR::startSound(this, "SE_SM_RABBIT_JUMP");
    }

    updatePos(::sRabbitSlowSpeed);
    tryTalk();

    f32 coordDiff = calcCoordDiff();

    if (coordDiff < ::sRabbitDistMin) {
        setNerve(GET_NERVE_ANON(DodoryuRabbitNrvRabbitEscape));
    } else if (coordDiff > ::sRabbitRestDist) {
        setNerve(GET_NERVE_ANON(DodoryuRabbitNrvRabbitRest));
    }
}

void DodoryuRabbit::exeRest() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Tired");
        MR::startBtp(this, "Tired");
    }

    tryTalk();

    if (calcCoordDiff() < ::sRabbitDistMin) {
        setNerve(GET_NERVE_ANON(DodoryuRabbitNrvRabbitEscape));
    }
}

void DodoryuRabbit::exeJump() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Jump");
        MR::startBtp(this, "Blink");
        mRabbitMtx.setInline(mHost->mDemoCameraTarget->mMatrix);
    }

    MR::setNerveAtBckStopped(this, GET_NERVE_ANON(DodoryuRabbitNrvRabbitWait));
}

void DodoryuRabbit::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
        MR::startBtp(this, "Blink");
    }
}

void DodoryuRabbit::exeReturn() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "StarJump");
        MR::startBtp(this, "blink");
    }

    MR::setNerveAtBckStopped(this, GET_NERVE_ANON(DodoryuRabbitNrvRabbitPleasure));
}

void DodoryuRabbit::exePleasure() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "StarWait");
        MR::startBtp(this, "blink");
        MR::resetAndForwardNode(mTalkCtrl, ::sRabbitPleasureTalkNode);
        MR::setDistanceToTalk(mTalkCtrl, ::sRabbitPleasureTalkDistance);
    }

    tryTalk();
}

void DodoryuRabbit::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayerOrRide(pReceiver)) {
        MR::sendMsgPush(pReceiver, pSender);
    } else if (MR::isSensorMapObj(pReceiver)) {
        MR::sendMsgEnemyAttack(pReceiver, pSender);
    }
}

bool DodoryuRabbit::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgPlayerTrample(msg)) {
        if (mStarPieceRumbler->isRumbling()) {
            mStarPieceRumbler->reset();
        }

        mTrampleRumbler->start();
        return true;
    }

    // A reflected star piece shakes the rabbit but is not taken.
    if (MR::isMsgStarPieceReflect(msg)) {
        if (mTrampleRumbler->isRumbling()) {
            mTrampleRumbler->reset();
        }

        mStarPieceRumbler->start();
    }

    return false;
}

void DodoryuRabbit::reset(bool isPlace) {
    mRailCoord = MR::getRailCoord(mHost) + ::sRabbitLead;

    setNerve(GET_NERVE_ANON(DodoryuRabbitNrvRabbitEscape));

    if (isPlace) {
        updatePos(0.0f);
    }
}

void DodoryuRabbit::updatePos(f32 speed) {
    mRailCoord += speed;

    if (mRailCoord >= MR::getRailTotalLength(mHost)) {
        mRailCoord -= MR::getRailTotalLength(mHost);
    }

    Vec3f railPos;
    MR::calcRailPosAtCoord(&railPos, mHost, mRailCoord);
    mPosition.set(railPos);

    MR::calcGravityVector(this, &mGravity, nullptr, 0);

    Vec3f upVec(-mGravity);
    Vec3f railDir;
    MR::calcRailDirectionAtCoord(&railDir, mHost, mRailCoord);

    Vec3f side;
    side.cross(upVec, railDir);

    Triangle triangle;
    Vec3f groundPos;
    if (!MR::getFirstPolyOnLineToMap(&groundPos, &triangle, mPosition - mGravity.scaleInline(::sGroundProbeRise), mGravity.scaleInline(::sGroundProbeLength))) {
        groundPos.set(railPos);
    }

    mRabbitMtx.setXYZDir(side, upVec, railDir);
    mRabbitMtx.setTrans(groundPos);
}

f32 DodoryuRabbit::calcCoordDiff() const {
    f32 coord = MR::getRailCoord(mHost);

    if (mRailCoord < coord) {
        return (mRailCoord + MR::getRailTotalLength(mHost)) - coord;
    }

    return mRailCoord - coord;
}

bool DodoryuRabbit::tryTalk() {
    if (mIsDisplayMessage) {
        return MR::tryTalkNearPlayer(mTalkCtrl);
    }

    return false;
}

DodoryuLeadHill::DodoryuLeadHill(Dodoryu* pHost) : LiveActor("ドドリュウ塚先頭"), mHostBaseMtx(pHost->getBaseMtx()), mSpinAngle() {
    for (int i = 0; i < ARRAY_SIZE(mJointCtrls); i++) {
        mJointCtrls[i] = nullptr;
    }

    initWithoutIter();
}

void DodoryuLeadHill::init(const TableIter& /*rIter*/) {
    initModelManagerWithAnm("DodoryuLeadHill", nullptr, false);
    MR::connectToSceneMapObjDecorationStrongLight(this);
    MR::initLightCtrl(this);
    initSound(8, false);
    initEffectKeeper(8, nullptr);
    mPosition.setTrans(mHostBaseMtx);
    initJoint();
    makeActorDead();
}

void DodoryuLeadHill::control() {
    mSpinAngle += ::sLeadHillSpinSpeed;
}

bool DodoryuLeadHill::calcJoint(Pos3f* pDst, const JointControllerInfo&) {
    Pos3f rotateMtx;
    rotateMtx.makeRotate(Vec3f(1.0f, 0.0f, 0.0f), mSpinAngle);
    pDst->concat(rotateMtx);

    return true;
}

void DodoryuLeadHill::calcAndSetBaseMtx() {
    for (int i = 0; i < ARRAY_SIZE(mJointCtrls); i++) {
        mJointCtrls[i]->registerCallBack();
    }

    if (mHostBaseMtx != nullptr) {
        mPosition.setTrans(mHostBaseMtx);
        MR::setBaseTRMtx(this, mHostBaseMtx);
    } else {
        LiveActor::calcAndSetBaseMtx();
    }
}

void DodoryuLeadHill::initJoint() {
    for (int i = 0; i < ARRAY_SIZE(mJointCtrls); i++) {
        mJointCtrls[i] =
            MR::createJointDelegatorWithNullChildFunc(this, &DodoryuLeadHill::calcJoint, MR::FormattedName("DodoryuLeadHill%d", i + 1).c_str());
    }
}
