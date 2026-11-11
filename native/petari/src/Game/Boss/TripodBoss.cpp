#include "Game/Boss/TripodBoss.hpp"
#include "Game/Boss/TripodBossAccesser.hpp"
#include "Game/Boss/TripodBossLeg.hpp"
#include "Game/Boss/TripodBossMovableArea.hpp"
#include "Game/Boss/TripodBossStepPoint.hpp"
#include "Game/Boss/TripodBossStepSequence.hpp"
#include "Game/Gravity/GravityInfo.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/CollisionParts.hpp"
#include "Game/MapObj/DummyDisplayModel.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_initLegIKPlacement_f1, "initLegIKPlacement.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_requestEndDamageDemo_f1, "requestEndDamageDemo.f1", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_requestEndDamageDemo_f2, "requestEndDamageDemo.f2", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDamage_f1, "exeDamage.f1", 79.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDamage_f2, "exeDamage.f2", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePainDemo_f1, "exePainDemo.f1", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePainDemo_f2, "exePainDemo.f2", 3200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePainDemo_f3, "exePainDemo.f3", 3400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePainDemo_f4, "exePainDemo.f4", 3400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePainDemo_f5, "exePainDemo.f5", 5000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcDemoMovement_f1, "calcDemoMovement.f1", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcBodyMovement_f1, "calcBodyMovement.f1", 0.95f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcBodyMovement_f2, "calcBodyMovement.f2", 0.95f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcBodyMovement_f3, "calcBodyMovement.f3", 0.95f);
NEBULA_TUNABLE_NAMED(f32, cNum_addAccelToWeightPosition_f1, "addAccelToWeightPosition.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_addAccelToWeightPosition_f2, "addAccelToWeightPosition.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_addAccelToWeightPosition_f3, "addAccelToWeightPosition.f3", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_addAccelToWeightPosition_f4, "addAccelToWeightPosition.f4", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_addAccelToWeightPosition_f5, "addAccelToWeightPosition.f5", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_addAccelToWeightPosition_f6, "addAccelToWeightPosition.f6", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcClippingSphere_f1, "calcClippingSphere.f1", 0.5f);

namespace {
    // Leg i follows this joint of the boss model in demos.
    const char* const sLegBoneNameTable[] = {"LeftLeg", "RightLeg", "BackLeg"};
    const Vec3f sAppearStarPieceOffset(0.0f, 3600.0f, 0.0f);
    const Vec3f sEndMarioPosition(0.0f, 2160.0f, 1260.0f);
    const s32 sBreakDownTime = NebulaFrameClock::stepsOfSeconds(4);
    const s32 sExplosionTime = NebulaFrameClock::stepsOfMilliseconds(2500);
    NEBULA_TUNABLE(s32, sKillerGeneraterIncreaseSeTiming, 90);
    NEBULA_TUNABLE(s32, sHeadExplodeSeTiming, 0);
    NEBULA_TUNABLE(s32, sDamageTime, 129);
    const s32 sRideMarioKeepTime = NebulaFrameClock::stepsOfSeconds(2);  // steps "riding" lasts after the player leaves the boss's gravity
    NEBULA_TUNABLE(s32, sBgmStateNormal, 1);
    NEBULA_TUNABLE(s32, sBgmStateRide, 3);
    NEBULA_TUNABLE(f32, sLegAngleStep, 2.0943952f);  // 2 pi / 3
    const s32 sBonesPerLeg = 7;
    // The demo blends the legs in over half a second.
    const s32 sDemoBlendSteps = NebulaFrameClock::stepsOfSeconds(1) / 2;

    // Bone 7 * leg + k is that leg's joint cBoneSubParts[k]; bone 21 is the body.
    const TripodBoss::SUB_PART_ID cBoneSubParts[sBonesPerLeg] = {
        TripodBoss::SUB_PART_ID_ROOT_JOINT,     TripodBoss::SUB_PART_ID_MIDDLE_JOINT,  TripodBoss::SUB_PART_ID_END_JOINT,
        TripodBoss::SUB_PART_ID_ROOT_LOCAL_Y,   TripodBoss::SUB_PART_ID_ROOT_LOCAL_YZ, TripodBoss::SUB_PART_ID_ANKLE_LOCAL_X,
        TripodBoss::SUB_PART_ID_ANKLE_LOCAL_XZ,
    };
};  // namespace

namespace NrvTripodBoss {
    NEW_NERVE(TripodBossNrvTryStartDemo, TripodBoss, TryStartDemo);
    NEW_NERVE(TripodBossNrvNonActive, TripodBoss, NonActive);
    NEW_NERVE(TripodBossNrvWait, TripodBoss, Wait);
    NEW_NERVE(TripodBossNrvStep, TripodBoss, Step);
    NEW_NERVE(TripodBossNrvWaitStep, TripodBoss, WaitStep);
    NEW_NERVE(TripodBossNrvDamage, TripodBoss, Damage);
    NEW_NERVE(TripodBossNrvChangeSequence, TripodBoss, ChangeSequence);
    NEW_NERVE(TripodBossNrvLeaveLegOutOfPlayer, TripodBoss, LeaveLegOutOfPlayer);
    NEW_NERVE(TripodBossNrvStartDemo, TripodBoss, StartDemo);
    NEW_NERVE(TripodBossNrvDamageDemo, TripodBoss, DamageDemo);
    NEW_NERVE(TripodBossNrvPainDemo, TripodBoss, PainDemo);
    NEW_NERVE(TripodBossNrvBreakDownDemo, TripodBoss, BreakDownDemo);
    NEW_NERVE(TripodBossNrvExplosionDemo, TripodBoss, ExplosionDemo);
};  // namespace NrvTripodBoss

TripodBoss::TripodBoss(const char* pName)
    : LiveActor(pName), mMovableArea(), mStepSequenceNum(0), mDummyModel(), mDummyModelOffset(0.0f, 3200.0f), mWeightPos(0, 0, 0), mBodyPos(0, 0, 0), mBodyVelocity(0, 0, 0), mClipCenter(0, 0, 0),
      mClipRadius(7500.0f), mWeightHeightOffset(), mDemoBlendRate(1.0f), mBodyHeight(3000.0f), mLegRootLength(4000.0f), mLegMiddleLength(4000.0f), mLegReach(2400.0f), mLegLift(), mLegHeightRate(0.1f), mMovableAreaRadius(3000.0f), mDummyModelSpinSpeed(2.0f),
      mCurrentStepSeq(-1), mNextStepSeq(-1), mRideMarioTimer(), mRideMarioState(RIDE_OFF), mIsPartsHidden(false), mBgmState(sBgmStateNormal), mIsBgmChangePending(false), mEventCamera() {
    lumacore::setClass(this, lumacore::kClassId< TripodBoss >);
    mBodyMtx.identity();
    mDemoStartMtx.identity();

    TripodBossAccesser::createSceneObj()->setTriPodBoss(this);

    for (u32 i = 0; i < ARRAY_SIZE(mLegs); i++) {
        mLegs[i] = nullptr;
    }
}

void TripodBoss::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::connectToSceneCollisionMapObjMovementCalcAnim(this);
    Pos3f placementMtx;
    placementMtx.identity();
    MR::getPlacementMatrixFromRT(rIter, &placementMtx);
    mBodyPos = mPosition;
    initMovableArea(placementMtx);
    mBodyMtx.set(placementMtx);
    initBodyPosition();

    const char* objName;
    MR::getObjectName(&objName, rIter);

    initModelManagerWithAnm(objName, nullptr, false);

    initLeg(rIter);
    initBoneInfo();
    calcLegMovement();
    initEventCamera(rIter);
    initSound(8, false);
    initEffectKeeper(0, "TripodBoss");
    MR::setEffectHostMtx(this, "BreakLight", mBodyMtx);
    initNerve(GET_NERVE(TripodBoss, TripodBossNrvNonActive));
    MR::invalidateClipping(this);
    MR::needStageSwitchReadA(this, rIter);
    MR::listenStageSwitchOnA(this, MR::Functor(this, &TripodBoss::requestOpeningDemo));
    MR::useStageSwitchReadB(this, rIter);
    MR::useStageSwitchWriteDead(this, rIter);
    MR::declareStarPiece(this, 24);
    MR::declarePowerStar(this);
    mDummyModelMtx.set(mBodyMtx);
    MR::addTransMtxLocal(mDummyModelMtx, mDummyModelOffset);

    mDummyModel = MR::createDummyDisplayModel(this, rIter, mDummyModelMtx, DummyDisplayModelId_GrandStar, Vec3f(0.0f, 0.0f, 0.0f), Vec3f(0.0f, 0.0f, 0.0f));

    // A mod may have removed the star's display model.
    if (mDummyModel != nullptr) {
        MR::startBrk(mDummyModel, "Recover");
        MR::setBrkFrameAndStop(mDummyModel, 0.0f);
    }

    makeActorAppeared();
    MR::startBck(this, "StartDemo");
    MR::setBckFrameAndStop(this, 0.0f);
    initPose();
    MR::offCalcAnim(this);
}

void TripodBoss::initAfterPlacement() {
    mIsPartsHidden = true;

    MR::hideTripodBossParts();
}

void TripodBoss::initEventCamera(const TableIter& rIter) {
    mEventCamera = MR::createActorCameraInfo(rIter);

    MR::initAnimCamera(this, mEventCamera, "StartDemo");
    MR::initAnimCamera(this, mEventCamera, "EndDemo");
}

void TripodBoss::initLeg(const TableIter& rIter) {
    const char* objName;
    MR::getObjectName(&objName, rIter);

    MR::FormattedName legShadowName("%sLegShadow", objName);

    for (u32 i = 0; i < ARRAY_SIZE(mLegs); i++) {
        mLegs[i] = new TripodBossLeg("三脚ボス足");
        mStepPoints[i] = new TripodBossStepPoint("ステップ位置");
        mLegs[i]->setBody(this);
        mLegs[i]->setMovableArea(mMovableArea);
        mLegs[i]->initWithoutIter();
        mLegs[i]->initShadow(legShadowName.c_str());
        mStepPoints[i]->initWithoutIter();
    }

    initLegIKPlacement();

    for (u32 i = 0; i < ARRAY_SIZE(mLegs); i++) {
        MR::addTripodBossParts(mLegs[i]);
    }
}

// The legs stand 120 degrees apart on the area sphere, mLegHeightRate of its radius up the base axis.
void TripodBoss::initLegIKPlacement() {
    f32 heightRate = mLegHeightRate;
    f32 widthRate = MR::sqrt(1.0f - heightRate * heightRate);

    Vec3f baseAxis(mMovableArea->mBaseAxis);
    Vec3f front(mMovableArea->mFront);
    Vec3f side = baseAxis.cross(front);
    baseAxis *= heightRate * mMovableArea->mRadius;
    front *= widthRate * mMovableArea->mRadius;
    side *= widthRate * mMovableArea->mRadius;

    for (u32 i = 0; i < ARRAY_SIZE(mLegs); i++) {
        f32 angle = cNum_initLegIKPlacement_f1 * sLegAngleStep + -(f32)i * sLegAngleStep;
        f32 x = MR::sin(angle);
        f32 z = MR::cos(angle);

        Vec3f legDirXZ(x, 0.0f, z);
        Vec3f up(0.0f, 1.0f, 0.0f);
        Vec3f legRootPos = legDirXZ * mLegReach + up * mLegLift;
        getLeg(i)->setIKParam(mLegRootLength, mLegMiddleLength, legRootPos, legDirXZ, up);

        Vec3f stepPos = baseAxis + side * x + front * z + mMovableArea->mCenter;

        Vec3f stepNormal;
        mMovableArea->calcLandingNormal(&stepNormal, stepPos);

        Vec3f stepFront;
        mMovableArea->calcLandingFront(&stepFront, stepPos);

        getStepPoint(i)->setStepPosition(stepPos);
        getStepPoint(i)->setStepNormal(stepNormal);
        getStepPoint(i)->setStepFront(stepFront);
        getLeg(i)->setStepTarget(mStepPoints[i]);
        getLeg(i)->setWait();
    }
}

void TripodBoss::initMovableArea(const Pos3f& rPos) {
    Vec3f trans;
    rPos.getTrans(trans);

    mMovableArea = new TripodBossMovableArea();
    mMovableArea->setCenter(trans);
    mMovableArea->setRadius(mMovableAreaRadius);

    Vec3f yDir;
    rPos.getYDir(yDir);

    Vec3f zDir;
    rPos.getZDir(zDir);

    mMovableArea->setBaseAxis(yDir);
    mMovableArea->setFrontVector(zDir);
}

void TripodBoss::initBodyPosition() {
    f32 radius = mMovableArea->mRadius;
    mBodyPos = mMovableArea->mCenter + mMovableArea->mBaseAxis * (mBodyHeight + radius);
    mWeightPos = mBodyPos;

    MR::makeMtxTR(mBodyMtx, mBodyPos, mRotation);
}

void TripodBoss::initBoneInfo() {
    mBossBones[cBodyBone].mAttachMtx = &mBodyMtx;

    for (s32 part = PART_ID_LEFT_LEG; part < PART_ID_MAX; part++) {
        for (s32 k = 0; k < sBonesPerLeg; k++) {
            mBossBones[part * sBonesPerLeg + k].mAttachMtx = getLegMatrixPtr(static_cast< PART_ID >(part), cBoneSubParts[k]);
        }
    }
}

void TripodBoss::initPose() {
    calcAnim();

    for (u32 i = 0; i < ARRAY_SIZE(mLegs); i++) {
        mLegs[i]->requestStartDemo();
    }

    calcDemoMovement();
    calcClippingSphere();
}

void TripodBoss::kill() {
    MR::startAfterBossBGM();
    LiveActor::kill();

    for (u32 i = 0; i < ARRAY_SIZE(mLegs); i++) {
        mLegs[i]->kill();
    }

    MR::requestAppearPowerStar(this, mBodyPos);

    if (MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
    }
}

void TripodBoss::control() {
    mDummyModelMtx.set(mBodyMtx);
    MR::addTransMtxLocal(mDummyModelMtx, mDummyModelOffset);

    if (mDummyModel != nullptr) {
        mDummyModel->mRotation.y = MR::repeatDegree(mDummyModel->mRotation.y - mDummyModelSpinSpeed);
    }

    if (isNerve(GET_NERVE(TripodBoss, TripodBossNrvNonActive))) {
        clippingModel();
    } else {
        mNextStepSeq = -1;
        checkRideMario();
        changeBgmState();
    }
}

void TripodBoss::calcAndSetBaseMtx() {
    LiveActor::calcAndSetBaseMtx();
}

bool TripodBoss::tryStartStep() {
    if (mNextStepSeq == -1 || isRideMario()) {
        return false;
    }

    mCurrentStepSeq = mNextStepSeq;
    mStepSequences[mCurrentStepSeq]->reset();
    setNerve(GET_NERVE(TripodBoss, TripodBossNrvStep));

    return true;
}

bool TripodBoss::tryChangeSequence() {
    if (mNextStepSeq == -1 || mNextStepSeq == mCurrentStepSeq || isRideMario()) {
        return false;
    }

    TripodBossStepSequence* currentSequence = getCurrentStepSequence();

    if (currentSequence == nullptr) {
        return false;
    }

    s32 leg = currentSequence->getCurrentLeg();

    if (!mLegs[leg]->canCancelStep()) {
        return false;
    }

    TripodBossStepSequence* stepSequence = getNextStepSequence();
    stepSequence->reset();
    leg = stepSequence->getCurrentLeg();

    // The other moving legs go back down where they stand before the new sequence starts.
    bool isAnyMoving = false;
    for (u32 i = 0; i < ARRAY_SIZE(mLegs); i++) {
        if (i != static_cast< u32 >(leg) && !mLegs[i]->isStop()) {
            mLegs[i]->requestStepTarget(mStepPoints[i]);
            isAnyMoving = true;
        }
    }

    mCurrentStepSeq = mNextStepSeq;

    if (!isAnyMoving) {
        setNerve(GET_NERVE(TripodBoss, TripodBossNrvStep));
    } else {
        setNerve(GET_NERVE(TripodBoss, TripodBossNrvChangeSequence));
    }

    return true;
}

bool TripodBoss::tryEndSequence() {
    if (mNextStepSeq != -1) {
        return false;
    }

    mCurrentStepSeq = -1;
    setNerve(GET_NERVE(TripodBoss, TripodBossNrvWait));

    return true;
}

bool TripodBoss::tryNextSequence() {
    if (isStopAllLeg() && !isRideMario()) {
        setNerve(GET_NERVE(TripodBoss, TripodBossNrvStep));

        return true;
    }

    return false;
}

bool TripodBoss::tryDamage() {
    TripodBossStepSequence* stepSequence = getCurrentStepSequence();

    if (stepSequence == nullptr) {
        return false;
    }

    s32 leg = stepSequence->getCurrentLeg();

    if (mLegs[leg]->isDamage()) {
        setNerve(GET_NERVE(TripodBoss, TripodBossNrvDamage));

        return true;
    }

    return false;
}

bool TripodBoss::tryWaitStep() {
    TripodBossStepSequence* stepSequence = getCurrentStepSequence();

    if (stepSequence == nullptr) {
        return false;
    }

    s32 leg = stepSequence->getCurrentLeg();

    if (mLegs[leg]->isLanding()) {
        setNerve(GET_NERVE(TripodBoss, TripodBossNrvWaitStep));

        return true;
    }

    return false;
}

bool TripodBoss::tryNextStep() {
    if (isRideMario()) {
        return false;
    }

    TripodBossStepSequence* stepSequence = getCurrentStepSequence();

    if (stepSequence == nullptr) {
        return false;
    }

    s32 leg = stepSequence->getCurrentLeg();

    if (MR::isGreaterStep(this, stepSequence->getCurrentWaitTime()) || mLegs[leg]->isBroken()) {
        stepSequence->nextStep();
        setNerve(GET_NERVE(TripodBoss, TripodBossNrvStep));

        return true;
    }

    return false;
}

bool TripodBoss::tryLeaveLegOutOfPlayer() {
    if (MR::isPlayerOnPress()) {
        bool isPressed = false;

        for (u32 i = 0; i < ARRAY_SIZE(mLegs); i++) {
            if (mLegs[i]->isPressPlayer()) {
                mLegs[i]->requestLeaveOut();
                isPressed = true;
                break;
            }
        }

        if (isPressed) {
            setNerve(GET_NERVE(TripodBoss, TripodBossNrvLeaveLegOutOfPlayer));

            return true;
        }
    }

    return false;
}

bool TripodBoss::tryEndLeaveLegOutOfPlayer() {
    if (!MR::isPlayerOnPress()) {
        setNerve(GET_NERVE(TripodBoss, TripodBossNrvWait));

        return true;
    }

    return false;
}

bool TripodBoss::tryEndDamage() {
    if (MR::isGreaterStep(this, sDamageTime)) {
        setNerve(GET_NERVE(TripodBoss, TripodBossNrvWait));

        return true;
    }

    return false;
}

bool TripodBoss::tryBreak() {
    if (MR::isValidSwitchB(this) && MR::isOnSwitchB(this)) {
        for (u32 i = 0; i < ARRAY_SIZE(mLegs); i++) {
            mLegs[i]->requestBreak();
        }

        setNerve(GET_NERVE(TripodBoss, TripodBossNrvTryStartDemo));
        MR::requestStartDemoMarioPuppetable(this, "破壊", GET_NERVE(TripodBoss, TripodBossNrvPainDemo), nullptr);

        return true;
    }

    return false;
}

void TripodBoss::requestOpeningDemo() {
    if (isNerve(GET_NERVE(TripodBoss, TripodBossNrvNonActive))) {
        mIsPartsHidden = false;
        MR::activeTripodBossParts();
        setNerve(GET_NERVE(TripodBoss, TripodBossNrvTryStartDemo));
        MR::requestStartDemoMarioPuppetable(this, "開始", GET_NERVE(TripodBoss, TripodBossNrvStartDemo), nullptr);
    }
}

bool TripodBoss::tryDamageDemo() {
    setNerve(GET_NERVE(TripodBoss, TripodBossNrvTryStartDemo));

    if (MR::tryStartDemo(this, "ダメージ")) {
        setNerve(GET_NERVE(TripodBoss, TripodBossNrvDamageDemo));

        return true;
    }

    return false;
}

void TripodBoss::requestEndDamageDemo() {
    endDemo("ダメージ");
    setNerve(GET_NERVE(TripodBoss, TripodBossNrvWait));

    Vec3f starPiecePos;
    mBodyMtx.mult(sAppearStarPieceOffset, starPiecePos);

    Vec3f yDir;
    mBodyMtx.getYDir(yDir);

    MR::appearStarPieceToDirection(this, starPiecePos, yDir, 24, cNum_requestEndDamageDemo_f1, cNum_requestEndDamageDemo_f2, false);
    MR::startSound(this, "SE_OJ_STAR_PIECE_BURST_F");
}

void TripodBoss::exeTryStartDemo() {
}

void TripodBoss::exeNonActive() {
}

void TripodBoss::exeWait() {
    MR::startLevelSound(this, "SE_BM_LV_TRIPOD_BOTTOM_MOVE");
    calcBodyMovement();
    calcLegMovement();

    if (!tryBreak()) {
        tryStartStep();
    }
}

void TripodBoss::exeStep() {
    if (MR::isFirstStep(this)) {
        TripodBossStepSequence* stepSequence = getCurrentStepSequence();

        if (stepSequence != nullptr) {
            s32 leg = stepSequence->getCurrentLeg();

            if (mLegs[leg]->canStep()) {
                mLegs[leg]->requestStepTarget(stepSequence->getCurrentStepPoint());
            } else {
                stepSequence->nextStep();
            }
        }
    }

    calcBodyMovement();
    calcLegMovement();
    MR::startLevelSound(this, "SE_BM_LV_TRIPOD_BOTTOM_MOVE");

    if (!tryBreak() && !tryDamage() && !tryChangeSequence()) {
        tryWaitStep();
    }
}

void TripodBoss::exeWaitStep() {
    MR::startLevelSound(this, "SE_BM_LV_TRIPOD_BOTTOM_MOVE");
    calcBodyMovement();
    calcLegMovement();

    if (!tryBreak() && !tryLeaveLegOutOfPlayer() && !tryChangeSequence() && !tryEndSequence()) {
        tryNextStep();
    }
}

void TripodBoss::exeChangeSequence() {
    MR::startLevelSound(this, "SE_BM_LV_TRIPOD_BOTTOM_MOVE");
    calcBodyMovement();
    calcLegMovement();

    if (!tryBreak()) {
        tryNextSequence();
    }
}

void TripodBoss::exeLeaveLegOutOfPlayer() {
    MR::startLevelSound(this, "SE_BM_LV_TRIPOD_BOTTOM_MOVE");
    calcBodyMovement();
    calcLegMovement();
    tryEndLeaveLegOutOfPlayer();
}

// After 30 steps the body shakes along its height, 3 steps down then 3 up, sinking slowly (79.5 against 80).
void TripodBoss::exeDamage() {
    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfMilliseconds(500))) {
        Vec3f outward = mBodyPos - mMovableArea->mCenter;

        MR::normalizeOrZero(&outward);

        if (getNerveStep() % 6 < 3) {
            mBodyVelocity -= outward * cNum_exeDamage_f1;
        } else {
            mBodyVelocity += outward * cNum_exeDamage_f2;
        }
    }

    calcBodyMovement();
    calcLegMovement();
    MR::startLevelSound(this, "SE_BM_LV_TRIPOD_BOTTOM_MOVE");
    tryEndDamage();
}

void TripodBoss::exeStartDemo() {
    if (MR::isFirstStep(this)) {
        startDemo();
        MR::startBckPlayer("BattleWait");
        MR::stopStageBGM(10);
        mDemoBlendRate = 1.0f;
        MR::startAnimCameraTargetSelf(this, mEventCamera, "StartDemo", 0, 1.0f);
        MR::startBck(this, "StartDemo");
    }

    if (MR::isLessStep(this, NebulaFrameClock::stepsOfSeconds(2))) {
        MR::startLevelSound(this, "SE_BM_LV_TRIPOD_SIREN");
    }

    if (MR::isStep(this, NebulaFrameClock::stepsOfSeconds(2))) {
        MR::startBossBGM(MR::BossBgmID_TripodBossA);
    }

    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(2))) {
        MR::startLevelSound(this, "SE_BM_LV_TRIPOD_START_DEMO");
    }

    calcDemoMovement();

    if (MR::isBckStopped(this)) {
        endDemo("開始");
        MR::endAnimCamera(this, mEventCamera, "StartDemo", 150, true);
        initLegIKPlacement();
        setNerve(GET_NERVE(TripodBoss, TripodBossNrvWait));
    }
}

void TripodBoss::exeDamageDemo() {
    if (MR::isFirstStep(this)) {
        MR::stopStageBGM(30);
        startDemo();
    }

    MR::startLevelSound(this, "SE_BM_LV_TRIPOD_SIREN");
    MR::startLevelSound(this, "SE_BM_LV_TRIPOD_MID_DEMO");

    if (MR::isStep(this, sKillerGeneraterIncreaseSeTiming)) {
        MR::startSound(this, "SE_BM_TRIPOD_CANNON_APPEAR");
    }

    mIsBgmChangePending = true;
}

void TripodBoss::exePainDemo() {
    if (MR::isFirstStep(this)) {
        bool isOnGround = MR::isOnGroundPlayer();

        MR::startSound(this, "SE_BM_TRIPOD_HALT");
        MR::stopStageBGM(30);
        startDemo();

        if (isOnGround) {
            MR::startBckPlayer("BattleWait");
        }

        if (mDummyModel != nullptr) {
            MR::startBrk(mDummyModel, "Recover");
        }

        mDummyModelSpinSpeed = cNum_exePainDemo_f1;
        mDemoBlendRate = 0.0f;
    }

    if (MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(1500))) {
        Pos3f mtx(getBaseMtx());
        Vec3f marioPos;
        mtx.mult(sEndMarioPosition, marioPos);
        mtx.setTrans(marioPos);
        MR::setPlayerBaseMtx(mtx);
        MR::startBckPlayer("Wait");
        MR::startAnimCameraTargetSelf(this, mEventCamera, "EndDemo", 0, 1.0f);
        MR::startBck(this, "EndDemo");
        MR::emitEffect(this, "BreakLight");
    }

    if (MR::isLessStep(this, NebulaFrameClock::stepsOfMilliseconds(1500))) {
        mDummyModelOffset.y = MR::calcNerveEaseInOutValue(this, 70, cNum_exePainDemo_f2, cNum_exePainDemo_f3);
    } else {
        mDummyModelOffset.y = MR::calcNerveEaseInValue(this, NebulaFrameClock::stepsOfMilliseconds(1500), NebulaFrameClock::stepsOfMilliseconds(2500), cNum_exePainDemo_f4, cNum_exePainDemo_f5);
    }

    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfMilliseconds(1500))) {
        MR::startLevelSound(this, "SE_BM_LV_TRIPOD_END_DEMO");
        MR::startLevelSound(this, "SE_BM_LV_TRIPOD_BREAK_LIGHT");
        calcDemoMovement();

        mDemoBlendRate += 1.0f / static_cast< f32 >(sDemoBlendSteps);

        if (mDemoBlendRate > 1.0f) {
            mDemoBlendRate = 1.0f;
        }

        if (MR::isBckStopped(this)) {
            if (mDummyModel != nullptr) {
                mDummyModel->kill();
            }

            setNerve(GET_NERVE(TripodBoss, TripodBossNrvBreakDownDemo));
        }
    }
}

void TripodBoss::exeBreakDownDemo() {
    if (MR::isFirstStep(this)) {
        MR::tryRumblePadStrong(this, PLAYER_1);
        MR::shakeCameraStrong();
        MR::startSound(this, "SE_BM_TRIPOD_ALL_BREAK");
    }

    if (MR::isGreaterStep(this, sBreakDownTime)) {
        setNerve(GET_NERVE(TripodBoss, TripodBossNrvExplosionDemo));
    }
}

void TripodBoss::exeExplosionDemo() {
    if (MR::isFirstStep(this)) {
        MR::tryRumblePadVeryStrongLong(this, PLAYER_1);
    }

    if (MR::isStep(this, sHeadExplodeSeTiming)) {
        MR::startSound(this, "SE_BM_TRIPOD_KILL_HEAD");
    }

    if (MR::isGreaterStep(this, sExplosionTime)) {
        endDemo("破壊");
        MR::endAnimCamera(this, mEventCamera, "EndDemo", 0, true);
        kill();
    }
}

bool TripodBoss::isStopLeg(s32 idx) const {
    return idx >= 0 && idx < static_cast< s32 >(ARRAY_SIZE(mLegs)) && mLegs[idx]->isStop();
}

bool TripodBoss::isStopAllLeg() const {
    for (u32 i = 0; i < ARRAY_SIZE(mLegs); i++) {
        if (!mLegs[i]->isStop()) {
            return false;
        }
    }

    return true;
}

bool TripodBoss::isStarted() const {
    return !isNerve(GET_NERVE(TripodBoss, TripodBossNrvNonActive));
}

bool TripodBoss::isDemo() const {
    return isStartDemo() || isDamageDemo() || isEndDemo();
}

bool TripodBoss::isStartDemo() const {
    return isNerve(GET_NERVE(TripodBoss, TripodBossNrvStartDemo));
}

bool TripodBoss::isDamageDemo() const {
    return isNerve(GET_NERVE(TripodBoss, TripodBossNrvDamageDemo));
}

bool TripodBoss::isEndDemo() const {
    return isEndPainDemo() || isEndBreakDownDemo() || isEndExplosionDemo();
}

bool TripodBoss::isEndPainDemo() const {
    return isNerve(GET_NERVE(TripodBoss, TripodBossNrvPainDemo));
}

bool TripodBoss::isEndBreakDownDemo() const {
    return isNerve(GET_NERVE(TripodBoss, TripodBossNrvBreakDownDemo));
}

bool TripodBoss::isEndExplosionDemo() const {
    return isNerve(GET_NERVE(TripodBoss, TripodBossNrvExplosionDemo));
}

bool TripodBoss::isBroken() const {
    return isEndBreakDownDemo() || isEndExplosionDemo() || MR::isDead(this);
}

bool TripodBoss::isRideMario() const {
    return mRideMarioState == RIDE_ON || mRideMarioState == RIDE_START;
}

bool TripodBoss::isLeaveMarioNow() const {
    return mRideMarioState == RIDE_LEAVE;
}

void TripodBoss::setJointAttachBaseMatrix(const Pos3f& rPos, s32 idx) {
    // A placement bone id with no bone (index -1) has nothing to set.
    if (idx < 0 || idx >= cBoneNum) {
        return;
    }

    mBossBones[idx].setAttachBaseMatrix(rPos);
}

void TripodBoss::addStepPoint(TripodBossStepPoint* pPoint) {
    s32 idx = pPoint->mSequenceIndex;
    Vec3f nearPos;
    mMovableArea->calcNearLandingPosition(&nearPos, pPoint->mStepPosition);
    Vec3f landingNormal;
    mMovableArea->calcLandingNormal(&landingNormal, nearPos);
    Vec3f landingFront;
    mMovableArea->calcLandingFront(&landingFront, nearPos);
    pPoint->setStepPosition(nearPos);
    pPoint->setStepNormal(landingNormal);
    pPoint->setStepFront(landingFront);
    while (idx >= mStepSequences.size()) {
        mStepSequences.grow();
    }
    for (; mStepSequenceNum <= idx; mStepSequenceNum++) {
        mStepSequences[mStepSequenceNum] = new TripodBossStepSequence();
    }
    mStepSequences[idx]->addStepPoint(pPoint);
}

TripodBossStepSequence* TripodBoss::findStepSequence(s32 seq) const {
    return seq >= 0 && seq < mStepSequenceNum ? mStepSequences[seq] : nullptr;
}

void TripodBoss::getBodyMatrix(Pos3f* pMtx) const {
    pMtx->set(mBodyMtx);
}

void TripodBoss::getJointMatrix(Pos3f* pMtx, s32 boneID) const {
    // No bone (or not yet attached): the identity.
    if (boneID < 0 || boneID >= cBoneNum || mBossBones[boneID].mAttachMtx == nullptr) {
        pMtx->identity();
        return;
    }

    pMtx->set(*mBossBones[boneID].mAttachMtx);
}

void TripodBoss::getJointAttachMatrix(Pos3f* pMtx, s32 boneID) const {
    if (boneID < 0 || boneID >= cBoneNum || mBossBones[boneID].mAttachMtx == nullptr) {
        pMtx->identity();
        return;
    }

    pMtx->concat(*mBossBones[boneID].mAttachMtx, mBossBones[boneID].mLocalMtx);
}

void TripodBoss::requestStartStepSequence(s32 seq) {
    TripodBossStepSequence* sequence = findStepSequence(seq);
    if (sequence == nullptr || sequence->isEmpty()) {
        return;
    }

    if (mCurrentStepSeq == -1) {
        mNextStepSeq = seq;
    } else if (mCurrentStepSeq == seq) {
        mNextStepSeq = seq;
    } else if (mNextStepSeq == -1) {
        mNextStepSeq = seq;
    }
}

TripodBossStepSequence* TripodBoss::getCurrentStepSequence() {
    if (mCurrentStepSeq == -1) {
        return nullptr;
    }

    return mStepSequences[mCurrentStepSeq];
}

TripodBossStepSequence* TripodBoss::getNextStepSequence() {
    if (mNextStepSeq == -1) {
        return nullptr;
    }

    return mStepSequences[mNextStepSeq];
}

// Outward from the area center at rFootPos, across the base axis.
void TripodBoss::calcLegUpVector(Vec3f* pUp, const Vec3f& rFootPos) {
    Vec3f up = rFootPos - mMovableArea->mCenter;
    MR::normalizeOrZero(&up);
    up.orthogonalize(mMovableArea->mBaseAxis);
    MR::normalizeOrZero(&up);
    pUp->set(up);
}

void TripodBoss::calcDemoMovement() {
    Pos3f mtx(MR::getJointMtx(this, "Body"));
    MR::blendMtx(mDemoStartMtx, mtx, mDemoBlendRate, mBodyMtx);
    mBodyMtx.getTrans(mBodyPos);

    // The demo animation drives each foot by its joint; a joint scale over 1.5 marks a landing.
    for (u32 i = 0; i < ARRAY_SIZE(mLegs); i++) {
        Pos3f legMtx(MR::getJointMtx(this, sLegBoneNameTable[i]));
        Vec3f footPos;
        legMtx.getTrans(footPos);
        Vec3f legScale;
        legMtx.getScale(legScale);
        mLegs[i]->setForceEndPoint(footPos);
        mLegs[i]->setDemoEffectTiming(legScale.x > cNum_calcDemoMovement_f1);
    }

    calcLegMovement();
}

void TripodBoss::calcBodyMovement() {
    addAccelToWeightPosition();
    mBodyPos += mBodyVelocity;
    mBodyVelocity.x *= cNum_calcBodyMovement_f1;
    mBodyVelocity.y *= cNum_calcBodyMovement_f2;
    mBodyVelocity.z *= cNum_calcBodyMovement_f3;
    MR::makeMtxTR(mBodyMtx, mBodyPos, mRotation);
}

void TripodBoss::calcLegMovement() {
    for (u32 i = 0; i < ARRAY_SIZE(mLegs); i++) {
        mLegs[i]->movement();
    }
}

// The body is pulled toward the point above the feet: the bounds center of the feet bearing weight, blended 30%
// toward the center of all feet, lifted to the body height over the area sphere.
void TripodBoss::addAccelToWeightPosition() {
    Box3f weightBounds;
    Box3f allBounds;
    Vec3f weightCenter;
    Vec3f allCenter;

    weightBounds.i.set(mWeightPos);
    weightBounds.f.set(mWeightPos);
    allBounds.i.set(mWeightPos);
    allBounds.f.set(mWeightPos);

    for (u32 i = 0; i < ARRAY_SIZE(mLegs); i++) {
        if (getLeg(i)->canWeighting()) {
            weightBounds.extend(getLeg(i)->mForceEndPoint);
        }

        allBounds.extend(getLeg(i)->mForceEndPoint);
    }

    weightCenter.lerp(weightBounds.f, weightBounds.i, cNum_addAccelToWeightPosition_f1);
    allCenter.lerp(allBounds.f, allBounds.i, cNum_addAccelToWeightPosition_f2);

    Vec3f footCenter;
    MR::vecBlend(allCenter, weightCenter, &footCenter, cNum_addAccelToWeightPosition_f3);

    footCenter.killElement(footCenter - mMovableArea->mCenter, mMovableArea->mBaseAxis);
    f32 height = mMovableArea->mRadius;
    height = mBodyHeight + height;
    height = mWeightHeightOffset + height;
    Vec3f dir = mMovableArea->mBaseAxis * height + footCenter;
    MR::normalizeOrZero(&dir);
    Vec3f weightPosition = mMovableArea->mCenter + dir * height;
    Vec3f toWeight = weightPosition - mBodyPos;
    f32 dist = toWeight.length();
    if (dist < cNum_addAccelToWeightPosition_f4) {
        dist = cNum_addAccelToWeightPosition_f5;
    }

    toWeight *= (1.0f / dist);
    mBodyVelocity += toWeight * cNum_addAccelToWeightPosition_f6;
}

void TripodBoss::calcClippingSphere() {
    Box3f bounds;
    bounds.i.set(mBodyPos);
    bounds.f.set(mBodyPos);

    for (u32 i = 0; i < ARRAY_SIZE(mLegs); i++) {
        bounds.extend(mLegs[i]->mForceEndPoint);
    }

    mClipCenter.lerp(bounds.f, bounds.i, cNum_calcClippingSphere_f1);
}

void TripodBoss::clippingModel() {
    if (MR::isJudgedToClipFrustum(mClipCenter, mClipRadius)) {
        if (!mIsPartsHidden) {
            MR::hideTripodBossParts();
            mIsPartsHidden = true;
        }
    } else if (mIsPartsHidden) {
        MR::showTripodBossParts();
        mIsPartsHidden = false;
    }
}

void TripodBoss::startDemo() {
    MR::onCalcAnim(this);
    mDemoStartMtx.set(mBodyMtx);
    if (mDummyModel != nullptr) {
        MR::requestMovementOn(mDummyModel);
    }

    MR::requestMovementTripodBossParts();

    for (u32 i = 0; i < ARRAY_SIZE(mLegs); i++) {
        mLegs[i]->requestStartDemo();
    }
}

void TripodBoss::endDemo(const char* pName) {
    MR::endDemo(this, pName);
    MR::offCalcAnim(this);

    for (u32 i = 0; i < ARRAY_SIZE(mLegs); i++) {
        mLegs[i]->requestEndDemo();
    }
}

// The player rides while a boss part's gravity holds them, and for sRideMarioKeepTime steps after.
void TripodBoss::checkRideMario() {
    GravityInfo info;
    Vec3f gravity;
    MR::calcGravityAndMagnetVector(this, *MR::getPlayerPos(), &gravity, &info, 0);

    if (info.mGravityInstance != nullptr && MR::isTripoddBossParts(static_cast< const NameObj* >(info.mGravityInstance->mHost))) {
        mRideMarioTimer = sRideMarioKeepTime;
    } else if (mRideMarioTimer > 0) {
        mRideMarioTimer--;
    }

    if (mRideMarioTimer > 0) {
        mRideMarioState = isRideMario() ? RIDE_ON : RIDE_START;
    } else {
        mRideMarioState = isRideMario() ? RIDE_LEAVE : RIDE_OFF;
    }
}

const Pos3f* TripodBoss::getLegMatrixPtr(PART_ID partID, SUB_PART_ID subPartID) const {
    bool isValidLegPartID = partID >= PART_ID_LEFT_LEG && partID <= PART_ID_RIGHT_LEG;

    if (!isValidLegPartID) {
        return nullptr;
    }

    switch (subPartID) {
    case SUB_PART_ID_ROOT_LOCAL_Y:
        return &mLegs[partID]->getRootLocalYMatrix();
    case SUB_PART_ID_ROOT_LOCAL_YZ:
        return &mLegs[partID]->getRootLocalYZMatrix();
    case SUB_PART_ID_ROOT_JOINT:
        return &mLegs[partID]->getRootJointMatrix();
    case SUB_PART_ID_MIDDLE_JOINT:
        return &mLegs[partID]->getMiddleJointMatrix();
    case SUB_PART_ID_ANKLE_LOCAL_X:
        return &mLegs[partID]->getAnkleLocalXMatrix();
    case SUB_PART_ID_ANKLE_LOCAL_XZ:
        return &mLegs[partID]->getAnkleLocalXZMatrix();
    case SUB_PART_ID_END_JOINT:
        return &mLegs[partID]->getEndJointMatrix();
    default:
        return nullptr;
    }
}

void TripodBoss::changeBgmState() {
    if (mIsBgmChangePending && !isDemo()) {
        if (!MR::isPlayingStageBgm()) {
            MR::startBossBGM(MR::BossBgmID_TripodBossB);
        }

        mIsBgmChangePending = false;
    }

    const s32 bgmState = isRideMario() ? sBgmStateRide : sBgmStateNormal;
    if (mBgmState != bgmState) {
        MR::setStageBGMState(bgmState, 60);
    }

    mBgmState = bgmState;
}

// Bones 0-6, 7-13 and 14-20 are legs 0, 1 and 2; bone 21 (part 3) is the body.
s32 TripodBoss::getPartIDFromBoneID(s32 boneID) {
    if (boneID >= 0 && boneID < cBodyBone) {
        return boneID / sBonesPerLeg;
    }

    return boneID == cBodyBone ? PART_ID_MAX : -1;
}

// The inverse of rPos with unit-length rotation columns.
void TripodBossBone::setAttachBaseMatrix(const Pos3f& rPos) {
    mLocalMtx.invert(rPos);

    for (s32 column = 0; column < 3; column++) {
        f32 invLen = FloatUtil::inv_sqrt((mLocalMtx.get(0, column) * mLocalMtx.get(0, column)) + (mLocalMtx.get(1, column) * mLocalMtx.get(1, column)) +
                                         (mLocalMtx.get(2, column) * mLocalMtx.get(2, column)));

        mLocalMtx.mMtx[0][column] = invLen * mLocalMtx.get(0, column);
        mLocalMtx.mMtx[1][column] = invLen * mLocalMtx.get(1, column);
        mLocalMtx.mMtx[2][column] = invLen * mLocalMtx.get(2, column);
    }
}

namespace MR {
    NameObj* createTripodBoss(const char* pName) {
        return new TripodBoss(pName);
    }

    NameObj* createTripod2Boss(const char* pName) {
        return new TripodBoss(pName);
    }
};  // namespace MR
