#include "Game/Enemy/Meramera.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/Enemy/WalkerStateBindStarPointer.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorStateUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ParabolicPath.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_exeRunaway_f1, "exeRunaway.f1", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_addRunawayJumpPower_f1, "addRunawayJumpPower.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_addRunawayJumpPower_f2, "addRunawayJumpPower.f2", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_addRunawayJumpPower_f3, "addRunawayJumpPower.f3", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_addRunawayJumpPower_f4, "addRunawayJumpPower.f4", 0.8f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_exeDamage_reboundVelocityFromEachCollision_1, "exeDamage.reboundVelocityFromEachCollision.1", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDamage_reboundVelocityFromEachCollision_2, "exeDamage.reboundVelocityFromEachCollision.2", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDamage_reboundVelocityFromEachCollision_3, "exeDamage.reboundVelocityFromEachCollision.3", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDamage_reboundVelocityFromEachCollision_4, "exeDamage.reboundVelocityFromEachCollision.4", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRunaway_reboundVelocityFromEachCollision_1, "exeRunaway.reboundVelocityFromEachCollision.1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRunaway_reboundVelocityFromEachCollision_2, "exeRunaway.reboundVelocityFromEachCollision.2", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRunaway_reboundVelocityFromEachCollision_3, "exeRunaway.reboundVelocityFromEachCollision.3", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRunaway_reboundVelocityFromEachCollision_4, "exeRunaway.reboundVelocityFromEachCollision.4", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeStartDiving_setBckRate_1, "exeStartDiving.setBckRate.1", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDiving_setBckRate_1, "exeDiving.setBckRate.1", 1.4f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDown_reboundVelocityFromEachCollision_1, "exeDown.reboundVelocityFromEachCollision.1", 0.0f);

namespace {
    enum { ElementType_Fire, ElementType_Ice };
    enum { EffectType_Null, EffectType_Wait, EffectType_Chase, EffectType_Escape };
    enum { BodyEffectType_Heat = 1, BodyEffectType_CoolDown = 2, BodyEffectType_Cold = 3 };
    // Placement arg0.
    enum { Status_Runaway, Status_Float };

    const char* const sElementArcName[] = {"Meramera", "IceMeramera", "ChildMeramera", "ChildIceMeramera"};
    const s32 sElementList[] = {ElementType_Fire, ElementType_Ice, ElementType_Fire, ElementType_Ice};
    // Indexed by EffectType.
    const char* const sHeadEffectName[] = {nullptr, "Wait", "Chase", "Escape"};
    const Vec3f sAttackHitOffset = Vec3f(50.0f, 0.0f, 0.0f);

    // Hover over the shadow: target height, rise and fall accel, speed limit.
    struct KeepHeightParam {
        f32 mHeight;
        f32 mUpAccel;
        f32 mDownAccel;
        f32 mSpeedMax;
    };

    NEBULA_TUNABLE_RECORD(KeepHeightParam, sWaitHeight, {130.0f, 0.5f, 0.1f, 15.0f});
    NEBULA_TUNABLE_RECORD(KeepHeightParam, sChaseStartHeight, {300.0f, 0.5f, 0.2f, 15.0f});
    NEBULA_TUNABLE_RECORD(KeepHeightParam, sChaseDashLowHeight, {150.0f, 0.5f, 0.1f, 15.0f});
    NEBULA_TUNABLE_RECORD(KeepHeightParam, sChaseDashHeight, {190.0f, 0.5f, 0.2f, 20.0f});
    NEBULA_TUNABLE_RECORD(KeepHeightParam, sChaseTurnHeight, {190.0f, 0.3f, 0.3f, 20.0f});
    NEBULA_TUNABLE_RECORD(KeepHeightParam, sDamageHeight, {140.0f, 0.0f, 1.2f, 15.0f});
    NEBULA_TUNABLE_RECORD(KeepHeightParam, sFloatHeight, {100.0f, 0.9f, 0.1f, 50.0f});
    NEBULA_TUNABLE_RECORD(KeepHeightParam, sIgnitionHeight, {100.0f, 0.5f, 0.1f, 50.0f});
    NEBULA_TUNABLE_RECORD(KeepHeightParam, sAttackSuccessHeight, {50.0f, 0.5f, 0.1f, 15.0f});

    NEBULA_TUNABLE(f32, sBodyHitRadius, 55.0f);
    NEBULA_TUNABLE(f32, sBreakHitRadius, 10.0f);
    NEBULA_TUNABLE(f32, sAttackHitRadius, 70.0f);
    NEBULA_TUNABLE(f32, sPointerRadius, 55.0f);
    NEBULA_TUNABLE(f32, sBindRadius, 45.0f);
    NEBULA_TUNABLE(f32, sShadowRadius, 50.0f);
    NEBULA_TUNABLE(f32, sBaseScale, 150.0f);
    NEBULA_TUNABLE(f32, sRecoveryDistance, 1500.0f);
    NEBULA_TUNABLE(f32, sRecoveryClipRadius, 100.0f);
    NEBULA_TUNABLE(f32, sOverWallAccel, 0.9f);
    NEBULA_TUNABLE(f32, sOverWallProbe, 200.0f);
    NEBULA_TUNABLE(f32, sMaxOverWallHeight, 250.0f);
    const s32 sWaitTime = NebulaFrameClock::stepsOfSeconds(5);
    const s32 sWalkTime = NebulaFrameClock::stepsOfSeconds(2);
    NEBULA_TUNABLE(f32, sWalkRadius, 200.0f);
    NEBULA_TUNABLE(f32, sWalkArriveDistance, 100.0f);
    const s32 sAttackSuccessTime = NebulaFrameClock::stepsOfSeconds(3);
    NEBULA_TUNABLE(s32, sShrinkTime, 20);
    NEBULA_TUNABLE(f32, sChaseDistance, 900.0f);
    NEBULA_TUNABLE(f32, sTerritoryChaseRange, 3000.0f);
    const s32 sChaseEndTime = NebulaFrameClock::stepsOfSeconds(4);
    const s32 sChaseStartTime = NebulaFrameClock::stepsOfSeconds(1);
    NEBULA_TUNABLE(s32, sChaseDashTime, 45);
    NEBULA_TUNABLE(s32, sChaseTurnTime, 50);
    const s32 sChaseDashLowTime = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(f32, sChaseDashMoveAccel, 0.175f);
    NEBULA_TUNABLE(f32, sChaseDashAccel, 0.98f);
    NEBULA_TUNABLE(f32, sChaseTurnAccel, 0.97f);
    NEBULA_TUNABLE(f32, sChaseTurnGravityAccel, 0.99f);
    NEBULA_TUNABLE(f32, sChaseTurnFloatAccel, 0.05f);
    NEBULA_TUNABLE(f32, sWalkMoveAccel, 0.05f);
    NEBULA_TUNABLE(f32, sWalkTurnCos, 0.995f);
    NEBULA_TUNABLE(f32, sChaseStartTurnCos, 0.99f);
    NEBULA_TUNABLE(f32, sDefaultAttenuate, 0.98f);
    NEBULA_TUNABLE(f32, sChaseStartAttenuate, 0.97f);
    NEBULA_TUNABLE(f32, sDamageAttenuate, 0.9f);
    NEBULA_TUNABLE(f32, sFloatAttenuate, 0.94f);
    NEBULA_TUNABLE(f32, sDownAttenuate, 0.99f);
    NEBULA_TUNABLE(s32, sSinkTime, 50);
    const s32 sFloatTime = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(f32, sFloatUpSpeed, 50.0f);
    NEBULA_TUNABLE(s32, sIgnitionStartStep, 15);
    NEBULA_TUNABLE(f32, sIgnitionUpSpeed, 25.0f);
    NEBULA_TUNABLE(f32, sIgnitionForceJump, 15.0f);
    NEBULA_TUNABLE(s32, sIgnitionTime, 20);
    const s32 sRunawayTime = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(s32, sDivingDelayTime, 35);
    NEBULA_TUNABLE(s32, sDivingTime, 40);
    NEBULA_TUNABLE(f32, sDivingHeight, 300.0f);
    const f32 sDivingProbeRate = 1.0f / 0.35f;
    NEBULA_TUNABLE(f32, sDivingFireTurnDegree, 180.0f);
    NEBULA_TUNABLE(s32, sDownStopFrame, 2);
    NEBULA_TUNABLE(f32, sDownGravityAccel, 0.5f);
    // The down bounce has no backward part; the zero term keeps the result bit-exact.
    NEBULA_TUNABLE(f32, sDownBackSpeed, 0.0f);
    NEBULA_TUNABLE(f32, sDownUpSpeed, 25.0f);
    NEBULA_TUNABLE(f32, sDownRebound, 0.9f);
    const s32 sReadyRestartTime = NebulaFrameClock::stepsOfSeconds(3);
    NEBULA_TUNABLE(f32, sSpinNearDistance, 300.0f);
    NEBULA_TUNABLE(f32, sSpinTurnDegree, 5.0f);
    NEBULA_TUNABLE(f32, sExtinguishTurnDegree, 10.0f);
    NEBULA_TUNABLE(f32, sSpinBlowSpeed, 35.0f);
    NEBULA_TUNABLE(f32, sPushSpeed, 0.5f);
    NEBULA_TUNABLE(s32, sDamageExtinguishStep, 3);
    NEBULA_TUNABLE(f32, sFireTurnDegree, 1.0f);
    NEBULA_TUNABLE(s32, sRunawayEscapeEffectStep, 10);
    NEBULA_TUNABLE(f32, sRunawayBoundSpeed, 12.0f);
    NEBULA_TUNABLE(s32, sRunawayJumpStep, 70);
    // On the ground slower than this, the jump timer counts down.
    NEBULA_TUNABLE(f32, sRunawayJumpSpeedH, 3.0f);
    NEBULA_TUNABLE(s32, sRunawayJumpIntervalRandom, 10);
    const s32 sRunawayJumpInterval = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(f32, sRunawayDiveProbe, 250.0f);
    NEBULA_TUNABLE(f32, sRunawayDiveSpread, 80.0f);
    NEBULA_TUNABLE(f32, sRunawayGravity, 2.0f);
    NEBULA_TUNABLE(f32, sRunawayGroundAttenuate, 0.8f);
    NEBULA_TUNABLE(f32, sRunawayAirAttenuate, 0.98f);
    NEBULA_TUNABLE(f32, sJumpDiveNear, 10.0f);
    NEBULA_TUNABLE(f32, sJumpDiveFar, 500.0f);
    NEBULA_TUNABLE(f32, sJumpFleeDistance, 1000.0f);
    NEBULA_TUNABLE(f32, sJumpRandom, 0.5f);
    NEBULA_TUNABLE(f32, sJumpSpeedH, 20.0f);
    NEBULA_TUNABLE(f32, sJumpSpeedVMin, 45.0f);
    NEBULA_TUNABLE(f32, sJumpSpeedVMax, 50.0f);
    NEBULA_TUNABLE(f32, sDiveProbeLift, 100.0f);
    NEBULA_TUNABLE(f32, sDiveProbeDepth, 600.0f);
    NEBULA_TUNABLE(f32, sRollRadius, 50.0f);
    NEBULA_TUNABLE(f32, sPoseBlendRate, 0.25f);
    NEBULA_TUNABLE(f32, sExtinguishPullRate, 0.9f);
    NEBULA_TUNABLE(f32, sExtinguishMinDistance, 100.0f);

    s32 getElementList(const TableIter& rIter) {
        for (s32 idx = 0; idx < ARRAY_SIZE(::sElementArcName); idx++) {
            if (MR::isEqualObjectName(rIter, ::sElementArcName[idx])) {
                return ::sElementList[idx];
            }
        }

        return 0;
    }

    const char* getHeadEffectName(s32 effectType) {
        if (effectType < EffectType_Wait || effectType > EffectType_Escape) {
            return nullptr;
        }

        return ::sHeadEffectName[effectType];
    }

    void keepHeight(LiveActor* pActor, const KeepHeightParam& rParam) {
        MR::addVelocityKeepHeightUseShadow(pActor, rParam.mHeight, rParam.mUpAccel, rParam.mDownAccel, rParam.mSpeedMax, nullptr);
    }
};  // namespace

namespace NrvMeramera {
    NEW_NERVE(MerameraNrvWait, Meramera, Wait);
    NEW_NERVE(MerameraNrvWalk, Meramera, Walk);
    NEW_NERVE(MerameraNrvChaseStart, Meramera, ChaseStart);
    NEW_NERVE(MerameraNrvChaseDash, Meramera, ChaseDash);
    NEW_NERVE(MerameraNrvChaseTurn, Meramera, ChaseTurn);
    NEW_NERVE(MerameraNrvDamage, Meramera, Damage);
    NEW_NERVE(MerameraNrvShrink, Meramera, Shrink);
    NEW_NERVE_ONEND(MerameraNrvRunaway, Meramera, Runaway, Runaway);
    NEW_NERVE_ONEND(MerameraNrvStartDiving, Meramera, StartDiving, Runaway);
    NEW_NERVE(MerameraNrvDiving, Meramera, Diving);
    NEW_NERVE(MerameraNrvSink, Meramera, Sink);
    NEW_NERVE(MerameraNrvFloat, Meramera, Float);
    NEW_NERVE(MerameraNrvIgnition, Meramera, Ignition);
    NEW_NERVE(MerameraNrvIgnitionForce, Meramera, IgnitionForce);
    NEW_NERVE(MerameraNrvAttackSuccess, Meramera, AttackSuccess);
    NEW_NERVE_ONEND(MerameraNrvBindStarPointer, Meramera, BindStarPointer, BindStarPointer);
    NEW_NERVE_ONEND(MerameraNrvBindStarPointerNoFire, Meramera, BindStarPointerNoFire, BindStarPointer);
    NEW_NERVE(MerameraNrvDown, Meramera, Down);
    NEW_NERVE(MerameraNrvFlatDown, Meramera, FlatDown);
    NEW_NERVE(MerameraNrvReadyRestart, Meramera, ReadyRestart);
};  // namespace NrvMeramera

inline bool Meramera::isSensor(const HitSensor* pSensor, const char* pName) const {
    return getSensor(pName) == pSensor;
}

inline bool Meramera::isActionDisabled() const {
    return isNerve(GET_NERVE(Meramera, MerameraNrvRunaway)) || isNerve(GET_NERVE(Meramera, MerameraNrvStartDiving)) ||
           isNerve(GET_NERVE(Meramera, MerameraNrvDiving)) || isNerve(GET_NERVE(Meramera, MerameraNrvBindStarPointerNoFire));
}

inline Vec3f Meramera::calcVecToPlayer() const {
    Vec3f toPlayer(*MR::getPlayerPos());
    toPlayer -= mPosition;
    return toPlayer;
}

Meramera::Meramera(const char* pName)
    : LiveActor(pName), mAnimScaleController(), mWalkerStateBindStarPointer(), mPoseQuat(0.0f, 1.0f), mFront(0, 0, 1), mFireUp(0, 1, 0), mHomePosition(0, 0, 0),
      mDashDir(0, 0, 1), mWalkTarget(0, 0, 0), mDivePos(0, 0, 0), mDiveDir(0, 0, 1), mDiveUp(0, 1, 0), mTargetDistance(-1.0f), mChaseDistance(::sChaseDistance),
      mAppearStatus(-1), mElementType(), mEffectType(), mBodyEffectType(), mRunawayTimer(), mCanDive(), mIsValidRestart() {
    mEffectMtx.identity();
    mExtinguishMtx.identity();
    mFallEffectMtx.identity();

    mParabolicPath = new ParabolicPath();
}

void Meramera::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initElementType(rIter);

    initModelManagerWithAnm(::sElementArcName[mElementType], nullptr, false);

    MR::connectToSceneEnemy(this);

    initSound(4, false);
    initSensor();
    initBind();
    initEffect();
    initShadow();

    MR::calcGravity(this);

    mHomePosition = mPosition;

    initNerve(GET_NERVE(Meramera, MerameraNrvWait));

    MR::useStageSwitchWriteDead(this, rIter);

    mAnimScaleController = new AnimScaleController(nullptr);
    mAnimScaleController->setParamTight();

    mWalkerStateBindStarPointer = new WalkerStateBindStarPointer(this, mAnimScaleController);

    MR::declareCoin(this, 1);

    initAppearState(rIter);
}

void Meramera::initElementType(const TableIter& rIter) {
    mElementType = ::getElementList(rIter);
}

void Meramera::initEffect() {
    initEffectKeeper(1, nullptr);

    MR::setEffectHostMtx(this, "ExtinguishSide", mExtinguishMtx);
    MR::setEffectHostMtx(this, "ExtinguishSideLight", mExtinguishMtx);

    MR::setEffectHostMtx(this, "Wait", mEffectMtx);
    MR::setEffectHostMtx(this, "Escape", mEffectMtx);
    MR::setEffectHostMtx(this, "ExtinguishSmoke", mEffectMtx);
    MR::setEffectHostMtx(this, "ChaseStart", mEffectMtx);
    MR::setEffectHostMtx(this, "Chase", mEffectMtx);
    MR::setEffectHostMtx(this, "Attack", mEffectMtx);

    MR::setEffectHostMtx(this, "Fall", mFallEffectMtx);
}

void Meramera::initSensor() {
    f32 scale = mScale.x;

    LiveActor::initHitSensor(3);

    MR::addHitSensorEnemy(this, "body", 8, scale * ::sBodyHitRadius, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorEnemy(this, "break", 8, scale * ::sBreakHitRadius, Vec3f(0.0f, 0.0f, 0.0f));

    MR::addHitSensorAtJointEnemy(this, "attack", "JointRoot", 8, scale * ::sAttackHitRadius, ::sAttackHitOffset * scale);

    MR::invalidateHitSensor(this, "break");

    MR::initStarPointerTarget(this, scale * ::sPointerRadius, Vec3f(0.0f, 0.0f, 0.0f));
}

void Meramera::initBind() {
    f32 scale = mScale.x;
    initBinder(scale * ::sBindRadius, 0.0f, 0);

    MR::onCalcGravity(this);
}

void Meramera::initShadow() {
    f32 scale = mScale.x;

    MR::initShadowController(this, 2);

    MR::addShadowSurfaceCircle(this, "水面用", scale * ::sShadowRadius);
    MR::setShadowDropStartOffset(this, "水面用", ::sBaseScale);
    MR::addShadowVolumeSphere(this, "地面用", scale * ::sShadowRadius);
}

void Meramera::initAppearState(const TableIter& rIter) {
    MR::getPlacementArg0WithInit(rIter, &mAppearStatus);

    resetAppear();

    MR::getPlacementArg2NoInit(rIter, &mIsValidRestart);

    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::syncStageSwitchAppear(this);
        makeActorDead();
    } else {
        makeActorAppeared();
    }
}

void Meramera::kill() {
    MR::emitEffect(this, "Break");

    MR::appearCoinPop(this, mPosition, 1);

    if (MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
    }

    if (!mIsValidRestart) {
        LiveActor::kill();
        return;
    }

    MR::invalidateClipping(this);
    MR::hideModel(this);
    MR::deleteEffectAll(this);

    setNerve(GET_NERVE(Meramera, MerameraNrvReadyRestart));
}

void Meramera::startClipped() {
    LiveActor::startClipped();

    MR::deleteEffectAll(this);

    mEffectType = EffectType_Null;
}

void Meramera::endClipped() {
    LiveActor::endClipped();

    setNerve(GET_NERVE(Meramera, MerameraNrvWait));
}

void Meramera::control() {
    mAnimScaleController->updateNerve();
    updatePose();

    MR::turnVecToVecDegree(&mFireUp, mFireUp, -mGravity, ::sFireTurnDegree, mFront);

    MR::makeMtxUpNoSupportPos(&mEffectMtx, mFireUp, MR::getJointPos(this, "Sin"));

    tryPointBind();
}

void Meramera::calcAndSetBaseMtx() {
    MR::setBaseTRMtx(this, mPoseQuat);
    MR::setBaseScale(this, mAnimScaleController->mScale * mScale);
}

void Meramera::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (isSensor(pSender, "body") && (MR::isSensorEnemy(pReceiver) || MR::isSensorPlayer(pReceiver))) {
        MR::sendMsgPush(pReceiver, pSender);
    }

    requestAttack(pSender, pReceiver);
}

bool Meramera::receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver) {
    if (!isSensor(pReceiver, "body")) {
        return false;
    }

    if (MR::isSensorPlayer(pSender) && requestDown(pReceiver, pSender)) {
        MR::sendMsgKick(pSender, pReceiver);
        return true;
    }

    if (!MR::isSensorEnemy(pSender) && !MR::isSensorPlayer(pSender)) {
        return false;
    }

    if (isPushMoved()) {
        Vec3f dir;
        MR::calcSensorDirectionNormalize(&dir, pSender, pReceiver);
        MR::vecScaleAdd(&mVelocity, &dir, ::sPushSpeed);
    }

    return true;
}

bool Meramera::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgLockOnStarPieceShoot(msg)) {
        return isAffectedSpin();
    }

    if (MR::isMsgStarPieceAttack(msg)) {
        return requestForceRunaway();
    }

    if (MR::isMsgInvincibleAttack(msg)) {
        return requestDownForce(pReceiver, pSender);
    }

    if (MR::isMsgFireBallAttack(msg)) {
        return requestFire(pReceiver, pSender);
    }

    if (MR::isMsgPlayerTrample(msg) || MR::isMsgPlayerHipDrop(msg)) {
        return requestFlatDown(pReceiver, pSender);
    }

    if (MR::isMsgPlayerHitAll(msg)) {
        return requestDown(pReceiver, pSender);
    }

    return false;
}

bool Meramera::receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    return MR::isMsgExplosionAttack(msg) && requestDownForce(pReceiver, pSender);
}

bool Meramera::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgSpinStormRange(msg)) {
        return requestDamage(pReceiver, pSender);
    }

    if (MR::isMsgStartPowerStarGet(msg)) {
        makeActorDead();
        return true;
    }

    return false;
}

bool Meramera::requestDamage(HitSensor* pSender, HitSensor* pReceiver) {
    if (!isSensor(pSender, "body") || !MR::isSensorPlayer(pReceiver) || !isAffectedSpin()) {
        return false;
    }

    // A spin out of reach only leans the flame toward the player.
    if (!MR::isNear(pSender, pReceiver, ::sSpinNearDistance)) {
        emitEffectHead(EffectType_Wait);
        turnFireDirectionToSpin(::sSpinTurnDegree);
        return false;
    }

    if (!MR::isPlayerSwingAction()) {
        return false;
    }

    MR::setVelocitySeparateHV(this, pReceiver, pSender, ::sSpinBlowSpeed, 0.0f);
    setNerve(GET_NERVE(Meramera, MerameraNrvDamage));
    return true;
}

bool Meramera::requestFire(HitSensor* pSender, HitSensor* pReceiver) {
    switch (mElementType) {
    case ElementType_Fire:
        if (isEnableFireball()) {
            setNerve(GET_NERVE(Meramera, MerameraNrvIgnitionForce));
            return true;
        }

        return false;

    case ElementType_Ice:
        return requestForceRunaway() || requestDownForce(pSender, pReceiver);
    }

    return false;
}

bool Meramera::requestForceRunaway() {
    if (!isAffectedSpin()) {
        return false;
    }

    MR::startBck(this, "Damage");
    setNerve(GET_NERVE(Meramera, MerameraNrvRunaway));
    return true;
}

bool Meramera::requestDownForce(HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(Meramera, MerameraNrvDown))) {
        return false;
    }

    MR::emitEffectHitBetweenSensors(this, pSender, pReceiver, 0.0f, "HitMark");

    Vec3f horizon;
    MR::calcSensorHorizonNormalize(&horizon, mGravity, pSender, pReceiver);

    if (!MR::isNearZero(horizon)) {
        mFront.set(horizon);
    }

    MR::zeroVelocity(this);
    setNerve(GET_NERVE(Meramera, MerameraNrvDown));
    return true;
}

bool Meramera::requestDown(HitSensor* pSender, HitSensor* pReceiver) {
    return MR::isSensorPlayer(pReceiver) && isActionDisabled() && requestDownForce(pSender, pReceiver);
}

bool Meramera::requestAttack(HitSensor* pSender, HitSensor* pReceiver) {
    if (!isSensor(pSender, "attack") || !isEnableAttack()) {
        return false;
    }

    if (!MR::isSensorPlayer(pReceiver)) {
        sendMsgElementAttack(pReceiver, pSender);
        return false;
    }

    if (!sendMsgElementAttack(pReceiver, pSender)) {
        return false;
    }

    setNerve(GET_NERVE(Meramera, MerameraNrvAttackSuccess));
    return true;
}

bool Meramera::requestFlatDown(HitSensor* pSender, HitSensor* pReceiver) {
    if (!isSensor(pSender, "body") || !isActionDisabled()) {
        return false;
    }

    MR::emitEffectHitBetweenSensors(this, pSender, pReceiver, 0.0f, "HitMark");
    setNerve(GET_NERVE(Meramera, MerameraNrvFlatDown));
    return true;
}

bool Meramera::sendMsgElementAttack(HitSensor* pSender, HitSensor* pReceiver) {
    switch (mElementType) {
    case ElementType_Fire:
        return MR::sendMsgEnemyAttackFire(pSender, pReceiver);

    case ElementType_Ice:
        return MR::sendMsgEnemyAttackFreeze(pSender, pReceiver);

    default:
        return false;
    }
}

bool Meramera::tryWalk() {
    if (getNerveStep() <= ::sWalkTime) {
        return false;
    }

    // A random point on the ground plane around home.
    Vec3f randomVec;
    MR::getRandomVector(&randomVec, 1.0f);
    MR::normalizeOrZero(&randomVec);
    randomVec.killElement(randomVec, mGravity);
    randomVec *= ::sWalkRadius;
    mWalkTarget = randomVec + mHomePosition;

    setNerve(GET_NERVE(Meramera, MerameraNrvWalk));
    return true;
}

bool Meramera::tryWalkEnd() {
    if (getNerveStep() > ::sWaitTime || (0.0f <= mTargetDistance && mTargetDistance <= ::sWalkArriveDistance)) {
        setNerve(GET_NERVE(Meramera, MerameraNrvWait));
        return true;
    }

    return false;
}

bool Meramera::tryEndShrink() {
    if (!MR::isGreaterStep(this, ::sShrinkTime)) {
        return false;
    }

    if (isEnableChase()) {
        setNerve(GET_NERVE(Meramera, MerameraNrvChaseStart));
    } else {
        setNerve(GET_NERVE(Meramera, MerameraNrvWait));
    }

    return true;
}

bool Meramera::tryChase() {
    if (isEnableChase()) {
        setNerve(GET_NERVE(Meramera, MerameraNrvChaseStart));
        return true;
    }

    return false;
}

bool Meramera::tryChaseStartToDash() {
    if (MR::isGreaterStep(this, ::sChaseStartTime)) {
        setNerve(GET_NERVE(Meramera, MerameraNrvChaseDash));
        return true;
    }

    return false;
}

bool Meramera::tryChaseDashToTurn() {
    if (MR::isGreaterStep(this, ::sChaseDashTime)) {
        setNerve(GET_NERVE(Meramera, MerameraNrvChaseTurn));
        return true;
    }

    return false;
}

bool Meramera::tryChaseTurnToDash() {
    if (MR::isGreaterStep(this, ::sChaseTurnTime)) {
        setNerve(GET_NERVE(Meramera, MerameraNrvChaseDash));
        return true;
    }

    return false;
}

bool Meramera::tryEndChase() {
    if (!MR::isNearPlayer(this, mChaseDistance) || MR::isGreaterStep(this, ::sChaseEndTime) || mHomePosition.distance(mPosition) > ::sTerritoryChaseRange) {
        setNerve(GET_NERVE(Meramera, MerameraNrvWait));
        return true;
    }

    return false;
}

bool Meramera::tryRunaway() {
    // getRandom(0, 0) is 0 but still draws from the random sequence.
    if (MR::isGreaterStep(this, MR::getRandom(0, 0) + ::sRunawayTime)) {
        setNerve(GET_NERVE(Meramera, MerameraNrvRunaway));
        return true;
    }

    return false;
}

bool Meramera::tryStartDiving() {
    if (mCanDive && MR::isBindedGround(this)) {
        MR::zeroVelocity(this);
        setNerve(GET_NERVE(Meramera, MerameraNrvStartDiving));
        return true;
    }

    return false;
}

bool Meramera::tryDiving() {
    if (MR::isGreaterStep(this, ::sDivingDelayTime)) {
        setNerve(GET_NERVE(Meramera, MerameraNrvDiving));
        return true;
    }

    return false;
}

bool Meramera::tryEndDiving() {
    if (MR::isGreaterStep(this, ::sDivingTime)) {
        setNerve(GET_NERVE(Meramera, MerameraNrvRunaway));
        return true;
    }

    return false;
}

bool Meramera::tryFloat() {
    if (MR::isGreaterStep(this, ::sSinkTime)) {
        setNerve(GET_NERVE(Meramera, MerameraNrvFloat));
        return true;
    }

    return false;
}

bool Meramera::tryEndFloat() {
    if (MR::isGreaterStep(this, ::sFloatTime)) {
        setNerve(GET_NERVE(Meramera, MerameraNrvIgnition));
        return true;
    }

    return false;
}

bool Meramera::tryEndIgnition() {
    if (!MR::isGreaterStep(this, ::sIgnitionTime)) {
        return false;
    }

    if (isEnableChase()) {
        setNerve(GET_NERVE(Meramera, MerameraNrvChaseDash));
    } else {
        setNerve(GET_NERVE(Meramera, MerameraNrvWait));
    }

    return true;
}

bool Meramera::tryForceSink() {
    if (!isHitRecover()) {
        return false;
    }

    mDivePos.set(mPosition);
    mDiveUp.set(-mGravity);

    MR::zeroVelocity(this);
    setNerve(GET_NERVE(Meramera, MerameraNrvSink));
    return true;
}

bool Meramera::tryRecovery() {
    // Out of the player's reach and out of view both here and at home: start over at home.
    f32 scale = mScale.x;

    if (!MR::isNearPlayer(this, ::sRecoveryDistance) && !MR::isNearPlayer(mHomePosition, ::sRecoveryDistance) &&
        MR::isJudgedToClipFrustum(mPosition, scale * ::sRecoveryClipRadius) && MR::isJudgedToClipFrustum(mHomePosition, scale * ::sRecoveryClipRadius)) {
        resetAppear();
        return true;
    }

    return false;
}

bool Meramera::tryEndAttackSuccess() {
    if (MR::isGreaterStep(this, ::sAttackSuccessTime)) {
        setNerve(GET_NERVE(Meramera, MerameraNrvWait));
        return true;
    }

    return false;
}

bool Meramera::tryPointBind() {
    if ((isNerve(GET_NERVE(Meramera, MerameraNrvWait)) || isNerve(GET_NERVE(Meramera, MerameraNrvWalk)) ||
         isNerve(GET_NERVE(Meramera, MerameraNrvChaseStart)) || isNerve(GET_NERVE(Meramera, MerameraNrvChaseDash)) ||
         isNerve(GET_NERVE(Meramera, MerameraNrvChaseTurn)) || isNerve(GET_NERVE(Meramera, MerameraNrvIgnition)) ||
         isNerve(GET_NERVE(Meramera, MerameraNrvIgnitionForce)) || isNerve(GET_NERVE(Meramera, MerameraNrvAttackSuccess))) &&
        mWalkerStateBindStarPointer->tryStartPointBind()) {
        setNerve(GET_NERVE(Meramera, MerameraNrvBindStarPointer));
        return true;
    }

    // Running away or diving, the flame is out: the bind ends back in Runaway.
    if ((isNerve(GET_NERVE(Meramera, MerameraNrvRunaway)) || isNerve(GET_NERVE(Meramera, MerameraNrvStartDiving))) &&
        mWalkerStateBindStarPointer->tryStartPointBind()) {
        setNerve(GET_NERVE(Meramera, MerameraNrvBindStarPointerNoFire));
        return true;
    }

    return false;
}

void Meramera::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::validateClipping(this);
        MR::startBck(this, "Wait");
        emitEffectHead(EffectType_Wait);
        emitEffectHeatBody();
    }

    startWaitLevelSound();

    ::keepHeight(this, ::sWaitHeight);
    MR::attenuateVelocity(this, ::sDefaultAttenuate);
    MR::reboundVelocityFromCollision(this);

    if (!tryChase()) {
        tryWalk();
    }
}

void Meramera::exeWalk() {
    if (MR::isFirstStep(this)) {
        MR::validateClipping(this);
        emitEffectHead(EffectType_Wait);
        emitEffectHeatBody();
    }

    startWaitLevelSound();

    ::keepHeight(this, ::sWaitHeight);
    addToTargetMovingAccel(mWalkTarget, ::sWalkMoveAccel, ::sWalkTurnCos);
    MR::attenuateVelocity(this, ::sDefaultAttenuate);
    MR::reboundVelocityFromCollision(this);

    if (!tryChase()) {
        tryWalkEnd();
    }
}

void Meramera::exeShrink() {
    if (MR::isFirstStep(this)) {
        MR::invalidateClipping(this);
        MR::startBck(this, "Wait");
        emitEffectHead(EffectType_Wait);
        emitEffectHeatBody();
    }

    turnFireDirectionToSpin(::sSpinTurnDegree);

    ::keepHeight(this, ::sWaitHeight);
    MR::attenuateVelocity(this, ::sDefaultAttenuate);
    MR::reboundVelocityFromCollision(this);

    tryEndShrink();
}

void Meramera::exeChaseStart() {
    if (MR::isFirstStep(this)) {
        MR::validateClipping(this);
        MR::startBck(this, "ChaseStart");
        MR::startSound(this, "SE_EM_MERAMERA_FIND");
        MR::startSound(this, mElementType == ElementType_Ice ? "SE_EM_ICEMERA_POWER_UP" : "SE_EM_MERAMERA_FIREPOWER_UP");
        deleteEffectHead(false);
        emitEffectHeatBody();
    }

    startWaitLevelSound();

    ::keepHeight(this, ::sChaseStartHeight);
    addToTargetMovingAccel(*MR::getPlayerPos(), ::sWalkMoveAccel, ::sChaseStartTurnCos);
    MR::attenuateVelocity(this, ::sChaseStartAttenuate);
    MR::reboundVelocityFromCollision(this);

    if (!tryEndChase()) {
        tryChaseStartToDash();
    }
}

void Meramera::exeChaseDash() {
    if (MR::isFirstStep(this)) {
        MR::invalidateClipping(this);
        MR::startBck(this, "ChaseDash");
        MR::startSound(this, "SE_EM_MERAMERA_DASH");

        // The dash keeps the direction to where the player was when it started.
        mPlayerPosition = *MR::getPlayerPos();
        mDashDir = mPlayerPosition - mPosition;

        emitEffectHead(EffectType_Chase);
        emitEffectHeatBody();
    }

    startChaseLevelSound();

    ::keepHeight(this, MR::isLessStep(this, ::sChaseDashLowTime) ? ::sChaseDashLowHeight : ::sChaseDashHeight);

    addMovingAccel(mDashDir, ::sChaseDashMoveAccel, ::sChaseDashAccel);
    addOverWallAccel(mDashDir);
    MR::attenuateVelocity(this, ::sChaseDashAccel);
    MR::reboundVelocityFromCollision(this);

    tryChaseDashToTurn();
}

void Meramera::exeChaseTurn() {
    if (MR::isFirstStep(this)) {
        MR::validateClipping(this);
        MR::startBck(this, "ChaseTurn");
        emitEffectHead(EffectType_Chase);
        emitEffectHeatBody();
    }

    startChaseLevelSound();

    ::keepHeight(this, ::sChaseTurnHeight);
    addToTargetMovingAccel(*MR::getPlayerPos(), ::sChaseTurnFloatAccel, ::sChaseTurnGravityAccel);
    MR::attenuateVelocity(this, ::sChaseTurnAccel);
    MR::reboundVelocityFromCollision(this);

    if (!tryEndChase()) {
        tryChaseTurnToDash();
    }
}

void Meramera::exeDamage() {
    if (MR::isFirstStep(this)) {
        MR::invalidateClipping(this);
        MR::startBck(this, "Damage");
        MR::turnVecToVecCosOnPlane(&mFront, calcVecToPlayer(), mGravity, -1.0f);
        MR::startSound(this, mElementType == ElementType_Ice ? "SE_EM_ICEMERA_BLOW_COLD" : "SE_EM_MERAMERA_BLOW_FIRE");
        emitEffectHead(EffectType_Chase);
        emitEffectHeatBody();
    }

    if (MR::isStep(this, ::sDamageExtinguishStep)) {
        mExtinguishMtx.makeTrans(MR::getJointPos(this, "Sin"));

        deleteEffectHead(true);
        emitEffectCoolDownBody();
        MR::emitEffect(this, "ExtinguishSide");
        MR::emitEffect(this, "ExtinguishSmoke");

        MR::startSound(this, "SE_EM_MERAMERA_SMOKE");
    }

    if (MR::isGreaterEqualStep(this, ::sDamageExtinguishStep)) {
        updateExtinguishMtx();
    }

    ::keepHeight(this, ::sDamageHeight);
    MR::attenuateVelocity(this, ::sDamageAttenuate);
    MR::reboundVelocityFromEachCollision(this, cNum_exeDamage_reboundVelocityFromEachCollision_1, cNum_exeDamage_reboundVelocityFromEachCollision_2, cNum_exeDamage_reboundVelocityFromEachCollision_3, cNum_exeDamage_reboundVelocityFromEachCollision_4);

    tryRunaway();
}

void Meramera::exeRunaway() {
    if (MR::isFirstStep(this)) {
        MR::invalidateClipping(this);
        MR::validateHitSensor(this, "break");

        mCanDive = false;

        deleteEffectHead(false);
        emitEffectColdBody();

        mRunawayTimer = 0;
    }

    if (tryForceSink()) {
        return;
    }

    f32 fallSpeed = mGravity.dot(mVelocity);

    if (MR::isStep(this, ::sRunawayEscapeEffectStep)) {
        emitEffectHead(EffectType_Escape);
    }

    if (MR::isBindedGround(this) && fallSpeed > ::sRunawayBoundSpeed) {
        MR::emitEffect(this, "EscapeRock");
        MR::startSound(this, "SE_EM_MERAMERA_BOUND");
    }

    if (tryStartDiving()) {
        return;
    }

    if (MR::isGreaterStep(this, ::sRunawayJumpStep)) {
        if (MR::isBindedGround(this)) {
            Vec3f velocityH;
            velocityH.scaleAdd(-mGravity.dot(mVelocity), mGravity, mVelocity);

            if (velocityH.length() < ::sRunawayJumpSpeedH) {
                mRunawayTimer--;
            }

            if (mRunawayTimer <= 0) {
                addRunawayJumpPower();
                MR::emitEffect(this, "EscapeRock");
                MR::startSound(this, "SE_EM_MERAMERA_JUMP");
                mRunawayTimer = MR::getRandom(0, ::sRunawayJumpIntervalRandom) + ::sRunawayJumpInterval;
            }
        }

        // Probe for a dive point ahead, sweeping alternately left and right.
        if (!mCanDive) {
            f32 angleDegree = (getNerveStep() % 5 * ::sRunawayDiveSpread) / cNum_exeRunaway_f1;

            if (getNerveStep() % 2 == 1) {
                angleDegree = -angleDegree;
            }

            checkDivingPoint(::sRunawayDiveProbe, angleDegree);
        }
    }

    if (!MR::isBindedGround(this)) {
        MR::addVelocityToGravity(this, ::sRunawayGravity);
    }

    MR::attenuateVelocity(this, MR::isBindedGround(this) ? ::sRunawayGroundAttenuate : ::sRunawayAirAttenuate);
    MR::reboundVelocityFromEachCollision(this, cNum_exeRunaway_reboundVelocityFromEachCollision_1, cNum_exeRunaway_reboundVelocityFromEachCollision_2, cNum_exeRunaway_reboundVelocityFromEachCollision_3, cNum_exeRunaway_reboundVelocityFromEachCollision_4);

    tryRecovery();
}

void Meramera::exeStartDiving() {
    if (MR::isFirstStep(this)) {
        MR::invalidateClipping(this);
        MR::validateHitSensor(this, "break");

        deleteEffectHead(false);
        emitEffectColdBody();

        MR::setBckRate(this, cNum_exeStartDiving_setBckRate_1);
    }

    if (MR::isLessStep(this, ::sDivingDelayTime)) {
        checkDivingPointMore(::sDivingProbeRate);
    }

    tryDiving();
}

void Meramera::exeDiving() {
    if (MR::isFirstStep(this)) {
        mParabolicPath->initFromUpVector(mPosition, mDivePos, mDiveUp, ::sDivingHeight);

        deleteEffectHead(false);
        emitEffectColdBody();

        MR::turnVecToVecCosOnPlane(&mFront, mDivePos - mPosition, mGravity, -1.0f);

        MR::invalidateClipping(this);

        MR::setBckRate(this, cNum_exeDiving_setBckRate_1);
        MR::emitEffect(this, "EscapeRock");
        MR::startSound(this, "SE_EM_MERAMERA_JUMP");
    }

    Vec3f pos;
    mParabolicPath->calcPosition(&pos, getNerveStep() / static_cast< f32 >(::sDivingTime));
    mVelocity.set(pos - mPosition);

    MR::turnVecToVecDegree(&mFireUp, mFireUp, -mGravity, ::sDivingFireTurnDegree, mFront);

    if (!tryForceSink()) {
        tryEndDiving();
    }
}

void Meramera::exeSink() {
    if (MR::isFirstStep(this)) {
        MR::invalidateClipping(this);

        MR::emitEffect(this, "Fall");

        if (mElementType == ElementType_Ice) {
            MR::emitEffect(this, "Revival");
        }

        MR::startSound(this, "SE_EM_MERAMERA_LAVA_IN");
        MR::startBck(this, "Wait");

        MR::hideModel(this);
        MR::offBind(this);

        MR::makeMtxUpNoSupportPos(&mFallEffectMtx, -mGravity, mPosition);

        deleteEffectHead(false);
        emitEffectColdBody();
    }

    MR::turnVecToVecCosOnPlane(&mFront, calcVecToPlayer(), mGravity, -1.0f);

    if (tryFloat() && mElementType == ElementType_Ice) {
        MR::deleteEffect(this, "Revival");
    }
}

void Meramera::exeFloat() {
    if (MR::isFirstStep(this)) {
        MR::invalidateClipping(this);
        MR::showModel(this);
        MR::onBind(this);

        MR::startBck(this, "Float");
        MR::emitEffect(this, "Fall");
        MR::startSound(this, "SE_EM_MERAMERA_LAVA_OUT");

        deleteEffectHead(false);
        emitEffectHeatBody();

        MR::setVelocity(this, mDiveUp * ::sFloatUpSpeed);
    }

    ::keepHeight(this, ::sFloatHeight);
    MR::attenuateVelocity(this, ::sFloatAttenuate);
    MR::reboundVelocityFromCollision(this);

    MR::turnVecToVecCosOnPlane(&mFront, calcVecToPlayer(), mGravity, -1.0f);

    tryEndFloat();
}

void Meramera::exeIgnition() {
    if (MR::isStep(this, ::sIgnitionStartStep)) {
        MR::invalidateClipping(this);
        MR::startBck(this, "FloatIgnition");
        MR::startSound(this, mElementType == ElementType_Ice ? "SE_EM_ICEMERA_IGNITION" : "SE_EM_MERAMERA_IGNITION");
        MR::addVelocity(this, mDiveUp * ::sIgnitionUpSpeed);
    }

    ::keepHeight(this, ::sIgnitionHeight);
    MR::attenuateVelocity(this, ::sDefaultAttenuate);
    MR::reboundVelocityFromCollision(this);

    tryEndIgnition();
}

void Meramera::exeIgnitionForce() {
    if (MR::isFirstStep(this)) {
        MR::invalidateClipping(this);
        MR::startBck(this, "FloatIgnition");
        MR::setVelocityJump(this, ::sIgnitionForceJump);
        emitEffectHead(EffectType_Wait);
        emitEffectHeatBody();
    }

    ::keepHeight(this, ::sWaitHeight);
    MR::attenuateVelocity(this, ::sDefaultAttenuate);
    MR::reboundVelocityFromCollision(this);

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(Meramera, MerameraNrvWait));
    }
}

void Meramera::exeAttackSuccess() {
    if (MR::isFirstStep(this)) {
        MR::invalidateClipping(this);
        MR::startBck(this, "Attack");
        MR::zeroVelocity(this);
        deleteEffectHead(false);
        emitEffectHeatBody();
    }

    ::keepHeight(this, ::sAttackSuccessHeight);
    MR::attenuateVelocity(this, ::sDefaultAttenuate);
    MR::reboundVelocityFromCollision(this);

    tryEndAttackSuccess();
}

void Meramera::exeDown() {
    if (MR::isFirstStep(this)) {
        MR::invalidateClipping(this);
        MR::startBck(this, "Down");
        MR::stopScene(::sDownStopFrame);
        MR::startSound(this, "SE_EM_MERAMERA_STOMPED");
        deleteEffectHead(false);
        emitEffectColdBody();
        MR::zeroVelocity(this);

        mVelocity = -mFront * ::sDownBackSpeed - mGravity * ::sDownUpSpeed;
    }

    if (!MR::isBindedGround(this)) {
        MR::addVelocityToGravity(this, ::sDownGravityAccel);
    }

    MR::attenuateVelocity(this, ::sDownAttenuate);
    MR::reboundVelocityFromEachCollision(this, ::sDownRebound, ::sDownRebound, ::sDownRebound, cNum_exeDown_reboundVelocityFromEachCollision_1);

    if (MR::isGreaterStep(this, 0)) {
        MR::startSound(this, "SE_EM_MERAMERA_EXPLODE");
        kill();
    }
}

void Meramera::exeFlatDown() {
    if (MR::isFirstStep(this)) {
        MR::invalidateClipping(this);
        MR::startBck(this, "FlatDown");
        MR::stopScene(::sDownStopFrame);
        MR::startSound(this, "SE_EM_MERAMERA_STOMPED");
        MR::zeroVelocity(this);
        deleteEffectHead(false);
        emitEffectColdBody();
    }

    if (MR::isGreaterStep(this, 0)) {
        MR::startSound(this, "SE_EM_EXPLODE_S");
        kill();
    }
}

bool Meramera::isAffectedSpin() const {
    return isNerve(GET_NERVE(Meramera, MerameraNrvWait)) || isNerve(GET_NERVE(Meramera, MerameraNrvAttackSuccess)) ||
           isNerve(GET_NERVE(Meramera, MerameraNrvWalk)) || isNerve(GET_NERVE(Meramera, MerameraNrvShrink)) ||
           isNerve(GET_NERVE(Meramera, MerameraNrvChaseStart)) || isNerve(GET_NERVE(Meramera, MerameraNrvChaseDash)) ||
           isNerve(GET_NERVE(Meramera, MerameraNrvChaseTurn)) || isNerve(GET_NERVE(Meramera, MerameraNrvIgnition)) ||
           isNerve(GET_NERVE(Meramera, MerameraNrvIgnitionForce)) || isNerve(GET_NERVE(Meramera, MerameraNrvBindStarPointer));
}

bool Meramera::isEnableAttack() const {
    return isNerve(GET_NERVE(Meramera, MerameraNrvWait)) || isNerve(GET_NERVE(Meramera, MerameraNrvWalk)) ||
           isNerve(GET_NERVE(Meramera, MerameraNrvChaseDash)) || isNerve(GET_NERVE(Meramera, MerameraNrvChaseTurn)) ||
           isNerve(GET_NERVE(Meramera, MerameraNrvBindStarPointer));
}

bool Meramera::isPushMoved() const {
    return isNerve(GET_NERVE(Meramera, MerameraNrvWait)) || isNerve(GET_NERVE(Meramera, MerameraNrvWalk)) ||
           isNerve(GET_NERVE(Meramera, MerameraNrvDamage)) || isNerve(GET_NERVE(Meramera, MerameraNrvChaseDash)) ||
           isNerve(GET_NERVE(Meramera, MerameraNrvChaseTurn)) || isNerve(GET_NERVE(Meramera, MerameraNrvAttackSuccess));
}

bool Meramera::isEnableFireball() const {
    return !isNerve(GET_NERVE(Meramera, MerameraNrvFloat)) && !isNerve(GET_NERVE(Meramera, MerameraNrvIgnition)) &&
           !isNerve(GET_NERVE(Meramera, MerameraNrvIgnitionForce)) && !isNerve(GET_NERVE(Meramera, MerameraNrvDown)) &&
           !isNerve(GET_NERVE(Meramera, MerameraNrvFlatDown)) && !isNerve(GET_NERVE(Meramera, MerameraNrvReadyRestart));
}

bool Meramera::isEnableChase() const {
    return MR::isNearPlayer(this, mChaseDistance) && mHomePosition.distance(mPosition) <= ::sTerritoryChaseRange;
}

void Meramera::addToTargetMovingAccel(const Vec3f& rTarget, f32 accel, f32 turnCos) {
    addMovingAccel(rTarget - mPosition, accel, turnCos);
}

void Meramera::addMovingAccel(const Vec3f& rDir, f32 accel, f32 turnCos) {
    Vec3f gravity(mGravity);
    Vec3f dirH;
    dirH.killElement(rDir, gravity);

    MR::separateScalarAndDirection(&mTargetDistance, &dirH, dirH);

    // turnCos outside (-1, 1) snaps the front to the direction.
    if (!MR::isNearZero(dirH)) {
        if (-1.0f < turnCos && turnCos < 1.0f) {
            MR::turnVecToVecCos(&mFront, mFront, dirH, turnCos, gravity);
        } else {
            mFront = dirH;
        }
    }

    mVelocity += mFront * accel;
}

void Meramera::addRunawayJumpPower() {
    // Toward a dive point (harder the farther it is), else away from a near player, else home.
    Vec3f dir;
    f32 rate = 1.0f;

    if (mCanDive) {
        dir.set(mDivePos - mPosition);

        Vec3f dirH;
        dirH.killElement(dir, mGravity);

        rate = MR::normalize(dirH.length(), ::sJumpDiveNear, ::sJumpDiveFar);
    } else if (MR::isNearPlayer(this, ::sJumpFleeDistance)) {
        dir.set(mPosition - *MR::getPlayerPos());
    } else {
        dir.set(mHomePosition - mPosition);
    }

    MR::normalizeOrZero(&dir);
    MR::addRandomVector(&dir, dir, ::sJumpRandom * rate);

    f32 speedH = ::sJumpSpeedH * (cNum_addRunawayJumpPower_f1 + rate * cNum_addRunawayJumpPower_f2);
    f32 randomSpeedV = MR::getRandom(::sJumpSpeedVMin, ::sJumpSpeedVMax);
    f32 speedV = (cNum_addRunawayJumpPower_f3 + rate * cNum_addRunawayJumpPower_f4) * randomSpeedV;
    addMovingAccel(dir, speedH, -1.0f);
    mVelocity -= mGravity * speedV;
}

void Meramera::addOverWallAccel(const Vec3f& rDir) {
    Vec3f gravity;
    gravity.set(mGravity);

    Vec3f dirH;
    dirH.killElement(rDir, gravity);
    MR::normalizeOrZero(&dirH);

    // A wall ahead and low enough to clear: rise.
    if (MR::getFirstPolyOnLineToMap(nullptr, nullptr, mPosition, dirH * ::sOverWallProbe) && MR::getShadowNearProjectionLength(this) < ::sMaxOverWallHeight) {
        mVelocity -= gravity * ::sOverWallAccel;
    }
}

void Meramera::updatePose() {
    if (isActionDisabled()) {
        MR::rotateQuatRollBall(&mPoseQuat, mVelocity, -mGravity, ::sRollRadius);
    } else {
        MR::blendQuatUpFront(&mPoseQuat, -mGravity, mFront, ::sPoseBlendRate, ::sPoseBlendRate);
    }
}

void Meramera::updateExtinguishMtx() {
    turnFireDirectionToSpin(::sExtinguishTurnDegree);

    Vec3f trans;
    mExtinguishMtx.getTrans(trans);

    Vec3f pullVelocity;
    MR::calcPlayerSpinPullVelocity(&pullVelocity, trans);

    trans += pullVelocity * ::sExtinguishPullRate;

    Vec3f toPlayer;
    toPlayer.set(*MR::getPlayerPos() - trans);
    f32 length = toPlayer.length();

    if (0.0f < length && length < ::sExtinguishMinDistance) {
        trans = *MR::getPlayerPos() + toPlayer * (::sExtinguishMinDistance / length);
    }

    if (MR::isNearZero(pullVelocity)) {
        mExtinguishMtx.identity();
        mExtinguishMtx.setTrans(trans);
    } else {
        MR::makeMtxUpNoSupportPos(&mExtinguishMtx, pullVelocity, trans);
    }
}

bool Meramera::checkDirectDivingPoint() {
    mCanDive = isHitRecover();

    if (mCanDive) {
        mDivePos.set(mPosition);
        mDiveUp.set(-mGravity);
    }

    return mCanDive;
}

bool Meramera::checkDivingPoint(f32 dist, f32 angleDegree) {
    mCanDive = false;

    if (checkDirectDivingPoint()) {
        return true;
    }

    // Down from a point dist ahead, turned by angleDegree.
    Vec3f start(mPosition);
    Vec3f ahead(mFront * dist);
    MR::rotateVecDegree(&ahead, mGravity, angleDegree);
    start += ahead;
    start -= mGravity * ::sDiveProbeLift;

    Vec3f probe(mGravity * ::sDiveProbeDepth + ahead);
    mCanDive = findDivingPoint(start, probe);

    return mCanDive;
}

bool Meramera::checkDivingPointMore(f32 dist) {
    Vec3f start;
    start.set(mDivePos);
    start += mDiveDir * dist;

    start -= mGravity * ::sDiveProbeLift;

    Vec3f probe(mGravity * ::sDiveProbeDepth + mDiveDir * dist);
    return findDivingPoint(start, probe);
}

bool Meramera::isHitRecover() const {
    switch (mElementType) {
    case ElementType_Fire:
        return MR::isBindedGroundDamageFire(this);

    case ElementType_Ice:
        return MR::isBindedGroundWater(this) || MR::isInWater(mPosition);
    }

    return false;
}

bool Meramera::findDivingPoint(Vec3f start, const Vec3f& rProbe) {
    // Only fire dives, into lava with a clear line to it.
    if (mElementType != ElementType_Fire) {
        return false;
    }

    Triangle triangle = Triangle();
    Vec3f hitPos;

    if (MR::isExistMapCollision(mPosition, start - mPosition) || !MR::getFirstPolyOnLineToMap(&hitPos, &triangle, start, rProbe) ||
        !MR::isGroundCodeDamageFire(&triangle)) {
        return false;
    }

    mDivePos.set(hitPos);
    mDiveUp.set(-mGravity);
    mDiveDir.killElement2(hitPos - mPosition, mDiveUp);
    MR::normalizeOrZero(&mDiveDir);
    return true;
}

void Meramera::resetAppear() {
    mPosition.set(mHomePosition);
    MR::zeroVelocity(this);
    MR::calcGravity(this);

    switch (mAppearStatus) {
    case Status_Runaway:
        setNerve(GET_NERVE(Meramera, MerameraNrvRunaway));
        break;

    case Status_Float:
        MR::startBck(this, "Wait");
        MR::hideModel(this);
        MR::offBind(this);

        MR::makeMtxUpNoSupportPos(&mFallEffectMtx, -mGravity, mPosition);

        deleteEffectHead(false);
        emitEffectColdBody();

        mDiveUp.set(-mGravity);
        mDivePos.set(mPosition);

        setNerve(GET_NERVE(Meramera, MerameraNrvFloat));
        break;

    default:
        setNerve(GET_NERVE(Meramera, MerameraNrvWait));
        break;
    }

    MR::resetPosition(this);
}

void Meramera::emitEffectHead(s32 effectType) {
    if (mEffectType == effectType) {
        return;
    }

    deleteEffectHead(false);

    mEffectType = effectType;

    const char* pEffectName = ::getHeadEffectName(mEffectType);

    if (pEffectName != nullptr) {
        MR::emitEffect(this, pEffectName);
    }
}

void Meramera::deleteEffectHead(bool useForce) {
    const char* pEffectName = ::getHeadEffectName(mEffectType);

    if (pEffectName == nullptr) {
        return;
    }

    if (useForce) {
        MR::forceDeleteEffect(this, pEffectName);
    } else {
        MR::deleteEffect(this, pEffectName);
    }

    mEffectType = EffectType_Null;
}

void Meramera::startBodyEffect(s32 bodyEffectType, const char* pAnimName) {
    if (mBodyEffectType == bodyEffectType) {
        return;
    }

    mBodyEffectType = bodyEffectType;

    MR::startBtp(this, pAnimName);
    MR::startBrk(this, pAnimName);
}

void Meramera::emitEffectHeatBody() {
    startBodyEffect(BodyEffectType_Heat, "OnFire");
}

void Meramera::emitEffectCoolDownBody() {
    startBodyEffect(BodyEffectType_CoolDown, "RedToBlack");
}

void Meramera::emitEffectColdBody() {
    startBodyEffect(BodyEffectType_Cold, "OffFire");
}

void Meramera::turnFireDirectionToSpin(f32 degree) {
    Vec3f jointPos;
    MR::copyJointPos(this, "Sin", &jointPos);

    Vec3f pullVelocity;
    MR::calcPlayerSpinPullVelocity(&pullVelocity, jointPos);

    if (MR::isNearZero(pullVelocity)) {
        return;
    }

    MR::normalize(pullVelocity, &pullVelocity);
    MR::turnVecToVecDegree(&mFireUp, mFireUp, pullVelocity, degree, mFront);
}

void Meramera::startWaitLevelSound() {
    MR::startLevelSound(this, mElementType == ElementType_Ice ? "SE_EM_LV_ICEMERA_WAIT" : "SE_EM_LV_MERAMERA_WAIT");
}

void Meramera::startChaseLevelSound() {
    MR::startLevelSound(this, mElementType == ElementType_Ice ? "SE_EM_LV_ICEMERA_CHASE" : "SE_EM_LV_MERAMERA_CHASE");
}

void Meramera::endRunaway() {
    MR::invalidateHitSensor(this, "break");
}

void Meramera::exeBindStarPointer() {
    MR::updateActorStateAndNextNerve(this, mWalkerStateBindStarPointer, GET_NERVE(Meramera, MerameraNrvWait));
}

void Meramera::endBindStarPointer() {
    mWalkerStateBindStarPointer->kill();
}

void Meramera::exeBindStarPointerNoFire() {
    MR::updateActorStateAndNextNerve(this, mWalkerStateBindStarPointer, GET_NERVE(Meramera, MerameraNrvRunaway));
}

void Meramera::exeReadyRestart() {
    if (MR::isGreaterStep(this, ::sReadyRestartTime)) {
        MR::showModel(this);
        resetAppear();
    }
}
