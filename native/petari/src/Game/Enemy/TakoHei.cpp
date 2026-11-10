#include "Game/Enemy/TakoHei.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/Enemy/TakoHeiInkHolder.hpp"
#include "Game/Enemy/WalkerStateBindStarPointer.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_kill_f1, "kill.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_kill_f2, "kill.f2", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updatePose_f1, "updatePose.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_updatePose_f2, "updatePose.f2", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_isFallNextMove_f1, "isFallNextMove.f1", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isFallNextMove_f2, "isFallNextMove.f2", 90.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isFallNextMove_f3, "isFallNextMove.f3", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isFallNextMove_f4, "isFallNextMove.f4", 150.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_exePursue_isNearPlayer_1, "exePursue.isNearPlayer.1", 700.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateNormalVelocity_reboundVelocityFromCollision_1, "updateNormalVelocity.reboundVelocityFromCollision.1", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateNormalVelocity_reboundVelocityFromCollision_2, "updateNormalVelocity.reboundVelocityFromCollision.2", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateNormalVelocity_reboundVelocityFromCollision_3, "updateNormalVelocity.reboundVelocityFromCollision.3", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateSwoonVelocity_reboundVelocityFromCollision_1, "updateSwoonVelocity.reboundVelocityFromCollision.1", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateSwoonVelocity_reboundVelocityFromCollision_2, "updateSwoonVelocity.reboundVelocityFromCollision.2", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateSwoonVelocity_reboundVelocityFromCollision_3, "updateSwoonVelocity.reboundVelocityFromCollision.3", 1.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exePunchDown_1, "exePunchDown.1", 5);

namespace {
    static Vec3f sInkShotOffset(0.0f, 20.0f, 0.0f);
    NEBULA_TUNABLE(f32, sBaseScale, 1.0f);
    NEBULA_TUNABLE(f32, sBodyHitRadius, 70.0f);
    NEBULA_TUNABLE(f32, sBodyHitYOffset, 70.0f);
    NEBULA_TUNABLE(f32, sAttackHitRadius, 50.0f);
    NEBULA_TUNABLE(f32, sAttackHitYOffset, 70.0f);
    NEBULA_TUNABLE(f32, sCollisionRadius, 90.0f);
    NEBULA_TUNABLE(f32, sShadowRadius, 60.0f);
    NEBULA_TUNABLE(f32, sPointerHitSize, 70.0f);
    NEBULA_TUNABLE(f32, sPushAccel, 5.0f);
    NEBULA_TUNABLE(f32, sPushScaleYSpeed, 0.1f);
    NEBULA_TUNABLE(s32, sNoPushTime, 20);
    NEBULA_TUNABLE(f32, sPushScaleYMarioSpeed, 0.15f);
    NEBULA_TUNABLE(f32, sPushMarioAccel, 12.0f);
    static const s32 sNoPushMarioTime = NebulaFrameClock::stepsOfSeconds(1);
    NEBULA_TUNABLE(f32, sNormalGroundFrec, 0.85f);
    NEBULA_TUNABLE(f32, sNormalAirFrec, 0.99f);
    NEBULA_TUNABLE(f32, sNormalGravityAccel, 1.5f);
    NEBULA_TUNABLE(f32, sActiveDistance, 3000.0f);
    static const s32 sWaitTime = NebulaFrameClock::stepsOfSeconds(2);
    NEBULA_TUNABLE(s32, sWalkMoveStartTime, 10);
    static const s32 sWalkEndTime = NebulaFrameClock::stepsOfSeconds(2);
    NEBULA_TUNABLE(f32, sWalkAccel, 0.2f);
    NEBULA_TUNABLE(f32, sWalkGoalDistance, 500.0f);
    NEBULA_TUNABLE(f32, sWalkTurnDegree, 6.0f);
    NEBULA_TUNABLE(f32, sWalkTerritoryRadius, 400.0f);
    NEBULA_TUNABLE(s32, sFindTurnTime, 20);
    static const s32 sFindTime = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(f32, sFindDistance, 1600.0f);
    NEBULA_TUNABLE(f32, sFindTurnDegree, 6.0f);
    static const s32 sPursueTime = NebulaFrameClock::stepsOfSeconds(5);
    NEBULA_TUNABLE(f32, sPursueAccel, 0.5f);
    NEBULA_TUNABLE(f32, sPursueTurnDegree, 6.0f);
    static const s32 sCoolDownTime = NebulaFrameClock::stepsOfSeconds(1);
    NEBULA_TUNABLE(f32, sAttackSignDegree, 8.0f);
    NEBULA_TUNABLE(f32, sAttackDistanceNear, 650.0f);
    NEBULA_TUNABLE(f32, sAttackDistanceFar, 750.0f);
    NEBULA_TUNABLE(s32, sForceFarAttackTime, 250);
    NEBULA_TUNABLE(f32, sSpurtPowerH, 20.0f);
    NEBULA_TUNABLE(f32, sSpurtPowerV, -15.0f);
    NEBULA_TUNABLE(f32, sCheckFrontHeight, 40.0f);
    NEBULA_TUNABLE(f32, sCheckFrontAttackSpace, 140.0f);
    NEBULA_TUNABLE(f32, sBackAttackSpaceAccel, -0.1f);
    static const s32 sAttackSuccessTime = NebulaFrameClock::stepsOfSeconds(1);
    NEBULA_TUNABLE(f32, sAttackSuccessHitPower, 8.0f);
    static const s32 sSwoonTime = NebulaFrameClock::stepsOfSeconds(3);
    NEBULA_TUNABLE(s32, sRecoverTime, 20);
    NEBULA_TUNABLE(f32, sSwoonGroundFrec, 0.85f);
    NEBULA_TUNABLE(f32, sSwoonGravityAccel, 1.5f);
    NEBULA_TUNABLE(s32, sPunchStopSceneTime, 2);
    NEBULA_TUNABLE(s32, sPunchDownTime, 22);
    NEBULA_TUNABLE(f32, sPunchHorizonPower, 20.0f);
    NEBULA_TUNABLE(f32, sPunchVerticalPower, 35.0f);
    NEBULA_TUNABLE(s32, sFlatDownTime, 20);
    NEBULA_TUNABLE(s32, sHipDropDownTime, 40);
    static const s32 sPressDownTime = NebulaFrameClock::stepsOfSeconds(3);
};  // namespace

namespace NrvTakoHei {
    NEW_NERVE(TakoHeiNrvNonActive, TakoHei, NonActive);
    NEW_NERVE(TakoHeiNrvWait, TakoHei, Wait);
    NEW_NERVE(TakoHeiNrvWalk, TakoHei, Walk);
    NEW_NERVE(TakoHeiNrvFindTurn, TakoHei, FindTurn);
    NEW_NERVE(TakoHeiNrvFind, TakoHei, Find);
    NEW_NERVE(TakoHeiNrvPursue, TakoHei, Pursue);
    NEW_NERVE(TakoHeiNrvCoolDown, TakoHei, CoolDown);
    NEW_NERVE(TakoHeiNrvAttackSign, TakoHei, AttackSign);
    NEW_NERVE(TakoHeiNrvAttack, TakoHei, Attack);
    NEW_NERVE(TakoHeiNrvAttackSuccess, TakoHei, AttackSuccess);
    NEW_NERVE_ONEND(TakoHeiNrvBindStarPointer, TakoHei, BindStarPointer, BindStarPointer);
    NEW_NERVE(TakoHeiNrvSwoon, TakoHei, Swoon);
    NEW_NERVE(TakoHeiNrvRecover, TakoHei, Recover);
    NEW_NERVE(TakoHeiNrvPressDown, TakoHei, PressDown);
    NEW_NERVE(TakoHeiNrvHipDropDown, TakoHei, HipDropDown);
    NEW_NERVE(TakoHeiNrvFlatDown, TakoHei, FlatDown);
    NEW_NERVE(TakoHeiNrvPunchDown, TakoHei, PunchDown);
};  // namespace NrvTakoHei

TakoHei::TakoHei(const char* pName)
    : LiveActor(pName), mScaleCtrl(0), mBindStarPointer(0), mPoseQuat(0.0f, 0.0f, 0.0f, 1.0f), mFront(0, 0, 1), mWalkTarget(0, 0, 0), mInitPos(0, 0, 0), mDeathItem(0),
      mNoPushStep(0), mHasMoveLimit(0) {
    MR::createTakoHeiInkHolder();
}

void TakoHei::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    mScale.mult(::sBaseScale);
    initModelManagerWithAnm("TakoHei", nullptr, false);
    MR::connectToSceneEnemy(this);
    MR::initLightCtrl(this);
    MR::onCalcGravity(this);
    mInitPos = mPosition;
    MR::makeQuatAndFrontFromRotate(&mPoseQuat, &mFront, this);
    MR::addToAttributeGroupSearchTurtle(this);
    initBinder(::sCollisionRadius * mScale.y, ::sCollisionRadius * mScale.y, 0);
    initEffectKeeper(0, nullptr);
    initSound(4, false);
    initSensor();
    MR::initShadowVolumeSphere(this, ::sShadowRadius);
    initNerve(GET_NERVE(TakoHei, TakoHeiNrvWait));
    mScaleCtrl = new AnimScaleController(nullptr);
    mBindStarPointer = new WalkerStateBindStarPointer(this, mScaleCtrl);

    MR::declareStarPiece(this, 3);
    MR::declareCoin(this, 1);
    makeActorAppeared();
}

void TakoHei::initAfterPlacement() {
    MR::turnQuatUpToGravity(&mPoseQuat, mPoseQuat, this);
    mHasMoveLimit = MR::trySetMoveLimitCollision(this);
}

void TakoHei::initSensor() {
    const f32 scaleX = mScale.x;
    initHitSensor(2);
    MR::addHitSensorEnemyAttack(this, "attack", 8, ::sAttackHitRadius * scaleX, Vec3f(0.0f, ::sAttackHitYOffset * scaleX, 0.0f));
    MR::addHitSensorEnemy(this, "body", 8, ::sBodyHitRadius * scaleX, Vec3f(0.0f, ::sBodyHitYOffset * scaleX, 0.0f));
    MR::initStarPointerTarget(this, ::sPointerHitSize * mScale.y, Vec3f(0.0f, ::sPointerHitSize * mScale.y, 0.0f));
}

void TakoHei::kill() {
    LiveActor::kill();
    MR::emitEffect(this, "Death");
    MR::startSound(this, "SE_EM_EXPLODE_S");

    switch (mDeathItem) {
    case 1:
        MR::appearCoinPop(this, mPosition, 1);
        break;
    case 2:
        MR::appearStarPiece(this, mPosition, 3, cNum_kill_f1, cNum_kill_f2, false);
        MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
        break;
    }
}

void TakoHei::control() {
    if (isNerve(GET_NERVE(TakoHei, TakoHeiNrvNonActive))) {
        return;
    }

    mScaleCtrl->updateNerve();
    if (mNoPushStep != 0) {
        mNoPushStep--;
    }

    updatePose();
    tryPressed();
}

void TakoHei::calcAndSetBaseMtx() {
    MR::setBaseTRMtx(this, mPoseQuat);
    Vec3f scale = mScaleCtrl->mScale * mScale;
    MR::setBaseScale(this, scale);
}

void TakoHei::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorEnemyAttack(pSender)) {
        if (MR::isSensorPlayer(pReceiver) && canAttack()) {
            if (!MR::isPlayerHipDropFalling() && MR::sendMsgEnemyAttack(pReceiver, pSender)) {
                requestAttackSuccess(pSender, pReceiver);
            } else {
                MR::sendMsgPush(pReceiver, pSender);
            }
        }
    } else if ((MR::isSensorPlayer(pReceiver) && !canAttack()) || MR::isSensorEnemy(pReceiver)) {
        if (MR::sendMsgPush(pReceiver, pSender) && isPushMovable()) {
            Vec3f sensorDirection;
            MR::calcSensorDirectionNormalize(&sensorDirection, pReceiver, pSender);
            if (mVelocity.dot(sensorDirection) < 0.0f) {
                mVelocity.orthogonalize(sensorDirection);
            }
        }
    }
}

bool TakoHei::receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::isSensorEnemyAttack(pReceiver)) {
        if ((MR::isSensorPlayer(pSender) && !canAttack()) || MR::isSensorEnemy(pSender)) {
            if (isPushMovable()) {
                MR::startBtp(this, "BlinkTwice");
                Vec3f sensorDirection;
                MR::calcSensorDirectionNormalize(&sensorDirection, pSender, pReceiver);
                mVelocity += sensorDirection * (MR::isSensorPlayer(pSender) ? ::sPushMarioAccel : ::sPushAccel);
                mScaleCtrl->startAndAddScaleVelocityY(MR::isSensorPlayer(pSender) ? ::sPushScaleYMarioSpeed : ::sPushScaleYSpeed);
                mNoPushStep = MR::isSensorPlayer(pSender) ? ::sNoPushMarioTime : ::sNoPushTime;
            }
            return true;
        }
    }

    return false;
}

bool TakoHei::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorEnemyAttack(pReceiver)) {
        return false;
    }

    if (MR::isMsgLockOnStarPieceShoot(msg)) {
        return true;
    }

    if (MR::isMsgStarPieceAttack(msg)) {
        return requestSwoon();
    }

    if (MR::isMsgInvincibleAttack(msg)) {
        return requestPunch(pReceiver, pSender);
    }

    if (MR::isMsgPlayerHipDrop(msg)) {
        return requestHipDropDown();
    }

    if (MR::isMsgPlayerTrample(msg)) {
        return requestFlatDown();
    }

    if (MR::isMsgPlayerHitAll(msg)) {
        return requestPunch(pReceiver, pSender);
    }

    return false;
}

bool TakoHei::receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorEnemyAttack(pReceiver)) {
        return false;
    }

    if (MR::isMsgToEnemyAttackTrample(msg)) {
        return requestFlatDown();
    }

    if (MR::isMsgToEnemyAttackBlow(msg)) {
        return requestPunch(pReceiver, pSender);
    }

    return false;
}

bool TakoHei::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgPlayerKick(msg) && isEnableKick() && requestPunch(pReceiver, pSender)) {
        return true;
    }

    return false;
}

bool TakoHei::requestPressDown() {
    if (!isDown()) {
        MR::clearHitSensors(this);
        MR::invalidateHitSensors(this);
        setNerve(GET_NERVE(TakoHei, TakoHeiNrvPressDown));
        mDeathItem = 1;
        return true;
    }

    return false;
}

bool TakoHei::requestHipDropDown() {
    if (!isDown()) {
        MR::clearHitSensors(this);
        MR::invalidateHitSensors(this);
        setNerve(GET_NERVE(TakoHei, TakoHeiNrvHipDropDown));
        mDeathItem = 1;
        return true;
    }

    return false;
}

bool TakoHei::requestFlatDown() {
    if (!isDown()) {
        MR::clearHitSensors(this);
        MR::invalidateHitSensors(this);
        setNerve(GET_NERVE(TakoHei, TakoHeiNrvFlatDown));
        mDeathItem = 1;
        return true;
    }

    return false;
}

bool TakoHei::requestSwoon() {
    if (!isDown()) {
        setNerve(GET_NERVE(TakoHei, TakoHeiNrvSwoon));
        return true;
    }

    return false;
}

bool TakoHei::requestPunch(HitSensor* pReceiver, HitSensor* pSender) {
    if (!isDown()) {
        MR::clearHitSensors(this);
        MR::invalidateHitSensors(this);
        MR::addVelocitySeparateHV(this, pSender, pReceiver, ::sPunchHorizonPower, ::sPunchVerticalPower);
        setNerve(GET_NERVE(TakoHei, TakoHeiNrvPunchDown));
        mDeathItem = 2;
        return true;
    }

    return false;
}

bool TakoHei::requestAttackSuccess(HitSensor* pSender, HitSensor* pReceiver) {
    if (canAttack()) {
        MR::addVelocityMoveToDirection(this, pSender->mPosition - pReceiver->mPosition, ::sAttackSuccessHitPower);
        setNerve(GET_NERVE(TakoHei, TakoHeiNrvAttackSuccess));
        return true;
    }

    return false;
}

bool TakoHei::tryActive() {
    if (MR::isNearPlayerAnyTime(this, ::sActiveDistance)) {
        setNerve(GET_NERVE(TakoHei, TakoHeiNrvWait));
        return true;
    }

    return false;
}

bool TakoHei::tryNonActive() {
    bool isNotNearPlayer = MR::isNearPlayerAnyTime(this, ::sActiveDistance) ? false : true;
    if (isNotNearPlayer && MR::isBindedGround(this)) {
        MR::zeroVelocity(this);
        MR::invalidateHitSensors(this);
        setNerve(GET_NERVE(TakoHei, TakoHeiNrvNonActive));
        return true;
    }

    return false;
}

bool TakoHei::tryWalk() {
    if (MR::isGreaterStep(this, ::sWaitTime)) {
        setNerve(GET_NERVE(TakoHei, TakoHeiNrvWalk));
        return true;
    }

    return false;
}

bool TakoHei::tryWalkEnd() {
    Vec3f scaleAdd;
    scaleAdd.killElement(mWalkTarget - mPosition, mGravity);

    if (MR::isGreaterStep(this, ::sWalkEndTime) || scaleAdd.squared() < ::sWalkTerritoryRadius) {
        setNerve(GET_NERVE(TakoHei, TakoHeiNrvWait));
        return true;
    }

    if (isFallNextMove()) {
        MR::zeroVelocity(this);
        setNerve(GET_NERVE(TakoHei, TakoHeiNrvWait));
        return true;
    }

    return false;
}

bool TakoHei::tryFindTurn() {
    if (isInSightMario()) {
        setNerve(GET_NERVE(TakoHei, TakoHeiNrvFindTurn));
        return true;
    }

    return false;
}

bool TakoHei::tryFind() {
    if (MR::isGreaterStep(this, ::sFindTurnTime)) {
        setNerve(GET_NERVE(TakoHei, TakoHeiNrvFind));
        return true;
    }

    return false;
}

bool TakoHei::tryPursue() {
    if (MR::isGreaterStep(this, ::sFindTime)) {
        setNerve(GET_NERVE(TakoHei, TakoHeiNrvPursue));
        return true;
    }

    return false;
}

bool TakoHei::tryPursueEnd() {
    if (MR::isGreaterStep(this, ::sPursueTime)) {
        setNerve(GET_NERVE(TakoHei, TakoHeiNrvCoolDown));
        return true;
    }

    if (isFallNextMove()) {
        MR::zeroVelocity(this);
        setNerve(GET_NERVE(TakoHei, TakoHeiNrvWait));
        return true;
    }

    return false;
}

bool TakoHei::tryAttack() {
    f32 distanceToPlayer = MR::calcDistanceToPlayer(this);
    if (::sAttackDistanceNear <= distanceToPlayer && (distanceToPlayer <= ::sAttackDistanceFar || MR::isGreaterStep(this, ::sForceFarAttackTime))) {
        setNerve(GET_NERVE(TakoHei, TakoHeiNrvAttackSign));
        return true;
    }

    return false;
}

bool TakoHei::tryCoolDownEnd() {
    if (MR::isGreaterStep(this, ::sCoolDownTime)) {
        if (isInSightMario()) {
            setNerve(GET_NERVE(TakoHei, TakoHeiNrvPursue));
        } else {
            setNerve((GET_NERVE(TakoHei, TakoHeiNrvWait)));
        }
        return true;
    }

    return false;
}

bool TakoHei::tryAttackSuccessEnd() {
    if (MR::isGreaterStep(this, ::sAttackSuccessTime)) {
        setNerve((GET_NERVE(TakoHei, TakoHeiNrvWait)));
        return true;
    }

    return false;
}

bool TakoHei::tryPointBind() {
    if (mBindStarPointer->tryStartPointBind()) {
        setNerve((GET_NERVE(TakoHei, TakoHeiNrvBindStarPointer)));
        return true;
    }

    return false;
}

bool TakoHei::tryRecover() {
    if (MR::isGreaterStep(this, ::sSwoonTime)) {
        setNerve((GET_NERVE(TakoHei, TakoHeiNrvRecover)));
        return true;
    }

    return false;
}

bool TakoHei::tryRecoverEnd() {
    if (MR::isGreaterStep(this, ::sRecoverTime)) {
        setNerve((GET_NERVE(TakoHei, TakoHeiNrvWait)));
        return true;
    }

    return false;
}

bool TakoHei::tryPressed() {
    if (MR::isPressedRoofAndGround(this) && requestPressDown()) {
        MR::zeroVelocity(this);
        MR::offBind(this);
        return true;
    }

    return false;
}

void TakoHei::exeNonActive() {
    if (MR::isFirstStep(this)) {
        MR::startBtp(this, "Open");
        updatePose();
        MR::offBind(this);
        MR::offCalcShadow(this, nullptr);
        MR::offCalcGravity(this);
    }

    MR::zeroVelocity(this);

    if (tryActive() || tryPointBind()) {
        MR::onBind(this);
        MR::onCalcShadow(this, nullptr);
        MR::onCalcGravity(this);
        MR::validateHitSensors(this);
    }
}

void TakoHei::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
        MR::startBtp(this, "Open");
    }

    if (mNoPushStep == 1) {
        MR::startBtp(this, "Open");
    }

    if (getNerveStep() % NebulaFrameClock::stepsOfSeconds(1) == NebulaFrameClock::stepsOfSeconds(1) - 1) {
        MR::startBtp(this, "Blink");
    }

    updateNormalVelocity();

    // The first transition that takes wins.
    if (!tryPointBind() && !tryFindTurn() && !tryWalk()) {
        tryNonActive();
    }
}

void TakoHei::exeWalk() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Walk");
        MR::startBtp(this, "Open");
        decideNextTargetPos();
    }

    if (mNoPushStep == 1) {
        MR::startBtp(this, "Open");
    }

    if (getNerveStep() % NebulaFrameClock::stepsOfSeconds(1) == NebulaFrameClock::stepsOfSeconds(1) - 1) {
        MR::startBtp(this, "Blink");
    }

    MR::turnDirectionToTargetDegree(this, &mFront, mWalkTarget, ::sWalkTurnDegree);

    if (MR::isGreaterStep(this, ::sWalkMoveStartTime)) {
        MR::addVelocityMoveToTarget(this, mWalkTarget, ::sWalkAccel);
    }

    updateNormalVelocity();

    if (tryPointBind() || tryFindTurn() || tryWalkEnd()) {
        return;
    }
}

void TakoHei::exeFindTurn() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Turn");
        MR::startBtp(this, "Open");
        MR::startSound(this, "SE_EM_TAKOHEI_FIND");
    }

    if (mNoPushStep == 1) {
        MR::startBtp(this, "Open");
    }

    MR::turnDirectionToTargetDegree(this, &mFront, *MR::getPlayerPos(), ::sFindTurnDegree);

    updateNormalVelocity();

    if (tryPointBind() || tryFind()) {
        return;
    }
}

void TakoHei::exeFind() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Search");
        MR::startBtp(this, "Search");
    }

    updateNormalVelocity();

    if (tryPointBind() || tryPursue()) {
        return;
    }
}

void TakoHei::exePursue() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Run");
        MR::startBtp(this, "Blink");
    }

    if (mNoPushStep == 1) {
        MR::startBtp(this, "Blink");
    }

    bool isFarPlayer = false;

    if (MR::isNearPlayer(this, cNum_exePursue_isNearPlayer_1)) {
        MR::addVelocityAwayFromTarget(this, *MR::getPlayerPos(), ::sPursueAccel);
    } else {
        MR::addVelocityMoveToTarget(this, *MR::getPlayerPos(), ::sPursueAccel);
        isFarPlayer = true;
    }

    MR::turnDirectionToTargetDegree(this, &mFront, *MR::getPlayerPos(), ::sPursueTurnDegree);

    updateNormalVelocity();

    if (isFarPlayer) {
        if (checkFront()) {
            setNerve(GET_NERVE(TakoHei, TakoHeiNrvAttackSign));
            return;
        }
    }

    if (tryAttack() || tryPointBind() || tryPursueEnd()) {
        return;
    }
}

void TakoHei::exeCoolDown() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "CoolDown");
        MR::startBtp(this, "Cry");
    }

    if (mNoPushStep == 1) {
        MR::startBtp(this, "Cry");
    }

    MR::turnDirectionToTargetDegree(this, &mFront, *MR::getPlayerPos(), 0.0f);

    updateNormalVelocity();

    if (tryPointBind() || tryCoolDownEnd()) {
        return;
    }
}

void TakoHei::exeAttackSign() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "ShotStart");
        MR::startBtp(this, "Angry");
    }

    MR::startLevelSound(this, "SE_EM_TAKOHEI_LV_PRE_SPIT_OUT");

    MR::turnDirectionToTargetDegree(this, &mFront, *MR::getPlayerPos(), ::sAttackSignDegree);

    if (checkFront()) {
        MR::addVelocity(this, mFront * ::sBackAttackSpaceAccel);
    }

    updateNormalVelocity();

    if (tryPointBind()) {
        return;
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(TakoHei, TakoHeiNrvAttack));
    }
}

void TakoHei::exeAttack() {
    if (MR::isFirstStep(this)) {
        Pos3f mouthMtx = MR::getJointMtx(this, "Mouth");
        Vec3f mult;
        mouthMtx.mult(::sInkShotOffset, mult);
        MR::spurtTakoHeiInk(mult, mFront * ::sSpurtPowerH + mGravity * ::sSpurtPowerV);

        MR::startBck(this, "Shot");
        MR::startSound(this, "SE_EM_TAKOHEI_SPIT_OUT");
    }

    updateNormalVelocity();

    if (tryPointBind()) {
        return;
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(TakoHei, TakoHeiNrvWait));
    }
}

void TakoHei::exeAttackSuccess() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Attack");
        MR::startBtp(this, "Angry");
    }

    updateNormalVelocity();

    if (tryPointBind() || tryAttackSuccessEnd()) {
        return;
    }
}

void TakoHei::exeBindStarPointer() {
    MR::updateActorStateAndNextNerve(this, mBindStarPointer, GET_NERVE(TakoHei, TakoHeiNrvWait));
}

void TakoHei::endBindStarPointer() {
    mBindStarPointer->kill();
}

void TakoHei::exeSwoon() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "StunStart");
        MR::startBtp(this, "Cry");
    }

    if (MR::isBckOneTimeAndStopped(this)) {
        MR::startBck(this, "Stun");
    }

    if (MR::isBckPlaying(this, "Stun")) {
        MR::startLevelSound(this, "SE_EM_LV_SWOON_S");
    }

    updateSwoonVelocity();

    if (tryRecover()) {
        return;
    }
}

void TakoHei::exeRecover() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "StunEnd");
        MR::startBtp(this, "BlinkTwice");
        MR::startSound(this, "SE_EV_TAKOHEI_RECOVER");
    }

    updateNormalVelocity();

    if (tryRecoverEnd()) {
        return;
    }
}

void TakoHei::exePressDown() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "HipDropDown");
        MR::startBtp(this, "Cry");
        MR::startSound(this, "SE_EM_TAKOHEI_STOMPED_L");
        MR::zeroVelocity(this);
    }

    if (MR::isGreaterStep(this, ::sPressDownTime)) {
        kill();
    }
}

void TakoHei::exeHipDropDown() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "HipDropDown");
        MR::startBtp(this, "Cry");
        MR::startSound(this, "SE_EM_TAKOHEI_STOMPED_L");
        MR::zeroVelocity(this);
    }

    if (MR::isGreaterStep(this, ::sHipDropDownTime)) {
        kill();
    }
}

void TakoHei::exeFlatDown() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "FlatDown");
        MR::startBtp(this, "Cry");
        MR::startSound(this, "SE_EM_TAKOHEI_STOMPED");
        MR::zeroVelocity(this);
    }

    if (MR::isGreaterStep(this, ::sFlatDownTime)) {
        kill();
    }
}

void TakoHei::exePunchDown() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "PunchDown");
        MR::startBtp(this, "Cry");
        MR::turnDirectionToTarget(this, &mFront, *MR::getPlayerPos(), -1.0f);
        MR::stopScene(::sPunchStopSceneTime);
        MR::startSound(this, "SE_EM_TAKOHEI_HITPUNCH");
        MR::startBlowHitSound(this);
    }

    updateNormalVelocity();

    if (MR::isGreaterStep(this, ::sPunchDownTime) || (MR::isBinded(this) && MR::isGreaterStep(this, cStep_exePunchDown_1))) {
        kill();
    }
}

void TakoHei::updatePose() {
    MR::blendQuatUpFront(&mPoseQuat, MR::isBindedGround(this) ? *MR::getGroundNormal(this) : -mGravity, mFront, cNum_updatePose_f1, cNum_updatePose_f2);
}

void TakoHei::updateNormalVelocity() {
    if (MR::isBindedGround(this)) {
        MR::attenuateVelocity(this, ::sNormalGroundFrec);
    } else {
        MR::addVelocityToGravity(this, ::sNormalGravityAccel);
        MR::attenuateVelocity(this, ::sNormalAirFrec);
    }

    MR::reboundVelocityFromCollision(this, cNum_updateNormalVelocity_reboundVelocityFromCollision_1, cNum_updateNormalVelocity_reboundVelocityFromCollision_2, cNum_updateNormalVelocity_reboundVelocityFromCollision_3);
}

void TakoHei::updateSwoonVelocity() {
    if (MR::isBindedGround(this)) {
        MR::attenuateVelocity(this, ::sSwoonGroundFrec);
    } else {
        MR::addVelocityToGravity(this, ::sSwoonGravityAccel);
        MR::attenuateVelocity(this, ::sNormalAirFrec);
    }

    MR::reboundVelocityFromCollision(this, cNum_updateSwoonVelocity_reboundVelocityFromCollision_1, cNum_updateSwoonVelocity_reboundVelocityFromCollision_2, cNum_updateSwoonVelocity_reboundVelocityFromCollision_3);
}

void TakoHei::decideNextTargetPos() {
    MR::getRandomVector(&mWalkTarget, ::sWalkGoalDistance);

    mWalkTarget.orthogonalize(mGravity);

    mWalkTarget += mInitPos;
}

bool TakoHei::checkFront() const {
    Vec3f positionUpOffset;
    MR::calcPositionUpOffset(&positionUpOffset, this, ::sCheckFrontHeight);
    return MR::isExistMapCollision(positionUpOffset, mFront * ::sCheckFrontAttackSpace);
}

bool TakoHei::canAttack() const {
    if (isNerve(GET_NERVE(TakoHei, TakoHeiNrvWait)) || isNerve(GET_NERVE(TakoHei, TakoHeiNrvWalk)) ||
        isNerve(GET_NERVE(TakoHei, TakoHeiNrvFindTurn)) || isNerve(GET_NERVE(TakoHei, TakoHeiNrvFind)) ||
        isNerve(GET_NERVE(TakoHei, TakoHeiNrvPursue)) || isNerve(GET_NERVE(TakoHei, TakoHeiNrvCoolDown))) {
        return true;
    } else {
        return false;
    }
}

bool TakoHei::isPushMovable() const {
    if (mNoPushStep > 0) {
        return false;
    }

    if (isDown()) {
        return false;
    }

    if (isNerve(GET_NERVE(TakoHei, TakoHeiNrvNonActive)) || isNerve(GET_NERVE(TakoHei, TakoHeiNrvAttackSuccess)) ||
        isNerve(GET_NERVE(TakoHei, TakoHeiNrvBindStarPointer)) || isNerve(GET_NERVE(TakoHei, TakoHeiNrvSwoon))) {
        return false;
    } else {
        return true;
    }
}

bool TakoHei::isEnableKick() const {
    if (isNerve(GET_NERVE(TakoHei, TakoHeiNrvSwoon))) {
        return true;
    } else {
        return false;
    }
}

bool TakoHei::isDown() const {
    if (isNerve(GET_NERVE(TakoHei, TakoHeiNrvHipDropDown)) || isNerve(GET_NERVE(TakoHei, TakoHeiNrvFlatDown)) ||
        isNerve(GET_NERVE(TakoHei, TakoHeiNrvPressDown)) || isNerve(GET_NERVE(TakoHei, TakoHeiNrvPunchDown))) {
        return true;
    } else {
        return false;
    }
}

bool TakoHei::isFallNextMove() const {
    if (mHasMoveLimit) {
        return false;
    }

    if (MR::isOnGround(this)) {
        return MR::isFallNextMove(mPosition, mVelocity, mGravity, cNum_isFallNextMove_f1, mScale.x * cNum_isFallNextMove_f2 * cNum_isFallNextMove_f3, cNum_isFallNextMove_f4, nullptr);
    } else {
        return false;
    }
}

bool TakoHei::isInSightMario() const {
    Vec3f toPlayer(*MR::getPlayerPos() - mPosition);
    f32 scalarToPlayer;
    MR::separateScalarAndDirection(&scalarToPlayer, &toPlayer, toPlayer);

    Vec3f quatZDir;

    mPoseQuat.getZDir(quatZDir);

    if (scalarToPlayer < ::sFindDistance && toPlayer.dot(quatZDir) > 0.0f) {
        return true;
    } else {
        return false;
    }
}
