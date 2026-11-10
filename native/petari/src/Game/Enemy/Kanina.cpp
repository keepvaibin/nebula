#include "Game/Enemy/Kanina.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/AnimationRandomPlayer.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/BenefitItemOneUp.hpp"
#include "Game/Util.hpp"
#include "Game/Util/JointRumbler.hpp"
#include "Game/System/NebulaInput.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_getRunAwayBreakTimeRandom_f1, "getRunAwayBreakTimeRandom.f1", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_getRunAwayBreakTimeRandom_f2, "getRunAwayBreakTimeRandom.f2", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_getRunAwayBreakTimeRandom_f3, "getRunAwayBreakTimeRandom.f3", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_getRunAwayBreakTimeRandom_f4, "getRunAwayBreakTimeRandom.f4", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_getRunAwayBreakTimeRandom_f5, "getRunAwayBreakTimeRandom.f5", 0.75f);
NEBULA_TUNABLE_NAMED(f32, cNum_getRunAwayTimeRandom_f1, "getRunAwayTimeRandom.f1", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_getRunAwayTimeRandom_f2, "getRunAwayTimeRandom.f2", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_getRunAwayTimeRandom_f3, "getRunAwayTimeRandom.f3", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_getRunAwayTimeRandom_f4, "getRunAwayTimeRandom.f4", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_getRunAwayTimeRandom_f5, "getRunAwayTimeRandom.f5", 0.75f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 6.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_appearItemPunch_f1, "appearItemPunch.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_appearItemPunch_f2, "appearItemPunch.f2", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isPlayerBackward_f1, "isPlayerBackward.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isPlayerBackward_f2, "isPlayerBackward.f2", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryFindPlayer_f1, "tryFindPlayer.f1", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryFindPlayer_f2, "tryFindPlayer.f2", 30.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_tryHitWall_reboundVelocityFromCollision_1, "tryHitWall.reboundVelocityFromCollision.1", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryHitWall_reboundVelocityFromCollision_2, "tryHitWall.reboundVelocityFromCollision.2", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryHitWall_reboundVelocityFromCollision_3, "tryHitWall.reboundVelocityFromCollision.3", 1.0f);

namespace NrvKanina {
    NEW_NERVE(HostTypeAppear, Kanina, Appear);
    NEW_NERVE(HostTypeWait, Kanina, Wait);
    NEW_NERVE(HostTypeAttack, Kanina, Attack);
    NEW_NERVE(HostTypeVauntAttackSuccess, Kanina, VauntAttackSuccess);
    NEW_NERVE(HostTypeGuard, Kanina, Guard);
    NEW_NERVE(HostTypeGuardEnd, Kanina, GuardEnd);
    NEW_NERVE(HostTypeDamageFireBall, Kanina, DamageFireBall);
    NEW_NERVE(HostTypeDamageHipDrop, Kanina, DamageHipDrop);
    NEW_NERVE(HostTypeDamageTrampleInvincivle, Kanina, DamageHipDrop);
    NEW_NERVE(HostTypeDig, Kanina, Dig);
    NEW_NERVE(HostTypeWaitUnderGround, Kanina, WaitUnderGround);
    NEW_NERVE(HostTypeFindPlayer, Kanina, FindPlayer);
    NEW_NERVE(HostTypeRunAwayBreak, Kanina, RunAwayBreak);
    NEW_NERVE(HostTypeReboundEach, Kanina, ReboundEach);
    NEW_NERVE(HostTypeTurn, Kanina, Turn);
    NEW_NERVE(HostTypeTurnEnd, Kanina, TurnEnd);
    NEW_NERVE_ONEND(HostTypePointing, Kanina, Pointing, Pointing);
    NEW_NERVE(HostTypePointingEnd, Kanina, GuardEnd);
    NEW_NERVE(HostTypeWalk, Kanina, Walk);
    NEW_NERVE(HostTypeRunAway, Kanina, RunAway);
    NEW_NERVE(HostTypeRunAwayReboundDirection, Kanina, RunAwayReboundDirection);
    NEW_NERVE(HostTypeHitWall, Kanina, HitWall);

};  // namespace NrvKanina

namespace {
    NEBULA_TUNABLE(f32, sSensorRadius, 100.0f);
    NEBULA_TUNABLE(f32, sBinderRadius, 100.0f);
    NEBULA_TUNABLE(f32, sBinderOffsetY, 100.0f);
    NEBULA_TUNABLE(f32, sShadowRadius, 80.0f);
    NEBULA_TUNABLE(f32, sTerritoryRadius, 2000.0f);
    NEBULA_TUNABLE(s32, sStarPieceNum, 3);
    NEBULA_TUNABLE(s32, sCoinNum, 1);
    NEBULA_TUNABLE(f32, sPoseLerpRate, 0.3f);
    static const s32 sAnimIntervalBlink = NebulaFrameClock::stepsOfSeconds(2);
    NEBULA_TUNABLE(f32, sSensorRadiusAttack, 80.0f);
    NEBULA_TUNABLE(f32, sWalkSpeed, 3.0f);
    static const s32 sWalkTime = NebulaFrameClock::stepsOfSeconds(3);
    static const s32 sWaitTime = NebulaFrameClock::stepsOfSeconds(2);
    NEBULA_TUNABLE(s32, sPunchDamageTime, 15);
    NEBULA_TUNABLE(f32, sPunchDamageVelocity, 25.0f);
    NEBULA_TUNABLE(f32, sPunchUpperVec, 25.0f);
    NEBULA_TUNABLE(f32, sGravityAccel, 1.5f);
    NEBULA_TUNABLE(s32, sStopSceneTimePunchBackword, 8);
    NEBULA_TUNABLE(s32, sWaitUnderGroundTime, 120.0f);
    NEBULA_TUNABLE(f32, sKeepUnderGroundDistance, 500.0f);
    NEBULA_TUNABLE(f32, sSearchRadius, 1000.0f);
    NEBULA_TUNABLE(f32, sWalkSpeedRunAway, 13.0f);
    NEBULA_TUNABLE(f32, sAnimRateRunAway, 1.0f);
    NEBULA_TUNABLE(f32, sSafetyDistance, 800.0f);
    NEBULA_TUNABLE(s32, sRunAwayRunningTime1, 15);
    static const s32 sRunAwayRunningTime2 = NebulaFrameClock::stepsOfMilliseconds(500);
    static const s32 sRunAwayRunningTime3 = NebulaFrameClock::stepsOfSeconds(1);
    NEBULA_TUNABLE(s32, sRunAwayRunningTime4, 75);
    NEBULA_TUNABLE(s32, sRunAwayIntervalTime1, 10);
    static const s32 sRunAwayIntervalTime2 = NebulaFrameClock::stepsOfMilliseconds(500);
    static const s32 sRunAwayIntervalTime3 = NebulaFrameClock::stepsOfSeconds(1);
    NEBULA_TUNABLE(s32, sRunAwayIntervalTime4, 75);
    NEBULA_TUNABLE(f32, sRunAwayDirectionAngle, 60.0f);
    NEBULA_TUNABLE(f32, sRunAwayFaceAngle, 15.0f);
    NEBULA_TUNABLE(s32, sHitWallTime, 10);
    NEBULA_TUNABLE(f32, sWallHitReboundAcc, 6.0f);
    NEBULA_TUNABLE(f32, sWallHitReboundAccY, 20.0f);
    NEBULA_TUNABLE(f32, sGravityAccelHitWall, 2.2f);
    static const s32 sGuardTime = NebulaFrameClock::stepsOfMilliseconds(1500);
    NEBULA_TUNABLE(f32, sPunchAccel, 30.0f);
    NEBULA_TUNABLE(f32, sGuardFrec, 0.88f);
    NEBULA_TUNABLE(f32, sBackwardAngle, 30.0f);
    NEBULA_TUNABLE(f32, sTurnSpeed, 1.7f);
    static const s32 sTurnEndTime = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(f32, sBackwardAngleTurn, 120.0f);
    NEBULA_TUNABLE(s32, sHipDropDownTime, 70);
    NEBULA_TUNABLE(f32, sKinokoOneUpShootSpeed, 15.0f);
    NEBULA_TUNABLE(f32, sWalkSpeedRunAwayRed, 7.0f);
    NEBULA_TUNABLE(f32, sRunAwayDirectionAngleRed, 30.0f);
    NEBULA_TUNABLE(f32, sRunAwayFaceAngleRed, 45.0f);
    NEBULA_TUNABLE(s32, sWalkCount, 3);
    NEBULA_TUNABLE(f32, sUpVecBlendRate, 0.05f);
    NEBULA_TUNABLE(f32, sFrontVecBlendRate, 0.5f);
    NEBULA_TUNABLE(f32, sGroundFrec, 0.93f);
    NEBULA_TUNABLE(f32, sAirFrec, 0.99f);
    NEBULA_TUNABLE(f32, sPushAccel, 1.5f);
    NEBULA_TUNABLE(f32, sDownGravityAccel, 1.0f);
};  // namespace

namespace {
    u32 getRunAwayBreakTimeRandom() {
        f32 random = MR::getRandom();
        if (0.0f <= random && random < cNum_getRunAwayBreakTimeRandom_f1) {
            return ::sRunAwayIntervalTime1;
        }

        if (cNum_getRunAwayBreakTimeRandom_f2 <= random && random < cNum_getRunAwayBreakTimeRandom_f3) {
            return ::sRunAwayIntervalTime2;
        }

        if (cNum_getRunAwayBreakTimeRandom_f4 <= random && random < cNum_getRunAwayBreakTimeRandom_f5) {
            return ::sRunAwayIntervalTime3;
        }

        return ::sRunAwayIntervalTime4;
    }

    u32 getRunAwayTimeRandom() {
        f32 random = MR::getRandom();
        if (0.0f <= random && random < cNum_getRunAwayTimeRandom_f1) {
            return ::sRunAwayRunningTime1;
        }

        if (cNum_getRunAwayTimeRandom_f2 <= random && random < cNum_getRunAwayTimeRandom_f3) {
            return ::sRunAwayRunningTime2;
        }

        if (cNum_getRunAwayTimeRandom_f4 <= random && random < cNum_getRunAwayTimeRandom_f5) {
            return ::sRunAwayRunningTime3;
        }

        return ::sRunAwayRunningTime4;
    }
};  // namespace

Kanina::Kanina(const char* pName)
    : LiveActor(pName), mJointRumbler(), mKinokoOneUp(), mAnimationRandomPlayer(), mInitPos(gZeroVec), mRunAwayStep(15), mRunAwayBreakStep(10), mRunAwaySide(1.0f), mHitWallDir(0.0f, 0.0f, 1.0f),
      mType(KaninaType_Blue), mFront(0.0f, 0.0f, 1.0f), mWalkCount() {
    mPoseQuat.set(0.0f, 0.0f, 0.0f, 1.0f);
}

void Kanina::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::makeQuatAndFrontFromRotate(&mPoseQuat, &mFront, this);
    mInitPos.set(mPosition);

    const char* objectName = nullptr;

    MR::getObjectName(&objectName, rIter);
    initModelManagerWithAnm(objectName, nullptr, false);
    mAnimationRandomPlayer = new AnimationRandomPlayer(this, "Blink", "Normal", ::sAnimIntervalBlink, cNum_init_f1);
    MR::connectToSceneEnemy(this);
    MR::initLightCtrl(this);
    initSound(4, false);
    initHitSensor(2);
    MR::addHitSensorMtxEnemy(this, "body", 8, ::sSensorRadius, MR::getJointMtx(this, "Body"), Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorMtxEnemyAttack(this, "attack", 8, ::sSensorRadiusAttack, MR::getJointMtx(this, "Body"), Vec3f(0.0f, 0.0f, 0.0f));
    initBinder(::sBinderRadius, ::sBinderOffsetY, 8);
    initEffectKeeper(1, "Kanina");
    MR::addEffectHitNormal(this, nullptr);
    MR::onCalcGravity(this);
    MR::initShadowVolumeSphere(this, ::sShadowRadius);
    MR::addToAttributeGroupSearchTurtle(this);
    mJointRumbler = new JointRumbler(this, "Root", cNum_init_f2, cNum_init_f3, 45, true, 0);

    if (MR::isEqualString(objectName, "KaninaRed")) {
        initForType(rIter, KaninaType_Red);
    } else {
        initForType(rIter, KaninaType_Blue);
    }

    MR::initStarPointerTarget(this, ::sSensorRadius);
    initNerve(GET_NERVE(Kanina, HostTypeAppear));
    MR::needStageSwitchReadAppear(this, rIter);
    MR::syncStageSwitchAppear(this);
    makeActorDead();
}

void Kanina::initAfterPlacement() {
    MR::trySetMoveLimitCollision(this);
}

void Kanina::kill() {
    MR::emitEffect(this, "Death");
    MR::startSound(this, "SE_EM_EXPLODE_S");
    LiveActor::kill();
}

void Kanina::appearItemPunch() {
    if (mType == KaninaType_Red) {
        MR::appearStarPiece(this, mPosition, ::sStarPieceNum, cNum_appearItemPunch_f1, cNum_appearItemPunch_f2, false);
        MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
    } else if (mType == KaninaType_Blue) {
        MR::appearKinokoOneUpPop(mKinokoOneUp, getBaseMtx(), ::sKinokoOneUpShootSpeed);
    }
}

void Kanina::appearItemTrample() {
    if (mType == KaninaType_Red) {
        // As many coins as a punch gives star pieces.
        MR::appearCoinPop(this, mPosition, ::sStarPieceNum);
    } else if (mType == KaninaType_Blue) {
        MR::appearKinokoOneUpPop(mKinokoOneUp, getBaseMtx(), ::sKinokoOneUpShootSpeed);
    }
}

void Kanina::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorEnemyAttack(pSender)) {
        if (!tryAttack(pSender, pReceiver)) {
            if (MR::isSensorPlayer(pReceiver)) {
                MR::sendMsgPush(pReceiver, pSender);
            } else if (tryPushEach(pSender, pReceiver)) {
                return;
            }
        }
    } else if ((MR::isSensorPlayer(pReceiver) && !isStatePossibleToAttack()) || MR::isSensorEnemy(pReceiver)) {
        MR::sendMsgPush(pReceiver, pSender);
    }
}

bool Kanina::tryAttack(HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::isSensorPlayer(pReceiver)) {
        return false;
    }

    if (!MR::isSensorPlayer(pReceiver)) {
        return false;
    }

    if (!isStatePossibleToAttack()) {
        return false;
    }

    if (isPlayerBackward(::sBackwardAngle)) {
        return false;
    }

    if (!MR::sendMsgEnemyAttack(pReceiver, pSender)) {
        return false;
    }

    MR::emitEffectHitBetweenSensors(this, pSender, pReceiver, 0.0f, nullptr);
    setNerve(GET_NERVE(Kanina, HostTypeAttack));

    return true;
}

bool Kanina::tryPushEach(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pSender)) {
        return false;
    }

    if (MR::isSensorEnemyAttack(pReceiver)) {
        return false;
    }

    if (!MR::isSensorEnemyAttack(pSender)) {
        return false;
    }

    if (isNerve(GET_NERVE(Kanina, HostTypeReboundEach))) {
        return false;
    }

    if (!MR::sendMsgPush(pReceiver, pSender)) {
        return false;
    }

    Vec3f diff(pSender->mPosition - pReceiver->mPosition);
    MR::normalizeOrZero(&diff);
    Vec3f upVec;
    MR::calcUpVec(&upVec, this);
    MR::vecKillElement(diff, upVec, &diff);

    mVelocity.add(diff * ::sPushAccel + upVec * ::sPushAccel);
    setNerve(GET_NERVE(Kanina, HostTypeReboundEach));
    return true;
}

bool Kanina::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgPlayerTrample(msg)) {
        if (MR::isPlayerElementModeInvincible()) {
            appearItemPunch();
            kill();
            return true;
        }

        if (receiveTrample(pSender, pReceiver)) {
            MR::tryRumblePad(this, "中", PLAYER_1);
            return true;
        }
    }

    if (MR::isMsgFireBallAttack(msg)) {
        return receiveFireBall(pSender, pReceiver);
    }

    if (MR::isMsgPlayerHipDrop(msg)) {
        return receiveHipDrop(pSender, pReceiver);
    }

    if (MR::isMsgJetTurtleAttack(msg)) {
        return receiveFireBall(pSender, pReceiver);
    }

    if (MR::isMsgInvincibleAttack(msg)) {
        return receiveInvincibleAttack(pSender, pReceiver);
    }

    if (MR::isMsgPlayerHitAll(msg)) {
        return receivePunch(pSender, pReceiver);
    }

    if (MR::isMsgStarPieceReflect(msg)) {
        setNerve(GET_NERVE(Kanina, HostTypeGuard));
        return true;
    }

    return false;
}

bool Kanina::receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pSender)) {
        return false;
    }

    MR::addVelocityFromPush(this, ::sPushAccel, pSender, pReceiver);

    if (!pSender->isType(ATYPE_COCO_NUT)) {
        setNerve(GET_NERVE(Kanina, HostTypeReboundEach));
    }

    return true;
}

bool Kanina::receiveTrample(HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (!isNerve(GET_NERVE(Kanina, HostTypeAttack))) {
        setNerve(GET_NERVE(Kanina, HostTypeGuard));
        return true;
    }

    return false;
}

bool Kanina::receivePunch(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorEnemyAttack(pReceiver)) {
        return false;
    }

    if (isPlayerBackward(::sBackwardAngle)) {
        MR::stopScene(::sStopSceneTimePunchBackword);
        doDamageFireBall(pSender, pReceiver);
        return true;
    }

    if (!isNerve(GET_NERVE(Kanina, HostTypeAttack))) {
        if (!isStateDamageNoFireBall()) {
            Vec3f vec;
            MR::calcVecFromTargetPosH(&vec, this, pSender->mPosition, nullptr);
            mVelocity.add(vec * ::sPunchAccel);
            setNerve(GET_NERVE(Kanina, HostTypeGuard));
            return true;
        }
    }

    return false;
}

bool Kanina::receiveFireBall(HitSensor* pSender, HitSensor* pReceiver) {
    if (pReceiver == getSensor("attack")) {
        return false;
    }

    doDamageFireBall(pSender, pReceiver);
    return true;
}

void Kanina::doDamageFireBall(HitSensor* pSender, HitSensor* pReceiver) {
    MR::setVelocityBlowAttack(this, pSender, pReceiver, ::sPunchDamageVelocity, ::sPunchUpperVec, 4);
    Vec3f vec;
    MR::vecKillElement(mVelocity, -mGravity, &vec);
    MR::normalizeOrZero(&vec);
    mFront.set(-vec);
    setNerve(GET_NERVE(Kanina, HostTypeDamageFireBall));
}

void Kanina::startRun() {
    if (mType == KaninaType_Red) {
        MR::startBck(this, "ChaseRun");
        MR::startBtp(this, "Angry");
    } else {
        MR::startBck(this, "Run");
    }

    MR::setBckRate(this, ::sAnimRateRunAway);

    Vec3f upVec;
    MR::calcUpVec(&upVec, this);

    Vec3f vecFromPlayerH;
    MR::calcVecFromPlayerH(&vecFromPlayerH, this);

    f32 BC = mRunAwaySide;
    f32 angle = -1.0f * BC * getFloatDependentType(::sRunAwayDirectionAngleRed, ::sRunAwayDirectionAngle);

    Pos3f rotate;
    rotate.identity();
    rotate.makeRotate(upVec, MR::toRadian(angle));

    Vec3f multVec(vecFromPlayerH);
    rotate.mult(multVec, multVec);

    if (mType == KaninaType_Red) {
        multVec.negate();
    }

    MR::normalizeOrZero(&multVec);

    mVelocity.set(multVec * getFloatDependentType(::sWalkSpeedRunAwayRed, ::sWalkSpeedRunAway));

    f32 faceAngle = -1.0f * BC *
                    (getFloatDependentType(::sRunAwayDirectionAngleRed, ::sRunAwayDirectionAngle) +
                     getFloatDependentType(::sRunAwayFaceAngleRed, ::sRunAwayFaceAngle));
    Pos3f rotate2;
    rotate2.identity();
    rotate2.makeRotate(upVec, MR::toRadian(faceAngle));
    Vec3f multVec2(vecFromPlayerH);
    rotate2.mult(multVec2, multVec2);

    Quat4f quat;
    MR::makeQuatSideUp(&quat, multVec2 * BC, upVec);
    quat.getZDir(mFront);

    mRunAwayStep = ::getRunAwayTimeRandom();
    mRunAwaySide *= -1.0f;
}

void Kanina::startRunAwayLevelSound() {
    if (mType == KaninaType_Blue) {
        MR::startLevelSound(this, "SE_EM_LV_KANINA_AWAY");
    } else if (mType == KaninaType_Red && isNerve(GET_NERVE(Kanina, HostTypeRunAwayReboundDirection))) {
        MR::startLevelSound(this, "SE_EM_LV_KANINA_AWAY");
    }
}

bool Kanina::receiveHipDrop(HitSensor* pSender, HitSensor* pReceiver) {
    if (receiveTrample(pSender, pReceiver)) {
        MR::tryRumblePad(this, "中", PLAYER_1);
        MR::sendMsgAwayJump(pSender, pReceiver);
        return true;
    }

    return false;
}

bool Kanina::receiveInvincibleAttack(HitSensor* pSender, HitSensor* pReceiver) {
    if (pReceiver == getSensor("attack")) {
        return false;
    }

    doDamageFireBall(pSender, pReceiver);
    return true;
}

void Kanina::calcAndSetBaseMtx() {
    Pos3f baseMtx(getBaseMtx());
    Quat4f quat;
    baseMtx.getQuat(quat);

    if (isNerve(GET_NERVE(Kanina, HostTypeDamageFireBall))) {
        quat.set(mPoseQuat);
    } else {
        quat.slerp(mPoseQuat, ::sPoseLerpRate);
    }

    MR::setBaseTRMtx(this, quat);
}

void Kanina::control() {
    mJointRumbler->update();
    MR::blendQuatFromGroundAndFront(&mPoseQuat, this, mFront, ::sUpVecBlendRate, ::sFrontVecBlendRate);
    updateMovement();

    if (MR::isInWater(this, Vec3f(0.0f, 0.0f, 0.0f))) {
        MR::setSeVersion(this, SeVersion_WATER);
    } else {
        MR::setSeVersion(this, SeVersion_NORMAL);
    }

    if (isStateBlink()) {
        mAnimationRandomPlayer->updateNerve();
    }
}

bool Kanina::isStateStayOnGround() const {
    if (isNerve(GET_NERVE(Kanina, HostTypeWalk))) {
        return false;
    }

    if (isNerve(GET_NERVE(Kanina, HostTypeRunAway))) {
        return false;
    }

    if (isNerve(GET_NERVE(Kanina, HostTypeRunAwayReboundDirection))) {
        return false;
    }

    if (isNerve(GET_NERVE(Kanina, HostTypeDamageFireBall))) {
        return false;
    }

    return true;
}

void Kanina::initForType(const TableIter& /*rIter*/, KaninaType type) {
    mType = type;
    if (mType == KaninaType_Red) {
        MR::declareStarPiece(this, ::sStarPieceNum);
        MR::declareCoin(this, ::sCoinNum);
    }

    if (mType == KaninaType_Blue) {
        mKinokoOneUp = MR::createKinokoOneUp();
    }
}

bool Kanina::isPlayerBackward(f32 angle) const {
    Vec3f frontVec;
    MR::calcFrontVec(&frontVec, this);
    angle = angle * cNum_isPlayerBackward_f1;
    return MR::isInSightFanPlayer(this, -frontVec, ::sSafetyDistance, angle, cNum_isPlayerBackward_f2);
}

bool Kanina::isStatePossibleToAttack() const {
    if (isNerve(GET_NERVE(Kanina, HostTypeAttack))) {
        return false;
    }

    if (isStateGuard()) {
        return false;
    }

    if (isStateDamageNoFireBall()) {
        return false;
    }

    return true;
}

bool Kanina::isStateBlink() const {
    return isNerve(GET_NERVE(Kanina, HostTypeWait)) || isNerve(GET_NERVE(Kanina, HostTypeVauntAttackSuccess)) ||
           isNerve(GET_NERVE(Kanina, HostTypeWalk)) || isNerve(GET_NERVE(Kanina, HostTypeRunAway)) ||
           isNerve(GET_NERVE(Kanina, HostTypeRunAwayReboundDirection)) || isNerve(GET_NERVE(Kanina, HostTypeRunAwayBreak)) ||
           isNerve(GET_NERVE(Kanina, HostTypeReboundEach)) || isNerve(GET_NERVE(Kanina, HostTypeTurn)) ||
           isNerve(GET_NERVE(Kanina, HostTypeTurnEnd)) || isNerve(GET_NERVE(Kanina, HostTypeFindPlayer));
}

bool Kanina::isStateGuard() const {
    return isNerve(GET_NERVE(Kanina, HostTypeGuard)) || isNerve(GET_NERVE(Kanina, HostTypeGuardEnd));
}

bool Kanina::isStateDamageNoFireBall() const {
    return isNerve(GET_NERVE(Kanina, HostTypeDamageHipDrop)) || isNerve(GET_NERVE(Kanina, HostTypeDamageTrampleInvincivle));
}

bool Kanina::isBindedGroundWaterBottom() const {
    return MR::isBindedGroundWaterBottomH(this) || MR::isBindedGroundWaterBottomM(this);
}

void Kanina::updateMovement() {
    if (!MR::isOnGround(this)) {
        f32 gravityAccel = ::sGravityAccel;
        if (isNerve(GET_NERVE(Kanina, HostTypeDamageFireBall))) {
            gravityAccel = ::sDownGravityAccel;
        } else if (isNerve(GET_NERVE(Kanina, HostTypeHitWall))) {
            gravityAccel = ::sGravityAccelHitWall;
        }

        MR::addVelocityToGravity(this, gravityAccel);
    }

    if (!isStateStayOnGround()) {
        return;
    }

    f32 frec;

    if (MR::isOnGround(this)) {
        frec = ::sGroundFrec;
    } else {
        frec = ::sAirFrec;
    }

    if (isStateGuard()) {
        frec = ::sGuardFrec;
    }

    MR::attenuateVelocity(this, frec);
}

bool Kanina::tryFindPlayer() {
    if (MR::isPlayerHidden()) {
        return false;
    }

    Vec3f front;
    MR::calcFrontVec(&front, this);
    if (!MR::isInSightFanPlayer(this, front, ::sSearchRadius, cNum_tryFindPlayer_f1, cNum_tryFindPlayer_f2)) {
        return false;
    }

    setNerve(GET_NERVE(Kanina, HostTypeFindPlayer));
    return true;
}

bool Kanina::tryHitWall() {
    if (!MR::isBindedWall(this)) {
        return false;
    }

    if (MR::isBindedWallOfMoveLimit(this)) {
        return false;
    }

    if (!MR::reboundVelocityFromCollision(this, cNum_tryHitWall_reboundVelocityFromCollision_1, cNum_tryHitWall_reboundVelocityFromCollision_2, cNum_tryHitWall_reboundVelocityFromCollision_3)) {
        return false;
    }

    mHitWallDir.set(mVelocity);
    MR::normalizeOrZero(&mHitWallDir);
    setNerve(GET_NERVE(Kanina, HostTypeHitWall));
    return true;
}

bool Kanina::tryTurn() {
    if (mType == KaninaType_Blue) {
        return false;
    }

    if (!isPlayerBackward(::sBackwardAngleTurn)) {
        return false;
    }

    setNerve(GET_NERVE(Kanina, HostTypeTurn));
    return true;
}

bool Kanina::tryPointing() {
    if (MR::isStarPointerPointing2POnPressButton(this, "弱", true, false)) {
        setNerve(GET_NERVE(Kanina, HostTypePointing));
        return true;
    }

    return false;
}

void Kanina::exeAppear() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Appear");
        MR::startBtp(this, "Normal");
        MR::validateHitSensors(this);
    }

    MR::startLevelSound(this, "SE_EM_LV_KANINA_PRE_APPEAR");

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(Kanina, HostTypeWait));
    }
}

void Kanina::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::zeroVelocity(this);
        MR::startBck(this, "Wait");
    }

    if (!tryFindPlayer() && !tryPointing() && MR::isStep(this, ::sWaitTime)) {
        if (isBindedGroundWaterBottom()) {
            setNerve(GET_NERVE(Kanina, HostTypeDig));
            return;
        }

        if (mType == KaninaType_Blue && mWalkCount >= ::sWalkCount) {
            setNerve(GET_NERVE(Kanina, HostTypeDig));
            return;
        }

        setNerve(GET_NERVE(Kanina, HostTypeWalk));
    }
}

namespace {
    inline void calcWalkDirection(Vec3f* pDirection, const LiveActor* pActor) {
        Vec3f front;
        Vec3f up;
        MR::calcFrontVec(&front, pActor);
        MR::calcUpVec(&up, pActor);

        f32 angle = MR::getRandom(-HALF_PI, HALF_PI);

        Pos3f rotate;
        rotate.identity();
        rotate.makeRotate(up, angle);
        rotate.mult(front, front);

        MR::normalize(&front);
        pDirection->set(front);
    }
};  // namespace

void Kanina::exeWalk() {
    if (MR::isFirstStep(this)) {
        mWalkCount++;
        MR::startBck(this, "Walk");

        Vec3f walkDir;
        ::calcWalkDirection(&walkDir, this);
        mFront.set(walkDir);

        MR::blendQuatFromGroundAndFront(&mPoseQuat, this, mFront, ::sUpVecBlendRate, ::sFrontVecBlendRate);

        Vec3f quatX;
        mPoseQuat.getXDir(quatX);
        mVelocity.set(quatX * (::sWalkSpeed * MR::getSignHalfProbability()));
    }

    if (!tryFindPlayer() && !tryPointing() && MR::isStep(this, ::sWalkTime)) {
        MR::zeroVelocity(this);
        setNerve(GET_NERVE(Kanina, HostTypeWait));
    }
}

void Kanina::exeRunAway() {
    if (MR::isFirstStep(this)) {
        startRun();
    }

    startRunAwayLevelSound();

    if (!tryHitWall() && !tryPointing()) {
        if (::sTerritoryRadius < MR::calcDistanceToPlayerH(this)) {
            MR::zeroVelocity(this);
            if ((mType == KaninaType_Red && MR::isHalfProbability()) || mType == KaninaType_Blue) {
                setNerve(GET_NERVE(Kanina, HostTypeDig));
            } else {
                setNerve(GET_NERVE(Kanina, HostTypeWait));
            }
        } else if (MR::isStep(this, mRunAwayStep)) {
            MR::zeroVelocity(this);
            setNerve(GET_NERVE(Kanina, HostTypeRunAwayBreak));
        }
    }
}

void Kanina::exeRunAwayReboundDirection() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Run");
        MR::setBckRate(this, ::sAnimRateRunAway);
        Vec3f up;
        MR::calcUpVec(&up, this);
        Vec3f vec(mHitWallDir);
        MR::vecKillElement(vec, up, &vec);
        MR::normalizeOrZero(&vec);
        mVelocity.set(vec * getFloatDependentType(::sWalkSpeedRunAwayRed, ::sWalkSpeedRunAway));
        mRunAwayStep = ::getRunAwayBreakTimeRandom();
        mRunAwaySide *= -1.0f;
    }

    startRunAwayLevelSound();

    if (!tryHitWall() && !tryPointing()) {
        if (::sTerritoryRadius < MR::calcDistanceToPlayer(this)) {
            setNerve(GET_NERVE(Kanina, HostTypeWait));
            return;
        }

        if (MR::isStep(this, mRunAwayStep)) {
            setNerve(GET_NERVE(Kanina, HostTypeRunAwayBreak));
        }
    }
}

void Kanina::exeRunAwayBreak() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
        mRunAwayBreakStep = ::getRunAwayBreakTimeRandom();
    }

    if (!tryTurn() && !tryPointing() && MR::isStep(this, mRunAwayBreakStep)) {
        if (isBindedGroundWaterBottom()) {
            setNerve(GET_NERVE(Kanina, HostTypeDig));
            return;
        }

        setNerve(GET_NERVE(Kanina, HostTypeRunAway));
    }
}

void Kanina::exeAttack() {
    if (MR::isFirstStep(this)) {
        MR::calcVecToPlayerH(&mFront, this, nullptr);
        MR::startBck(this, "Attack");
        MR::startSound(this, "SE_EM_KANINA_ATTACK_FRONT");
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(Kanina, HostTypeVauntAttackSuccess));
    }
}

void Kanina::exeVauntAttackSuccess() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Vaunt");
        MR::startBtp(this, "Angry");
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(Kanina, HostTypeRunAwayBreak));
    }
}

void Kanina::exeHitWall() {
    if (MR::isFirstStep(this)) {
        mJointRumbler->start();
        Vec3f up;
        MR::calcUpVec(&up, this);
        mVelocity.set(mHitWallDir * ::sWallHitReboundAcc + up * ::sWallHitReboundAccY);
        Vec3f vec(mHitWallDir);
        MR::vecKillElement(vec, up, &vec);
        MR::normalizeOrZero(&vec);
        MR::makeQuatSideUp(&mPoseQuat, vec, up);
    }

    startRunAwayLevelSound();

    if (MR::isGreaterStep(this, ::sHitWallTime) && MR::isBindedGround(this)) {
        setNerve(GET_NERVE(Kanina, HostTypeRunAwayReboundDirection));
    }
}

void Kanina::exeReboundEach() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
        mJointRumbler->start();
    }

    if (MR::isGreaterStep(this, ::sHitWallTime) && MR::isBindedGround(this)) {
        setNerve(GET_NERVE(Kanina, HostTypeRunAwayReboundDirection));
    }
}

void Kanina::exeGuard() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Guard");
        MR::startSound(this, "SE_EM_KANINA_GUARD");
        MR::tryRumbleDefaultHit(this, 0);
        MR::calcVecToPlayerH(&mFront, this, nullptr);
    }

    if (MR::isStep(this, ::sGuardTime)) {
        setNerve(GET_NERVE(Kanina, HostTypeGuardEnd));
    }
}

void Kanina::exeGuardEnd() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "GuardReturn");
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(Kanina, HostTypeRunAwayBreak));
    }
}

void Kanina::exeDamageHipDrop() {
    if (MR::isFirstStep(this)) {
        if (isNerve(GET_NERVE(Kanina, HostTypeDamageHipDrop)) || isNerve(GET_NERVE(Kanina, HostTypeDamageTrampleInvincivle))) {
            MR::startBck(this, "HipDropDown");
        }

        MR::startBtp(this, "Swoon");
        MR::startSound(this, "SE_EM_STOMPED_S");
        MR::tryRumblePad(this, "強", PLAYER_1);
        MR::invalidateHitSensors(this);
    }

    if (MR::isStep(this, ::sHipDropDownTime)) {
        appearItemTrample();
        kill();
    }
}

void Kanina::exeDamageFireBall() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "PunchDown");
        MR::startBtp(this, "Swoon");
        MR::startBlowHitSound(this);
        MR::invalidateClipping(this);
        MR::invalidateHitSensors(this);
    }

    if (MR::isStep(this, ::sPunchDamageTime)) {
        MR::validateClipping(this);
        MR::validateHitSensors(this);
        appearItemPunch();
        kill();
    }
}

void Kanina::exeDig() {
    if (MR::isFirstStep(this)) {
        mWalkCount = 0;
        MR::startBck(this, "Dig");
    }

    MR::startLevelSound(this, "SE_EM_LV_KANINA_DIG");

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(Kanina, HostTypeWaitUnderGround));
    }
}

void Kanina::exeWaitUnderGround() {
    if (MR::isFirstStep(this)) {
        MR::zeroVelocity(this);
        MR::invalidateShadow(this, nullptr);
        mPosition.set(mInitPos);
    }

    if (MR::isGreaterStep(this, ::sWaitUnderGroundTime) && ::sKeepUnderGroundDistance < MR::calcDistanceToPlayer(this)) {
        MR::validateShadow(this, nullptr);
        setNerve(GET_NERVE(Kanina, HostTypeAppear));
    }
}

void Kanina::exeFindPlayer() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Search");
        MR::startSound(this, "SE_EV_KANINA_FIND");
        MR::calcVecToPlayerH(&mFront, this, nullptr);
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(Kanina, HostTypeRunAway));
    }
}

void Kanina::exeTurn() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Turn");
    }

    if (MR::turnDirectionToTargetUseGroundNormalDegree(this, &mFront, *MR::getPlayerPos(), ::sTurnSpeed)) {
        setNerve(GET_NERVE(Kanina, HostTypeTurnEnd));
    }
}

void Kanina::exeTurnEnd() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
    }

    if (MR::isStep(this, ::sTurnEndTime)) {
        setNerve(GET_NERVE(Kanina, HostTypeRunAway));
    }
}

void Kanina::exePointing() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Guard");
        MR::startPointerHitSound();
    }

    if (MR::isStarPointerPointing2POnPressButton(this, "弱", true, false)) {
        MR::startPointerFreezeLevelSound(this);
        MR::tryEmitEffect(this, "Touch");
    }

    if (!MR::isStarPointerPointing2POnPressButton(this, "弱", true, false)) {
        setNerve(GET_NERVE(Kanina, HostTypePointingEnd));
    }
}

void Kanina::endPointing() {
    MR::tryDeleteEffect(this, "Touch");
}
