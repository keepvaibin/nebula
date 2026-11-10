#include "Game/Enemy/BegomanBase.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/Enemy/BegomanBaby.hpp"
#include "Game/Enemy/WalkerStateBindStarPointer.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/WaterInfo.hpp"
#include "Game/MapObj/ElectricRailHolder.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorStateUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointController.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_initEffectAndStarPointerBind_f1, "initEffectAndStarPointerBind.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f1, "initSensor.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f2, "initSensor.f2", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f3, "initSensor.f3", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f4, "initSensor.f4", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_isFallNextMove_f1, "isFallNextMove.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcDirectionAwayFromRail_f1, "calcDirectionAwayFromRail.f1", 90.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_preventSwingby_f1, "preventSwingby.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_preventSlopeBlow_f1, "preventSlopeBlow.f1", 0.5f);

namespace {
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hReturnWaitParam, {0.0f, 1.0f, 0.95f, 0.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hReturnMoveParam, {0.2f, 1.0f, 0.95f, 3.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hBrakeParam, {0.0f, 1.0f, 0.7f, 3.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hBrakeOnAirParam, {0.0f, 1.0f, 0.95f, 3.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hKeepDistanceFarParam, {0.35f, 1.0f, 0.95f, 3.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hKeepDistanceNearParam, {-0.22f, 1.0f, 0.95f, 3.0f});
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, hKeepDistanceWaitParam, {0.0f, 1.0f, 0.95f, 3.0f});
    // Rail contact must last longer than this before it knocks the begoman away.
    NEBULA_TUNABLE(s32, hElectricTouchTime, 40);
    NEBULA_TUNABLE(f32, hReboundWallAddVel, 5.0f);
    NEBULA_TUNABLE(f32, hNoCalcWaitRotate, 0.15f);
    NEBULA_TUNABLE(f32, hBlendGravityRate, 0.1f);
    NEBULA_TUNABLE(f32, hRailPushH, 10.0f);
    NEBULA_TUNABLE(f32, hRailPushJump, 20.0f);
    NEBULA_TUNABLE(f32, hRailPushHBoss, 30.0f);
    NEBULA_TUNABLE(f32, hRailPushJumpBoss, 30.0f);
    // Light pushes: the side rail sensor, and body contact before the knock.
    NEBULA_TUNABLE(f32, hPushedRailPush, 0.5f);
    NEBULA_TUNABLE(f32, hBodyRailPush, 8.0f);
    NEBULA_TUNABLE(f32, hTurnStartDegree, 3);
    NEBULA_TUNABLE(f32, hTurnEndDegree, 10);
    NEBULA_TUNABLE(f32, hIsFaceToPlayerDegree, 30);
    NEBULA_TUNABLE(f32, hPursueLimitTurnDegree, 3.0f);
    NEBULA_TUNABLE(f32, hPursueRange, 2000.0f);
    const s32 hPursueLimitTime = NebulaFrameClock::stepsOfSeconds(3);
    NEBULA_TUNABLE(s32, hPursueAnimFrame, 10);
    NEBULA_TUNABLE(s32, hPursueDashFrame, 18);
    NEBULA_TUNABLE(f32, hPursueDashVelocity, 15.0f);
    const s32 hTurnTime = NebulaFrameClock::stepsOfSeconds(1);
    const s32 hTurnTimeCanNotAttack = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(s32, hBrakeTime, 50);
    const s32 hProvokeTime = NebulaFrameClock::stepsOfSeconds(2);
    const s32 hTiredTime = NebulaFrameClock::stepsOfSeconds(2);
    NEBULA_TUNABLE(s32, hTiredCount, 180);
    const s32 hReturnWaitTime = NebulaFrameClock::stepsOfSeconds(3);
    NEBULA_TUNABLE(f32, hKeepDistanceToWaitDist, 1200.0f);
    NEBULA_TUNABLE(f32, hKeepDistSideVel, 0.6f);
    NEBULA_TUNABLE(s32, hReceiveLimitTime, 2);
    const s32 hPermitLimitTime = NebulaFrameClock::stepsOfMilliseconds(500);
    // Closer than this the player wakes a waiting begoman.
    NEBULA_TUNABLE(f32, hWakeDist, 1000.0f);
    // A rail ahead within this cosine of the face keeps a waiting begoman from attacking.
    NEBULA_TUNABLE(f32, hWaitRailAheadDot, 0.5f);
    NEBULA_TUNABLE(f32, hFallCheckDist, 150.0f);
    NEBULA_TUNABLE(f32, hFallCheckDepth, 150.0f);
    const f32 hInitPosNearDistSq = 200.0f * 200.0f;
    NEBULA_TUNABLE(f32, hStarPointerRadiusRate, 0.75f);
    NEBULA_TUNABLE(f32, hLaunchDamp, 0.98f);
    NEBULA_TUNABLE(f32, hLaunchGravity, 1.0f);
    NEBULA_TUNABLE(f32, hGroundReboundRate, 0.5f);
    NEBULA_TUNABLE(f32, hGroundReboundCosMin, 100.0f);
    NEBULA_TUNABLE(s32, hGroupClippingNum, 16);
    NEBULA_TUNABLE(f32, hPermitterNoDist, 99999.0f);
    const BegomanSound sound_normal = {"SE_EM_LV_BEGOMAN_DASH"};
    const BegomanSound sound_boss = {"SE_BM_LV_BBEGO_DASH"};

    // Places the dead ones of pList on a circle of radius around pActor, moving outward and up.
    template < typename T >
    void launchCore(LiveActor* pActor, T* const* pList, s32 num, f32 radius, f32 velH, f32 velV, const Vec3f* pFront) {
        Vec3f side;
        Vec3f front;

        if (pFront == nullptr) {
            MR::calcFrontVec(&front, pActor);
            MR::calcSideVec(&side, pActor);
        } else {
            front.set(*pFront);
            NebulaVecCrossProduct(pActor->getGravity(), &front, &side);
            NebulaVecCrossProduct(&side, pActor->getGravity(), &front);
        }

        f32 angle = 0.0f;

        for (s32 i = 0; i < num; i++) {
            if (!MR::isDead(pList[i])) {
                continue;
            }

            Vec3f outward(side);
            outward.scale(MR::cos(angle));
            outward += (front * MR::sin(angle));

            pList[i]->mPosition.set(*pActor->getPosition() + outward * radius);
            pList[i]->mVelocity.set(outward * velH - *pActor->getGravity() * velV);
            pList[i]->mFaceVec.set(outward);

            angle += TWO_PI / (num);
        }
    }
};  // namespace

namespace NrvBegomanAttackPermitter {
    NEW_NERVE(HostTypeNrvWait, BegomanAttackPermitter, Wait);
    NEW_NERVE(HostTypeNrvReceive, BegomanAttackPermitter, Receive);
    NEW_NERVE(HostTypeNrvPermit, BegomanAttackPermitter, Permit);
};  // namespace NrvBegomanAttackPermitter

BegomanBase::BegomanBase(const char* pName)
    : LiveActor(pName), mBaseDelegator(), mFaceVec(0.0f, 0.0f, 1.0f), mTargetVec(0.0f, 0.0f, 1.0f), mLastGravity(0.0f, 0.0f, -1.0f), mAnimScale(1.0f, 1.0f, 1.0f),
      mPoseQuat(0, 0, 0, 1), mSpinQuat(0, 0, 0, 1), mTiredCounter(), mElectricCounter(), mInitPos(0.0f, 0.0f, 0.0f), mScaleControler(), mStarPointBind(),
      mIsTouchElectricRail(false), mCanTrySetReturn(), mInvertSideVec(false) {
    lumacore::setClass(this, lumacore::kClassId< BegomanBase >);
}

const BegomanSound* BegomanBase::getSoundNormal() {
    return &::sound_normal;
}

const BegomanSound* BegomanBase::getSoundBoss() {
    return &::sound_boss;
}

void BegomanBase::initCore(const TableIter& rIter, const char* pModelArcName, bool) {
    MR::createSceneObj(SceneObj_BegomanAttackPermitter);
    MR::useStageSwitchWriteDead(this, rIter);
    MR::useStageSwitchReadA(this, rIter);
    initModelManagerWithAnm(pModelArcName, nullptr, false);

    if (MR::isValidInfo(rIter)) {
        MR::initDefaultPos(this, rIter);
        mInitPos.set(mPosition);
        MR::setGroupClipping(this, rIter, ::hGroupClippingNum);

        // Placement arg 0 set: the begoman returns home while switch A is off.
        s32 returnHome = -1;
        MR::getPlacementArg0NoInit(rIter, &returnHome);
        mCanTrySetReturn = returnHome != -1;
    }

    MR::connectToSceneEnemy(this);

    mBaseDelegator = MR::createJointDelegatorWithNullChildFunc(this, &BegomanBase::calcJointLocator1, "Locator1");
    MR::onCalcGravity(this);
    MR::initLightCtrl(this);

    mPoseQuat.setEulerDegree(mRotation.x, mRotation.y, mRotation.z);
    mPoseQuat.getZDir(mFaceVec);
    mRotation.set(0.0f);
}

void BegomanBase::initEffectAndStarPointerBind() {
    initEffect(1);
    MR::startAction(this, "Wait");
    calcAnim();

    f32 pointerRadius;
    MR::calcModelBoundingRadius(&pointerRadius, this);

    pointerRadius *= ::hStarPointerRadiusRate;
    MR::initStarPointerTarget(this, pointerRadius, Vec3f(0.0f, cNum_initEffectAndStarPointerBind_f1 * pointerRadius, 0.0f));

    mScaleControler = new AnimScaleController(nullptr);
    mScaleControler->setParamTight();
    mStarPointBind = new WalkerStateBindStarPointer(this, mScaleControler);
}

void BegomanBase::initAfterPlacement() {
    MR::trySetMoveLimitCollision(this);
}

void BegomanBase::initShadow(f32 radius, const char* pJointName) {
    MR::initShadowVolumeSphere(this, radius);
    MR::setShadowDropPositionAtJoint(this, nullptr, pJointName, Vec3f(0.0f, 0.0f, 0.0f));
}

void BegomanBase::initEffect(s32 extraEffectNum) {
    initEffectKeeper(extraEffectNum + 3, nullptr);

    MR::addEffect(this, "BegomanSpark");
    MR::addEffect(this, "BegomanHit");
    MR::addEffect(this, "BegomanFailureHit");

    MR::setEffectHostSRT(this, "BegomanSpark", nullptr, nullptr, nullptr);
    MR::setEffectHostSRT(this, "BegomanHit", nullptr, nullptr, nullptr);
    MR::setEffectHostSRT(this, "BegomanFailureHit", nullptr, nullptr, nullptr);

    MR::setEffectName(this, "BegomanSpark", "Spark");
    MR::setEffectName(this, "BegomanHit", "Hit");
    MR::setEffectName(this, "BegomanFailureHit", "FailureHit");
}

void BegomanBase::initSensor(s32 extraSensorNum, f32 radius, f32 pushedRailRadius, const char* pSensorJointName) {
    f32 yScale = mScale.y;
    initHitSensor(extraSensorNum + 3);

    MR::addHitSensor(this, "check", ATYPE_EYE, 1, radius * yScale, Vec3f(0.0f, cNum_initSensor_f1 * radius * yScale, cNum_initSensor_f2 * radius * yScale));
    MR::addHitSensorAtJoint(this, "body", pSensorJointName, ATYPE_BEGOMAN, 32, radius * yScale, Vec3f(0.0f, cNum_initSensor_f3 * radius * yScale, 0.0f));
    MR::addHitSensorAtJoint(this, "pushed_rail", pSensorJointName, ATYPE_EYE, 32, pushedRailRadius * yScale,
                            Vec3f(0.0f, cNum_initSensor_f4 * pushedRailRadius * yScale, 0.0f));
}

void BegomanBase::initUseSwitchB(const TableIter& rIter, const MR::FunctorBase& rFunctor) {
    if (MR::isValidInfo(rIter) && MR::useStageSwitchReadB(this, rIter)) {
        MR::listenStageSwitchOnB(this, rFunctor);
    }
}

void BegomanBase::appear() {
    LiveActor::appear();
    MR::onBind(this);
    MR::onCalcGravity(this);
    getSensor("body")->validate();
}

void BegomanBase::kill() {
    LiveActor::kill();

    if (MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
    }
}

void BegomanBase::control() {
    if (mScaleControler != nullptr) {
        mScaleControler->updateNerve();
    }

    updateTargetVec();

    Vec3f upVec;
    MR::calcUpVec(&upVec, this);

    // The pose leans toward the gravity up a step each frame.
    Vec3f blendVec;
    MR::vecBlend(upVec, -mGravity, &blendVec, ::hBlendGravityRate);
    MR::normalizeOrZero(&blendVec);

    if (MR::isNearZero(blendVec)) {
        blendVec.set(upVec);
    }

    MR::makeQuatUpFront(&mPoseQuat, blendVec, mFaceVec);
    mLastGravity.set(mGravity);

    Quat4f yRotationQuat;
    yRotationQuat.setRotate(0.0f, 1.0f, 0.0f, mRotation.y);
    NebulaQuatMultiply(&yRotationQuat, &mSpinQuat, &mSpinQuat);
}

bool BegomanBase::isPlayerWaking() const {
    return MR::calcDistanceToPlayer(mPosition) < ::hWakeDist && (!MR::isValidSwitchA(this) || MR::isOnSwitchA(this));
}

void BegomanBase::setNerveAttackOr(const Nerve* pAttackNerve, const Nerve* pOtherNerve) {
    if (requestAttack()) {
        setNerve(pAttackNerve);
    } else {
        setNerve(pOtherNerve);
    }
}

void BegomanBase::exeNoCalcWaitCore(f32 tolerance, const Nerve* pNerve) {
    if (MR::isFirstStep(this)) {
        MR::offBind(this);
        MR::offCalcGravity(this);
        MR::validateClipping(this);
    }

    updateRotateY(::hNoCalcWaitRotate, tolerance);
    mVelocity.zero();

    if (pNerve != nullptr && isPlayerWaking()) {
        MR::onBind(this);
        MR::onCalcGravity(this);
        setNerve(pNerve);
    }
}

void BegomanBase::finishNoCalcWait() {
    MR::onBind(this);
    MR::onCalcGravity(this);
    MR::invalidateClipping(this);
}

void BegomanBase::exeWaitCore(const MR::ActorMoveParam& rMoveParam, const Nerve* pAttackNerve, const Nerve* pNoAttackNerve, const Nerve* pFarNerve) {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Wait");
    }

    MR::moveAndTurnToDirection(this, &mFaceVec, mTargetVec, rMoveParam);
    reboundWallAndGround(&mFaceVec, false);

    if (!isNearInitPos() && trySetReturnNerve()) {
        return;
    }

    if (!(MR::calcDistanceToPlayer(mPosition) < ::hWakeDist)) {
        if (MR::isOnGround(this)) {
            setNerve(pFarNerve);
        }

        return;
    }

    if ((MR::isValidSwitchA(this) && !MR::isOnSwitchA(this)) || isFallNextMove(::hFallCheckDist, ::hFallCheckDepth)) {
        return;
    }

    Vec3f touchRailPos;

    if (!ElectricRailFunction::isTouchRail(getSensor("check"), &touchRailPos, nullptr)) {
        setNerveAttackOr(pAttackNerve, pNoAttackNerve);
        return;
    }

    Vec3f toRail(touchRailPos);
    toRail.sub(getSensor("check")->mPosition);
    MR::normalizeOrZero(&toRail);

    if (!MR::isNearZero(toRail) && !(::hWaitRailAheadDot < toRail.dot(mFaceVec))) {
        setNerveAttackOr(pAttackNerve, pNoAttackNerve);
    }
}

void BegomanBase::exeSignAttackCore(const MR::ActorMoveParam& rMoveParam, const Nerve* pNerve) {
    MR::moveAndTurnToDirection(this, &mFaceVec, mTargetVec, rMoveParam);
    reboundWallAndGround(&mFaceVec, false);

    if (!requestAttack()) {
        if (!trySetReturnNerve()) {
            setNerve(getNerveWait());
        }
    } else if (MR::isActionEnd(this)) {
        setNerve(pNerve);
    }
}

void BegomanBase::exePursueCore(const MR::ActorMoveParam& rMoveParam, const Nerve* pBrakeNerve, const Nerve* pTurnNerve, const BegomanSound& rSound,
                                f32 speedRate) {
    if (MR::isStep(this, ::hPursueAnimFrame)) {
        MR::startAction(this, "Attack");
    }

    if (MR::isGreaterEqualStep(this, ::hPursueAnimFrame)) {
        MR::startLevelSound(this, rSound.mSound);
    }

    if (MR::isStep(this, ::hPursueDashFrame)) {
        mVelocity = mFaceVec * ::hPursueDashVelocity * speedRate;
    }

    if (isFallNextMove(::hFallCheckDist, ::hFallCheckDepth)) {
        setNerve(pBrakeNerve);
        return;
    }

    MR::moveAndTurnToDirection(this, &mFaceVec, mTargetVec, {rMoveParam.mMoveSpeed * speedRate, rMoveParam.mGravity, rMoveParam.mDamp, rMoveParam.mTurnDegree});

    // The dash start sparks on the ground.
    reboundWallAndGround(&mFaceVec, !MR::isGreaterStep(this, ::hPursueDashFrame));

    if (!requestAttack()) {
        if (!trySetReturnNerve()) {
            setNerve(getNerveWait());
        }

        return;
    }

    if (ElectricRailFunction::isTouchRail(getSensor("check"), nullptr, nullptr)) {
        setNerve(pBrakeNerve);
        return;
    }

    if (!MR::isFaceToPlayerHorizontalDegree(this, mFaceVec, ::hIsFaceToPlayerDegree)) {
        MR::startAction(this, "Turn");
        setNerve(pTurnNerve);
        return;
    }

    // No return here: a far player or switch A off below still overrides the turn.
    if (MR::isGreaterStep(this, ::hPursueLimitTime)) {
        MR::turnDirection(this, &mFaceVec, mTargetVec, ::hPursueLimitTurnDegree);
        MR::startAction(this, "Turn");
        setNerve(pTurnNerve);
    }

    if (!trySetReturnNerve() && ::hPursueRange < MR::calcDistanceToPlayer(mPosition)) {
        setNerve(getNerveWait());
    }
}

void BegomanBase::exeTurnCore(const MR::ActorMoveParam& rMoveParam, const Nerve* pBrakeNerve, const Nerve* pPursueNerve, bool notCheckRail) {
    if (MR::isOnGround(this) && isFallNextMove(::hFallCheckDist, ::hFallCheckDepth)) {
        setNerve(pBrakeNerve);
        return;
    }

    f32 turnDegree = MR::calcNerveValue(this, ::hTurnTime, ::hTurnStartDegree, ::hTurnEndDegree);
    MR::moveAndTurnToDirection(this, &mFaceVec, mTargetVec, {rMoveParam.mMoveSpeed, rMoveParam.mGravity, rMoveParam.mDamp, turnDegree});
    reboundWallAndGround(&mFaceVec, true);

    if (!notCheckRail && ElectricRailFunction::isTouchRail(getSensor("check"), nullptr, nullptr)) {
        setNerve(pBrakeNerve);
        return;
    }

    if (trySetReturnNerve()) {
        return;
    }

    bool attackRequested = requestAttack();

    if (!attackRequested && MR::isGreaterStep(this, ::hTurnTimeCanNotAttack)) {
        setNerve(getNerveWait());
        return;
    }

    if (MR::isGreaterStep(this, ::hTurnTime)) {
        if (attackRequested && MR::isFaceToPlayerHorizontalDegree(this, mFaceVec, ::hIsFaceToPlayerDegree)) {
            setNerve(pPursueNerve);
            return;
        }

        if (::hPursueRange < MR::calcDistanceToPlayer(mPosition)) {
            setNerve(getNerveWait());
        }
    }
}

void BegomanBase::exeBrakeCore(const Nerve* pNerve) {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Brake");
        MR::vecKillElement(mVelocity, mFaceVec, &mVelocity);
    }

    if (MR::isOnGround(this)) {
        MR::moveAndTurnToDirection(this, &mFaceVec, mTargetVec, ::hBrakeParam);
    } else {
        MR::moveAndTurnToDirection(this, &mFaceVec, mTargetVec, ::hBrakeOnAirParam);
    }

    reboundWallAndGround(&mFaceVec, true);

    if (!isFallNextMove(::hFallCheckDist, ::hFallCheckDepth) && !ElectricRailFunction::isTouchRail(getSensor("check"), nullptr, nullptr) &&
        MR::isGreaterStep(this, ::hBrakeTime)) {
        MR::startAction(this, "Brake");
        setNerve(pNerve);
    }
}

void BegomanBase::exeStepBackCore(const MR::ActorMoveParam& rMoveParam, const Nerve* pNerve) {
    MR::moveAndTurnToDirection(this, &mFaceVec, mTargetVec, rMoveParam);

    if (MR::isOnGround(this)) {
        setNerve(pNerve);
    }
}

void BegomanBase::exeProvokeCore(const MR::ActorMoveParam& rMoveParam, const Nerve* pNerve) {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Provoke");
    }

    MR::moveAndTurnToDirection(this, &mFaceVec, mTargetVec, rMoveParam);
    reboundWallAndGround(&mFaceVec, false);

    // Provoking stands still: only the fall along gravity is kept.
    mVelocity.scale(mGravity.dot(mVelocity), mGravity);

    if (MR::isGreaterStep(this, ::hProvokeTime)) {
        setNerve(pNerve);
    }
}

void BegomanBase::exeHitReactionCore(const MR::ActorMoveParam& rMoveParam, const Nerve* pNerve) {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "HitReaction");
    }

    MR::moveAndTurnToDirection(this, &mFaceVec, mTargetVec, rMoveParam);
    reboundWallAndGround(&mFaceVec, false);

    if (MR::isActionEnd(this)) {
        setNerve(pNerve);
    }
}

void BegomanBase::exeTiredCore(const MR::ActorMoveParam& rMoveParam, const Nerve* pNerve) {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Tired");
    }

    MR::moveAndTurnToDirection(this, &mFaceVec, mTargetVec, rMoveParam);
    reboundWallAndGround(&mFaceVec, false);

    if (MR::isGreaterStep(this, ::hTiredTime)) {
        setNerve(pNerve);
    }
}

void BegomanBase::exeReturnCore(const Nerve* pNerve) {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Brake");
    }

    if (MR::isLessStep(this, ::hReturnWaitTime)) {
        MR::moveAndTurnToTarget(this, &mFaceVec, mInitPos, ::hReturnWaitParam);
    } else {
        MR::moveAndTurnToTarget(this, &mFaceVec, mInitPos, ::hReturnMoveParam);
    }

    reboundWallAndGround(&mFaceVec, false);

    if (isNearInitPos()) {
        setNerve(pNerve);
    }
}

void BegomanBase::exeKeepDistanceCore(const Nerve* pWaitNerve, const Nerve* pAttackNerve, const Nerve* pBrakeNerve, f32 farDist, f32 nearDist) {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Brake");
    }

    f32 distToPlayer = MR::calcDistanceToPlayer(mPosition);

    if (::hKeepDistanceToWaitDist < distToPlayer) {
        setNerve(pWaitNerve);
        return;
    }

    if (farDist < distToPlayer) {
        MR::moveAndTurnToPlayer(this, &mFaceVec, ::hKeepDistanceFarParam);
    } else if (distToPlayer < nearDist) {
        MR::moveAndTurnToPlayer(this, &mFaceVec, ::hKeepDistanceNearParam);
        addVelocityEscapeToSide(::hKeepDistSideVel);
    } else {
        MR::moveAndTurnToPlayer(this, &mFaceVec, ::hKeepDistanceWaitParam);
    }

    if (isFallNextMove(::hFallCheckDist, ::hFallCheckDepth)) {
        setNerve(pBrakeNerve);
        return;
    }

    reboundWallAndGround(&mFaceVec, false);

    if (requestAttack()) {
        setNerve(pAttackNerve);
        return;
    }

    trySetReturnNerve();
}

void BegomanBase::addVelocityEscapeToSide(f32 velocity) {
    Vec3f sideVec;
    MR::calcSideVec(&sideVec, this);

    if (mInvertSideVec) {
        sideVec.scale(-1.0f);
    }

    // Escape away from the player and from where the player is heading; the side flips for the next frame.
    if (0.0f < mTargetVec.dot(sideVec) || 0.0f < sideVec.dot(*MR::getPlayerVelocity())) {
        mInvertSideVec = !mInvertSideVec;
    }

    sideVec.setLength(velocity);
    MR::addVelocity(this, sideVec);
}

void BegomanBase::exeLaunch() {
    if (MR::isFirstStep(this)) {
        MR::invalidateHitSensors(this);
    }

    MR::applyVelocityDampAndGravity(this, ::hLaunchDamp, ::hLaunchDamp, ::hLaunchDamp, ::hLaunchDamp, ::hLaunchGravity);

    if (MR::isOnGround(this)) {
        MR::validateHitSensors(this);
        setNerve(getNerveWait());
    }
}

void BegomanBase::exeBindStarPointer() {
    MR::updateActorStateAndNextNerve(this, mStarPointBind, getNerveWait());
}

void BegomanBase::finishBindStarPointer() {
    mStarPointBind->kill();
}

bool BegomanBase::tryAndSetStarPointerBind(const Nerve* pNerve) {
    if (mStarPointBind->tryStartPointBind()) {
        setNerve(pNerve);
        return true;
    }

    return false;
}

bool BegomanBase::isNearInitPos() const {
    return mInitPos.squared(mPosition) < ::hInitPosNearDistSq;
}

bool BegomanBase::trySetReturnNerve() {
    if (MR::isValidSwitchA(this) && !MR::isOnSwitchA(this) && mCanTrySetReturn) {
        setNerveReturn();
        return true;
    }

    return false;
}

bool BegomanBase::incAndCheckTiredCounter() {
    if (mTiredCounter < ::hTiredCount) {
        mTiredCounter++;
    }

    return mTiredCounter == ::hTiredCount;
}

void BegomanBase::launchBegoman(LiveActor* pActor, BegomanBase** pBegomanArray, s32 numBegoman, f32 radius, f32 velH, f32 velV, const Vec3f* pFront) {
    launchCore(pActor, pBegomanArray, numBegoman, radius, velH, velV, pFront);

    for (s32 i = 0; i < numBegoman; i++) {
        pBegomanArray[i]->appear();
        pBegomanArray[i]->setNerveLaunch();
    }
}

void BegomanBase::launchBegomanBabyFromGuarder(LiveActor* pActor, BegomanBaby** pBabyArray, s32 numBegoman, f32 radius, f32 velH, f32 velV,
                                               const Vec3f* pFront) {
    launchCore(pActor, pBabyArray, numBegoman, radius, velH, velV, pFront);

    for (s32 i = 0; i < numBegoman; i++) {
        pBabyArray[i]->appearFromGuarder();
    }
}

void BegomanBase::launchBegomanBabyLauncher(LiveActor* pActor, BegomanBase* const* pBabyArray, s32 numBegoman, f32 radius, f32 velH, f32 velV,
                                            const Vec3f* pFront) {
    launchCore(pActor, pBabyArray, numBegoman, radius, velH, velV, pFront);

    for (s32 i = 0; i < numBegoman; i++) {
        static_cast< BegomanBaby* >(pBabyArray[i])->appearFromLaunch(pActor->mPosition, -pActor->mGravity);
    }
}

void BegomanBase::setNerveLaunch() {
}

void BegomanBase::updateRotateY(f32 target, f32 tolerance) {
    f32 difference = target - mRotation.y;

    if (0.0f < difference) {
        mRotation.y = MR::min(tolerance + mRotation.y, target);
    } else {
        mRotation.y = MR::max(mRotation.y - tolerance, target);
    }
}

void BegomanBase::updateTargetVec() {
    mTargetVec.set(*MR::getPlayerCenterPos());
    mTargetVec.sub(mPosition);
    MR::turnDirectionToGround(this, &mTargetVec);
}

bool BegomanBase::reboundPlaneWithEffect(const Vec3f& rNormal, f32 rate, f32 cosAngleMin, const char* pEffectName) {
    bool reflected = MR::calcReflectionVector(&mVelocity, rNormal, rate, cosAngleMin);

    if (pEffectName != nullptr && reflected) {
        Vec3f hitPos(getSensor("body")->mPosition);

        hitPos.sub(rNormal * getSensor("body")->mRadius);
        MR::emitEffectHit(this, hitPos, pEffectName);
    }

    return reflected;
}

void BegomanBase::reboundWallAndGround(Vec3f* pFront, bool emitGroundSpark) {
    if (MR::isOnGround(this)) {
        Vec3f groundNormal(*MR::getGroundNormal(this));

        if (emitGroundSpark) {
            MR::calcReflectionVector(&mVelocity, groundNormal, 0.0f, 0.0f);
            MR::emitEffect(this, "OnGroundSpark");
        } else {
            MR::calcReflectionVector(&mVelocity, groundNormal, ::hGroundReboundRate, ::hGroundReboundCosMin);
            MR::deleteEffect(this, "OnGroundSpark");
        }
    } else {
        MR::deleteEffect(this, "OnGroundSpark");
    }

    if (!MR::isBindedWallOfMap(this)) {
        return;
    }

    Vec3f wallNormal(*MR::getWallNormal(this));
    bool reboundWall = reboundPlaneWithEffect(wallNormal, 0.0f, 0.0f, "Spark");
    mVelocity.add(wallNormal * ::hReboundWallAddVel);

    if (reboundWall) {
        MR::startLevelSound(this, "SE_EM_LV_BEGOMAN_COLLI_WALL");
    }

    // The face follows the bounce.
    if (pFront != nullptr && !MR::isNearZero(mVelocity)) {
        pFront->set(mVelocity);
        MR::normalize(pFront);
    }
}

bool BegomanBase::isFallNextMove(f32 dist, f32 depth) {
    f32 scaledBinderRadius = MR::getBinderRadius(this);
    scaledBinderRadius = scaledBinderRadius * mScale.y * cNum_isFallNextMove_f1;

    // Standing still, the check runs along the face.
    if (MR::isNearZero(MR::getVelocityHorizon(this))) {
        return MR::isFallNextMove(mPosition, mFaceVec, mGravity, dist, scaledBinderRadius, depth, nullptr);
    }

    return MR::isFallNextMove(this, dist, scaledBinderRadius, depth, nullptr);
}

f32 BegomanBase::getRailPushVelHBoss() const {
    return ::hRailPushHBoss;
}

f32 BegomanBase::getRailPushJumpBoss() const {
    return ::hRailPushJumpBoss;
}

void BegomanBase::calcDirectionAwayFromRail(Vec3f* pOut, const Vec3f& rRailPos, const Vec3f& rRailDir) {
    // Across the rail, or straight back without a rail direction; turned to point away from the rail.
    Vec3f away(rRailDir);

    if (MR::isNearZero(away)) {
        away.set(-mFaceVec);
    } else {
        MR::rotateVecDegree(&away, mGravity, cNum_calcDirectionAwayFromRail_f1);
    }

    Vec3f toRail(rRailPos);
    toRail.sub(getSensor("body")->mPosition);

    if (0.0f < toRail.dot(away)) {
        away = -away;
    }

    MR::vecKillElement(away, mGravity, &away);
    MR::normalizeOrZero(&away);
    pOut->set(away);
}

void BegomanBase::addVelocityOnPushedFromElectricRail(const Vec3f& rRailPos, const Vec3f& rRailDir) {
    Vec3f directionAwayFromRail;
    calcDirectionAwayFromRail(&directionAwayFromRail, rRailPos, rRailDir);

    directionAwayFromRail.setLength(::hRailPushH);
    MR::addVelocityLimit(this, directionAwayFromRail);
    MR::addVelocityLimit(this, mGravity * -::hRailPushJump);
}

bool BegomanBase::checkTouchElectricRail(bool notCheckPush) {
    Vec3f railPos;
    Vec3f railDir;
    bool knocked = false;

    if (!notCheckPush && ElectricRailFunction::isTouchRail(getSensor("pushed_rail"), &railPos, &railDir)) {
        pushedFromElectricRail(getSensor("pushed_rail"), railPos, railDir, ::hPushedRailPush, ::hPushedRailPush, true);

        // Side escapes turn away from a rail on their side.
        Vec3f sideVec;
        MR::calcSideVec(&sideVec, this);

        if (mInvertSideVec) {
            sideVec.scale(-1.0f);
        }

        if (0.0f < sideVec.dot(railPos - mPosition)) {
            mInvertSideVec = !mInvertSideVec;
        }
    }

    if (ElectricRailFunction::isTouchRail(getSensor("body"), &railPos, &railDir)) {
        knocked = onTouchElectric(railPos, railDir);
    }

    if (mIsTouchElectricRail) {
        mIsTouchElectricRail = false;
        mElectricCounter++;
    } else {
        mElectricCounter = 0;
    }

    return knocked;
}

bool BegomanBase::onTouchElectric(const Vec3f& rRailPos, const Vec3f& rRailDir) {
    mIsTouchElectricRail = true;

    if (mElectricCounter > ::hElectricTouchTime) {
        MR::emitEffectHit(this, rRailPos, "FailureHit");
        addVelocityOnPushedFromElectricRail(rRailPos, rRailDir);
        mElectricCounter = 0;
        return true;
    }

    pushedFromElectricRail(getSensor("body"), rRailPos, rRailDir, ::hBodyRailPush, ::hBodyRailPush, true);
    return false;
}

void BegomanBase::pushedFromElectricRail(HitSensor* pSensor, const Vec3f& rRailPos, const Vec3f& rRailDir, f32 awaySpeed, f32 outSpeed, bool limit) {
    Vec3f directionAwayFromRail;
    calcDirectionAwayFromRail(&directionAwayFromRail, rRailPos, rRailDir);

    // Also out from the contact point, in the ground plane.
    Vec3f fromRail(pSensor->mPosition);
    fromRail.sub(rRailPos);
    MR::vecKillElement(fromRail, mGravity, &fromRail);
    fromRail.setLength(outSpeed);

    if (limit) {
        MR::addVelocityLimit(this, directionAwayFromRail * awaySpeed);
        MR::addVelocityLimit(this, fromRail);
    } else {
        MR::addVelocity(this, directionAwayFromRail * awaySpeed);
        MR::addVelocity(this, fromRail);
    }
}

void BegomanBase::preventSwingby(f32 damp) {
    if (!MR::isOnGround(this)) {
        // Damped by how much gravity turned since the last frame.
        f32 rate = (mGravity - mLastGravity).length();
        rate *= cNum_preventSwingby_f1;
        dampingVerticalAndParallelVelocity(damp, rate);
    }
}

void BegomanBase::preventSlopeBlow(f32 damp) {
    if (MR::isOnGround(this)) {
        // Damped by how far the ground tilts from gravity.
        Vec3f gravityMinusNormal(mGravity);
        gravityMinusNormal.sub(-*MR::getGroundNormal(this));

        f32 rate = gravityMinusNormal.length();
        rate *= cNum_preventSlopeBlow_f1;
        dampingVerticalAndParallelVelocity(damp, rate);
    }
}

void BegomanBase::dampingVerticalAndParallelVelocity(f32 damp, f32 rate) {
    Vec3f verticalVelocity;
    Vec3f parallelVelocity;

    verticalVelocity.scale(mGravity.dot(mVelocity), mGravity);
    MR::vecKillElement(mVelocity, mGravity, &parallelVelocity);

    // Upward speed is damped, falling is not.
    if (verticalVelocity.dot(mGravity) < 0.0f) {
        verticalVelocity.scale(rate * damp + (1 - rate));
    }

    parallelVelocity.scale(rate * damp + (1 - rate));
    mVelocity.set(verticalVelocity + parallelVelocity);
}

bool BegomanBase::isInWaterAndSetWaterNerve(const Nerve* pWaterNerve, Pos3f* pPos) {
    WaterInfo info;

    if (!MR::getWaterAreaObj(&info, mPosition)) {
        return false;
    }

    MR::getWaterAreaInfo(&info, mPosition, mGravity, false);
    setNerve(pWaterNerve);
    MR::makeMtxUpNoSupportPos(pPos, info.mSurfaceNormal, info.mSurfacePos);
    MR::emitEffect(this, "WaterColumn");
    return true;
}

void BegomanBase::calcBlowReaction(const Vec3f& rAttackerPos, const Vec3f& rHitPos, f32 blowStrengthParallel, f32 blowStrengthVertical) {
    Vec3f blowDirection(rHitPos - rAttackerPos);
    Vec3f effectPos(-blowDirection);
    f32 sensorRadius = getSensor("body")->mRadius;

    effectPos.setLength(sensorRadius);
    effectPos.add(getSensor("body")->mPosition);

    MR::emitEffectHit(this, effectPos, "Hit");
    MR::startSound(this, "SE_EM_BEGOMAN_KNOCK_SUCCESS");
    MR::vecKillElement(blowDirection, mGravity, &blowDirection);
    MR::normalizeOrZero(&blowDirection);

    if (MR::isNearZero(blowDirection)) {
        MR::calcFrontVec(&mFaceVec, this);
    } else {
        mFaceVec.set(blowDirection);
    }

    blowDirection.scale(blowStrengthParallel);
    blowDirection.sub(mGravity * blowStrengthVertical);
    mVelocity.set(blowDirection);
}

void BegomanBase::calcAnimCore(Pos3f* pPos) {
    LiveActor::calcAnim();
    pPos->setInline(MR::getJointMtx(this, "Center"));

    Vec3f yDir;
    pPos->getYDir(yDir);

    if (!MR::isSameDirection(yDir, mTargetVec)) {
        MR::makeMtxUpFront(pPos, yDir, mTargetVec);
    }
}

bool BegomanBase::calcJointLocator1(Pos3f* pPos, const JointControllerInfo&) {
    Pos3f spin;
    spin.makeQuat(mSpinQuat);
    pPos->concat(*pPos, spin);
    return true;
}

void BegomanBase::calcAndSetBaseMtx() {
    MR::setBaseTRMtx(this, mPoseQuat);

    if (mScaleControler != nullptr) {
        Vec3f scale;

        scale.set(mScaleControler->mScale);
        mAnimScale.set(scale);
        scale *= mScale;
        MR::setBaseScale(this, scale);
    }

    mBaseDelegator->registerCallBack();
}

bool BegomanBase::requestAttack() {
    return MR::getSceneObj< BegomanAttackPermitter >(SceneObj_BegomanAttackPermitter)->requestAttack(this);
}

BegomanAttackPermitter::BegomanAttackPermitter(const char* pName) : LiveActor(pName), mBegoman(), mDistToPlayer(::hPermitterNoDist), mHasRequest() {
    lumacore::setClass(this, lumacore::kClassId< BegomanAttackPermitter >);
}

void BegomanAttackPermitter::init(const TableIter&) {
    MR::connectToSceneEnemyDecorationMovement(this);
    MR::invalidateClipping(this);
    makeActorAppeared();
    initNerve(GET_NERVE(BegomanAttackPermitter, HostTypeNrvWait));
}

void BegomanAttackPermitter::control() {
    mHasRequest = false;
}

// One begoman attacks at a time: requests gather for hReceiveLimitTime steps, the one nearest the player wins and keeps the
// permit while it keeps asking. The boss attacks whenever no permit is out.
bool BegomanAttackPermitter::requestAttack(BegomanBase* pBegoman) {
    if (MR::isValidSwitchA(pBegoman) && !MR::isOnSwitchA(pBegoman)) {
        return false;
    }

    if (pBegoman->getKind() == BegomanBase::Kind_Boss) {
        return !isNerve(GET_NERVE(BegomanAttackPermitter, HostTypeNrvPermit));
    }

    mHasRequest = true;

    if (isNerve(GET_NERVE(BegomanAttackPermitter, HostTypeNrvReceive)) || isNerve(GET_NERVE(BegomanAttackPermitter, HostTypeNrvWait))) {
        if (mBegoman == nullptr) {
            mBegoman = pBegoman;
            mDistToPlayer = MR::calcDistanceToPlayer(pBegoman->mPosition);

            if (isNerve(GET_NERVE(BegomanAttackPermitter, HostTypeNrvWait))) {
                setNerve(GET_NERVE(BegomanAttackPermitter, HostTypeNrvReceive));
            }
        } else {
            f32 distToPlayer = MR::calcDistanceToPlayer(pBegoman->mPosition);

            if (distToPlayer < mDistToPlayer) {
                mDistToPlayer = distToPlayer;
                mBegoman = pBegoman;
            }
        }
    }

    if (isNerve(GET_NERVE(BegomanAttackPermitter, HostTypeNrvPermit)) && mBegoman == pBegoman) {
        // Re-entering Permit restarts its hPermitLimitTime count.
        setNerve(GET_NERVE(BegomanAttackPermitter, HostTypeNrvPermit));
        return true;
    }

    return false;
}

void BegomanAttackPermitter::exeWait() {
    if (MR::isFirstStep(this)) {
        mBegoman = nullptr;
        mDistToPlayer = ::hPermitterNoDist;
    }
}

void BegomanAttackPermitter::exeReceive() {
    if (!mHasRequest) {
        setNerve(GET_NERVE(BegomanAttackPermitter, HostTypeNrvWait));
    } else if (MR::isGreaterStep(this, ::hReceiveLimitTime)) {
        setNerve(GET_NERVE(BegomanAttackPermitter, HostTypeNrvPermit));
    }
}

void BegomanAttackPermitter::exePermit() {
    if (MR::isGreaterStep(this, ::hPermitLimitTime)) {
        setNerve(GET_NERVE(BegomanAttackPermitter, HostTypeNrvWait));
    }
}
