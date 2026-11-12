#include "Game/Enemy/BasaBasa.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/Enemy/AnimStampController.hpp"
#include "Game/Enemy/SpinHitController.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <nebula_math.h>
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 cFlyAccel = 2.0f;
    const f32 cTurnDegree = 2.55f;
    const f32 cChaseTurnDegree = 1.7f;
    const f32 cChaseFaceDegree = 60.0f;
    const f32 cHomeDistanceMargin = 1000.0f;  // over the chase distance when Obj_arg1 is unset
    const f32 cHomeArriveDistance = 300.0f;
    const s32 cComeHomeChaseSteps = NebulaFrameClock::stepsOfSeconds(2);
    const f32 cAttachDistance = 10.0f;
    const f32 cAttachSpeedRate = 5.0f;
    const f32 cAttackRise = 100.0f;  // above the player's feet
    const f32 cAttackSpeed = 10.0f;
    const f32 cAttackHitDistance = 40.0f;
    const s32 cAttackSteps = NebulaFrameClock::stepsOfSeconds(3);
    const f32 cIceAttackUpRate = 5.0f;
    const f32 cAttackEndEffectHeight = 200.0f;
    const f32 cHitBackSpeed = 15.0f;
    const f32 cPushSpeed = 1.5f;
    const f32 cSpinStormDistance = 500.0f;
    const f32 cClipNearPlayerDistance = 2500.0f;
    const s32 cClipResetSteps = NebulaFrameClock::stepsOfSeconds(3);
    const f32 cSideDamping = 0.5f;
    const f32 cHoverBelowAttack = 350.0f;  // hover height is the attack distance minus this
    const f32 cTuneMinOffset = 2.0f;

    inline bool isPointerHolding(const LiveActor* pActor) {
        return MR::isStarPointerPointing2POnPressButton(pActor, "弱", true, false);
    }
};  // namespace

namespace NrvBasaBasa {
    NEW_NERVE(BasaBasaNrvWait, BasaBasa, Wait);
    NEW_NERVE(BasaBasaNrvAirWait, BasaBasa, AirWait);
    NEW_NERVE(BasaBasaNrvChaseStart, BasaBasa, ChaseStart);
    NEW_NERVE(BasaBasaNrvChase, BasaBasa, Chase);
    NEW_NERVE(BasaBasaNrvQuickTurnStart, BasaBasa, QuickTurnStart);
    NEW_NERVE(BasaBasaNrvQuickTurn, BasaBasa, QuickTurn);
    NEW_NERVE(BasaBasaNrvPunchDown, BasaBasa, PunchDown);
    NEW_NERVE(BasaBasaNrvTrampleDown, BasaBasa, TrampleDown);
    NEW_NERVE(BasaBasaNrvAttackStart, BasaBasa, AttackStart);
    NEW_NERVE(BasaBasaNrvAttack, BasaBasa, Attack);
    NEW_NERVE(BasaBasaNrvAttackEnd, BasaBasa, AttackEnd);
    NEW_NERVE(BasaBasaNrvAttackEndRecover, BasaBasa, AttackEndRecover);
    NEW_NERVE(BasaBasaNrvHitBack, BasaBasa, HitBack);
    NEW_NERVE(BasaBasaNrvHitBackEnd, BasaBasa, HitBackEnd);
    NEW_NERVE(BasaBasaNrvComeHome, BasaBasa, ComeHome);
    NEW_NERVE(BasaBasaNrvAttachCelling, BasaBasa, AttachCelling);
    NEW_NERVE_ONEND(BasaBasaNrvPointerSwoon, BasaBasa, PointerSwoon, PointerSwoon);
    NEW_NERVE(BasaBasaNrvStun, BasaBasa, Stun);
};  // namespace NrvBasaBasa

BasaBasa::BasaBasa(const char* pName)
    : LiveActor(pName), mHangModel(nullptr), mScaleController(nullptr), mStampController(nullptr), mSpinHitController(nullptr), mFront(0.0f, 0.0f, 1.0f),
      mChaseDistance(2000.0f), mAttackDistance(0.0f), mTarget(nullptr), mChaseTurnAngle(0.0f), mAttackTarget(0.0f, 0.0f, 0.0f), mIsIceModel(false), mHomePos(0.0f, 0.0f, 0.0f), mHomeDistance(-1.0f), mClipStep(0), mIsOnCeiling(false) {
    lumacore::setClass(this, lumacore::kClassId< BasaBasa >);
}

void BasaBasa::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    const char* objName;
    MR::getObjectName(&objName, rIter);
    mIsIceModel = MR::isEqualString(objName, "BasaBasaIce");
    initModelManagerWithAnm(objName, nullptr, false);
    MR::connectToSceneEnemy(this);
    initHitSensor(2);
    MR::addHitSensorAtJointEnemy(this, "body", "JointRoot", 8, 90.0f, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorAtJointEnemyAttack(this, "attack", "JointRoot", 8, 60.0f, Vec3f(0.0f, 0.0f, 0.0f));
    initBinder(110.0f, 0.0f, 0);
    MR::initLightCtrl(this);
    initEffectKeeper(1, nullptr);
    MR::addEffectHitNormal(this, nullptr);
    initSound(4, false);
    MR::initShadowVolumeSphere(this, 100.0f);
    MR::validateClipping(this);
    if (MR::isConnectedWithRail(rIter)) {
        initRailRider(rIter);
        mTarget = &MR::getRailPos(this);
    } else {
        mTarget = MR::getPlayerCenterPos();
    }

    MR::declareStarPiece(this, 3);
    MR::declareCoin(this, 1);
    MR::onCalcGravity(this);
    MR::initStarPointerTarget(this, 100.0f, Vec3f(0.0f));
    mScaleController = new AnimScaleController(nullptr);
    mStampController = new AnimStampController();
    mSpinHitController = new SpinHitController(this, 20, 20, 5, 0.0f, 20.0f, 10.0f, 3, false);
    initHangModel();
    MR::getPlacementArg0NoInit(rIter, &mChaseDistance);
    MR::getPlacementArg1NoInit(rIter, &mHomeDistance);
    if (-1.0f == mHomeDistance) {
        mHomeDistance = cHomeDistanceMargin + mChaseDistance;
    }

    MR::calcFrontVec(&mFront, this);
    mAttackDistance = MR::getRandom(600.0f, 800.0f);
    mChaseTurnAngle = MR::getRandom(-25.0f, 25.0f);
    MR::useStageSwitchSleep(this, rIter);
    makeActorAppeared();
}

void BasaBasa::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
        mHangModel->appear();
        mTarget = MR::getPlayerCenterPos();
        MR::validateClipping(this);
        MR::validateHitSensors(this);
        MR::offBind(this);
    }

    if (isNearTarget(mChaseDistance)) {
        MR::invalidateClipping(this);
        setNerve(GET_NERVE(BasaBasa, BasaBasaNrvChaseStart));
    }
}

void BasaBasa::exeAirWait() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Fly");
        if (MR::isExistRail(this)) {
            mTarget = &MR::getRailPos(this);
        } else {
            mTarget = MR::getPlayerCenterPos();
        }

        MR::validateClipping(this);
        MR::validateHitSensors(this);
    }

    tuneHeight();
    if (isNearTarget(mChaseDistance)) {
        MR::invalidateClipping(this);
        setNerve(GET_NERVE(BasaBasa, BasaBasaNrvChase));
    } else if (!tryComeHome()) {
        trySetNervePointerSwoon();
    }
}

void BasaBasa::exeChaseStart() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "FlyStart");
        MR::startSound(this, "SE_EV_BASABASA_FIND");
    }

    if (MR::isLessStep(this, 50)) {
        mVelocity.scaleAdd(15.0f, mGravity, mVelocity);
    }

    if (MR::isStep(this, 15)) {
        MR::onBind(this);
    }

    if (MR::isStep(this, 34)) {
        mHangModel->kill();
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(BasaBasa, BasaBasaNrvChase));
    }
}

void BasaBasa::exeChase() {
    if (MR::isFirstStep(this)) {
        MR::tryStartBck(this, "Fly");
        MR::onBind(this);
    }

    Vec3f toTarget;
    toTarget.sub(*mTarget, mPosition);
    toTarget.orthogonalize(mGravity);
    MR::normalizeOrZero(&toTarget);
    if (!MR::isNearZero(toTarget)) {
        Vec3f turnDir(toTarget);
        MR::rotateVecDegree(&turnDir, mGravity, mChaseTurnAngle);
        MR::turnDirectionDegree(this, &mFront, turnDir, cChaseTurnDegree);
    }

    mVelocity.scaleAdd(cFlyAccel, mFront, mVelocity);
    tuneHeight();
    if (!trySetNervePointerSwoon()) {
        if (MR::isNearPlayer(mHomePos, mHomeDistance) && MR::isNearPlayer(this, mAttackDistance)) {
            setNerve(GET_NERVE(BasaBasa, BasaBasaNrvAttackStart));
        } else if (!MR::isFaceToTargetHorizontalDegree(this, *mTarget, mFront, cChaseFaceDegree)) {
            setNerve(GET_NERVE(BasaBasa, BasaBasaNrvQuickTurnStart));
        } else {
            tryComeHome();
        }
    }
}

void BasaBasa::exeQuickTurnStart() {
    mVelocity.scaleAdd(cFlyAccel, mFront, mVelocity);
    if (MR::isStep(this, 45)) {
        setNerve(GET_NERVE(BasaBasa, BasaBasaNrvQuickTurn));
    } else {
        trySetNervePointerSwoon();
    }
}

void BasaBasa::exeQuickTurn() {
    mVelocity.scaleAdd(cFlyAccel, mFront, mVelocity);
    MR::turnDirectionToTargetDegree(this, &mFront, *mTarget, cTurnDegree);
    if (MR::isFaceToTargetHorizontalDegree(this, *mTarget, mFront, cTurnDegree)) {
        setNerve(GET_NERVE(BasaBasa, BasaBasaNrvChase));
    } else {
        trySetNervePointerSwoon();
    }
}

void BasaBasa::exeTrampleDown() {
    if (MR::isFirstStep(this)) {
        MR::stopBck(this);
        mVelocity.zero();
        mStampController->startAnim(mGravity);
        MR::invalidateHitSensors(this);
        MR::startSound(this, "SE_EV_BASABASA_DAMAGE");
        MR::startSound(this, "SE_EM_STOMPED_S");
    }

    mStampController->updateNerve();

    if (mStampController->isAnimEnd()) {
        MR::appearCoinPop(this, mPosition, 1);
        kill();
    }
}

void BasaBasa::exePunchDown() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Blow");
        MR::invalidateClipping(this);
        MR::invalidateHitSensors(this);
        MR::startSound(this, "SE_EV_BASABASA_DAMAGE");
        MR::startBlowHitSound(this);
        MR::onBind(this);
    }

    mSpinHitController->execute(this);
}

void BasaBasa::exeAttackStart() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "AttackStart");
    }

    MR::turnDirectionToTargetDegree(this, &mFront, *mTarget, cTurnDegree);
    Vec3f backUp;
    backUp.scaleAdd(2.0f, mFront, mGravity);
    backUp.negate();
    MR::normalizeOrZero(&backUp);
    if (MR::isNearZero(backUp)) {
        backUp.set(-mGravity);
    }

    mVelocity.scaleAdd(0.2f, backUp, mVelocity);
    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(BasaBasa, BasaBasaNrvAttack));
    } else {
        trySetNervePointerSwoon();
    }
}

void BasaBasa::exeAttack() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Attack");
        MR::startSound(this, "SE_EV_BASABASA_ATTACK");
        Vec3f playerUp;
        MR::getPlayerUpVec(&playerUp);
        mAttackTarget.scaleAdd(cAttackRise, playerUp, *MR::getPlayerPos());
    }

    MR::startLevelSound(this, "SE_EM_LV_BASABASA_ATTACK");
    Vec3f toTarget;
    toTarget.sub(mAttackTarget, mPosition);
    if (mIsIceModel) {
        Vec3f up;
        MR::calcUpVec(&up, this);
        Vec3f horizontal = toTarget.killElement(up);
        Vec3f vertical;
        vertical.scale(up.dot(toTarget), up);
        toTarget.scaleAdd(cIceAttackUpRate, vertical, horizontal);
    }
    Vec3f dir;
    MR::normalize(toTarget, &dir);
    MR::turnDirectionToTargetDegree(this, &mFront, mAttackTarget, cTurnDegree);
    mVelocity.scaleAdd(cAttackSpeed, dir, mVelocity);
    if (MR::isNear(this, mAttackTarget, cAttackHitDistance) || MR::isBindedWall(this) || MR::isStep(this, cAttackSteps)) {
        setNerve(GET_NERVE(BasaBasa, BasaBasaNrvAttackEnd));
    } else {
        trySetNervePointerSwoon();
    }
}

void BasaBasa::exeAttackEnd() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "AttackEnd");
        if (MR::getShadowProjectionLength(this, nullptr) < cAttackEndEffectHeight) {
            MR::emitEffect(this, "AttackEnd");
        }
    }

    mVelocity.scaleAdd(0.1f, mFront, mVelocity);
    if (MR::isStep(this, 50)) {
        setNerve(GET_NERVE(BasaBasa, BasaBasaNrvAttackEndRecover));
    } else {
        trySetNervePointerSwoon();
    }
}

void BasaBasa::exeAttackEndRecover() {
    if (MR::isFirstStep(this)) {
        mVelocity.zero();
    }

    Vec3f riseDir;
    riseDir.scaleAdd(-0.5f, mFront, mGravity);
    riseDir.negate();
    MR::normalize(&riseDir);
    if (MR::getBckFrame(this) >= 116.0f) {
        mVelocity.scaleAdd(0.3f, riseDir, mVelocity);
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(BasaBasa, BasaBasaNrvChase));
    }

    trySetNervePointerSwoon();
}

void BasaBasa::exeHitBack() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Reaction");
        MR::onBind(this);
        MR::turnDirectionToPlayerDegree(this, &mFront, 180.0f);
        MR::startSound(this, "SE_EM_ICEBASA_SPIN_BLOW");
    }

    if (MR::isLessEqualStep(this, 5)) {
        Vec3f backUp;
        backUp.add(mFront, mGravity);
        backUp.negate();
        MR::normalizeOrZero(&backUp);
        if (MR::isNearZero(backUp)) {
            backUp.set(-mGravity);
        }
        mVelocity.scaleAdd(cHitBackSpeed, backUp, mVelocity);
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(BasaBasa, BasaBasaNrvHitBackEnd));
    } else {
        trySetNervePointerSwoon();
    }
}

void BasaBasa::exeHitBackEnd() {
    if (MR::isFirstStep(this)) {
        MR::tryStartBck(this, "Fly");
    }

    if (MR::isStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
        setNerve(GET_NERVE(BasaBasa, BasaBasaNrvChase));
    } else {
        trySetNervePointerSwoon();
    }
}

void BasaBasa::exeComeHome() {
    if (MR::isFirstStep(this)) {
        MR::tryStartBck(this, "Fly");
        mTarget = &mHomePos;
    }

    MR::turnDirectionToTargetDegree(this, &mFront, *mTarget, cTurnDegree);
    mVelocity.scaleAdd(cFlyAccel, mFront, mVelocity);
    tuneHeight();
    if (isNearTarget(cHomeArriveDistance)) {
        if (mIsOnCeiling) {
            setNerve(GET_NERVE(BasaBasa, BasaBasaNrvAttachCelling));
        } else {
            setNerve(GET_NERVE(BasaBasa, BasaBasaNrvAirWait));
        }
    } else if (MR::isGreaterEqualStep(this, cComeHomeChaseSteps) && MR::isNearPlayer(this, mChaseDistance) && MR::isNearPlayer(mHomePos, mHomeDistance)) {
        mTarget = MR::getPlayerCenterPos();
        setNerve(GET_NERVE(BasaBasa, BasaBasaNrvChase));
    } else {
        trySetNervePointerSwoon();
    }
}

void BasaBasa::exeAttachCelling() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Return");
        MR::offBind(this);
        MR::invalidateHitSensors(this);
    }

    MR::turnDirectionDegree(this, &mFront, mHomeFront, cTurnDegree);
    if (MR::isNear(this, mHomePos, cAttachDistance)) {
        mVelocity.zero();
        MR::resetPosition(this, mHomePos);
    } else {
        Vec3f toHome;
        toHome.sub(mHomePos, mPosition);
        mVelocity.scale(cAttachSpeedRate, toHome);
    }

    if (MR::isStep(this, 42)) {
        mHangModel->appear();
    }

    if (MR::isBckStopped(this)) {
        mVelocity.zero();
        MR::validateHitSensors(this);
        setNerve(GET_NERVE(BasaBasa, BasaBasaNrvWait));
    }
}

void BasaBasa::exePointerSwoon() {
    if (MR::isFirstStep(this)) {
        MR::stopBck(this);
        MR::emitEffect(this, "Touch");
        mScaleController->startPointerHitVibration();
        MR::startPointerHitSound();
    }

    MR::startPointerFreezeLevelSound(this);

    if (!isPointerHolding(this)) {
        setNerve(GET_NERVE(BasaBasa, BasaBasaNrvAirWait));
    }
}

void BasaBasa::endPointerSwoon() {
    MR::deleteEffect(this, "Touch");
    mScaleController->stopAndReset();
}

void BasaBasa::exeStun() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Swoon");
        MR::startSound(this, "SE_EV_BASABASA_DAMAGE");
    }

    mVelocity.scaleAdd(1.0f, mGravity, mVelocity);
    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(BasaBasa, BasaBasaNrvChase));
    }
}

void BasaBasa::initAfterPlacement() {
    MR::trySetMoveLimitCollision(this);
    Vec3f lineStart;
    lineStart.scaleAdd(20.0f, mGravity, mPosition);
    Vec3f lineDir;
    lineDir.scale(-30.0f, mGravity);
    mIsOnCeiling = MR::getFirstPolyOnLineToMap(&mPosition, nullptr, lineStart, lineDir);
    if (mIsOnCeiling) {
        initNerve(GET_NERVE(BasaBasa, BasaBasaNrvWait));
    } else {
        initNerve(GET_NERVE(BasaBasa, BasaBasaNrvAirWait));
    }

    mHomePos.set(mPosition);
    MR::calcFrontVec(&mHomeFront, this);
}

void BasaBasa::kill() {
    MR::emitEffect(this, "Death");
    MR::startSound(this, "SE_EM_EXPLODE_S");
    mHangModel->kill();
    LiveActor::kill();
}

void BasaBasa::control() {
    MR::startLevelSound(this, "SE_EM_LV_ICEBASA_CHILL");
    mScaleController->updateNerve();
    if (!tryClippingAndResetPos()) {
        controlVelocity();
        updateRailType();
    }
}

void BasaBasa::calcAndSetBaseMtx() {
    Pos3f baseMtx;
    MR::calcMtxFromGravityAndZAxis(&baseMtx, this, mGravity, mFront);
    baseMtx.concat(mStampController->mMtx, baseMtx);
    baseMtx.setTrans(mPosition);
    MR::setBaseTRMtx(this, baseMtx);
    MR::setBaseScale(this, mScaleController->mScale);
}

void BasaBasa::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (isDown() || isNerve(GET_NERVE(BasaBasa, BasaBasaNrvHitBack))) {
        return;
    }

    if (MR::isSensorEnemyAttack(pSender) && MR::isSensorPlayer(pReceiver)) {
        bool isHit = false;
        if (!MR::isPlayerHipDropFalling()) {
            if (mIsIceModel) {
                isHit = MR::sendMsgEnemyAttackFreeze(pReceiver, pSender);
            } else if (!isNerve(GET_NERVE(BasaBasa, BasaBasaNrvPointerSwoon))) {
                if (MR::isPlayerSwimming()) {
                    isHit = MR::sendMsgEnemyAttackStrong(pReceiver, pSender);
                } else {
                    isHit = MR::sendMsgEnemyAttack(pReceiver, pSender);
                }
            }
        }

        if (isHit) {
            MR::emitEffectHitBetweenSensors(this, pSender, pReceiver, 0.0f, nullptr);
            setNerve(GET_NERVE(BasaBasa, BasaBasaNrvHitBack));
        } else {
            MR::sendMsgPush(pReceiver, pSender);
        }
    } else if (MR::isSensorEnemy(pSender) && MR::isSensorEnemy(pReceiver)) {
        MR::sendMsgPush(pReceiver, pSender);
        Vec3f away;
        away.sub(mPosition, pReceiver->mHost->mPosition);
        MR::normalizeOrZero(&away);
        if (mVelocity.dot(away) < 0.0f) {
            mVelocity.scaleAdd(-away.dot(mVelocity), away, mVelocity);
        }
    }
}

bool BasaBasa::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::isSensorEnemy(pReceiver)) {
        return false;
    }

    if (MR::isMsgStarPieceAttack(msg)) {
        if (isNrvEnableStun()) {
            setNerve(GET_NERVE(BasaBasa, BasaBasaNrvStun));
        }

        return true;
    }

    if (MR::isMsgLockOnStarPieceShoot(msg)) {
        return true;
    }

    if (isDown()) {
        return false;
    }

    if (mIsIceModel && !MR::isPlayerElementModeIce()) {
        if (MR::isMsgPlayerSpinAttack(msg)) {
            return false;
        } else if (MR::isMsgPlayerTrample(msg) || MR::isMsgPlayerHipDrop(msg)) {
            MR::sendMsgEnemyAttackFreeze(pSender, pReceiver);
            return true;
        }
    }

    if (MR::isMsgPlayerTrample(msg) || MR::isMsgPlayerHipDrop(msg)) {
        MR::tryRumbleDefaultHit(this, 0);
        setNerve(GET_NERVE(BasaBasa, BasaBasaNrvTrampleDown));
        return true;
    }

    if (MR::isMsgPlayerHitAll(msg)) {
        mSpinHitController->start(this, pSender->mPosition, pReceiver->mPosition);
        setNerve(GET_NERVE(BasaBasa, BasaBasaNrvPunchDown));
        return true;
    }

    return false;
}

bool BasaBasa::receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(BasaBasa, BasaBasaNrvPointerSwoon))) {
        return false;
    }

    if (!MR::isSensorEnemy(pReceiver)) {
        return false;
    }

    if (MR::isSensorEnemy(pSender) || MR::isSensorMapObj(pSender)) {
        Vec3f away;
        away.sub(mPosition, pSender->mHost->mPosition);
        MR::normalizeOrZero(&away);
        mVelocity.scaleAdd(cPushSpeed, away, mVelocity);
        return true;
    }

    return false;
}

bool BasaBasa::receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::isSensorEnemy(pReceiver)) {
        return false;
    }

    if (MR::isMsgToEnemyAttackBlow(msg) || MR::isMsgToEnemyAttackTrample(msg)) {
        mSpinHitController->startWithoutStopScene(this, pSender->mPosition, pReceiver->mPosition);
        setNerve(GET_NERVE(BasaBasa, BasaBasaNrvPunchDown));
        return true;
    }

    return false;
}

// Knocked back by a near spin storm; the message is never taken.
bool BasaBasa::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (!mIsIceModel || !MR::isSensorEnemy(pReceiver)) {
        return false;
    }

    if (MR::isMsgSpinStormRange(msg) && MR::isPlayerElementModeNormal() && !(cSpinStormDistance < MR::calcDistance(pSender, pReceiver, nullptr))) {
        setNerve(GET_NERVE(BasaBasa, BasaBasaNrvHitBack));
    }

    return false;
}

void BasaBasa::initHangModel() {
    const char* modelName = mIsIceModel ? "BasaBasaIceHang" : "BasaBasaHang";
    mHangModel = MR::createModelObjEnemy("ぶら下がりモデル", modelName, MR::getJointMtx(this, "JointRoot"));
    mHangModel->initWithoutIter();
    mHangModel->kill();
}

// Sent home after it has stayed off screen and away from the player for cClipResetSteps.
bool BasaBasa::tryClippingAndResetPos() {
    if (isNerve(GET_NERVE(BasaBasa, BasaBasaNrvAttachCelling)) || isNerve(GET_NERVE(BasaBasa, BasaBasaNrvChaseStart))) {
        return false;
    }

    f32 radius = getSensor("body")->mRadius;
    if (!MR::isJudgedToClipFrustum(mPosition, radius) || MR::isNearPlayer(this, cClipNearPlayerDistance)) {
        mClipStep = 0;
        return false;
    }

    if (mClipStep < cClipResetSteps) {
        mClipStep++;
        return false;
    }

    MR::resetPosition(this, mHomePos);
    mFront.set(mHomeFront);
    mVelocity.zero();
    mClipStep = 0;
    if (mIsOnCeiling) {
        setNerve(GET_NERVE(BasaBasa, BasaBasaNrvWait));
    } else {
        setNerve(GET_NERVE(BasaBasa, BasaBasaNrvAirWait));
    }

    return true;
}

bool BasaBasa::trySetNervePointerSwoon() {
    if (!isPointerHolding(this)) {
        return false;
    }

    setNerve(GET_NERVE(BasaBasa, BasaBasaNrvPointerSwoon));
    return true;
}

bool BasaBasa::tryComeHome() {
    if (MR::isNear(this, mHomePos, mHomeDistance) && isNearTarget(mChaseDistance)) {
        return false;
    }

    setNerve(GET_NERVE(BasaBasa, BasaBasaNrvComeHome));
    return true;
}

void BasaBasa::updateRailType() {
    if (MR::isExistRail(this) && mTarget == &MR::getRailPos(this)) {
        MR::moveCoord(this, 1.0f);
        if (MR::isNearPlayer(mHomePos, mChaseDistance)) {
            mTarget = MR::getPlayerPos();
        }
    }
}

void BasaBasa::controlVelocity() {
    if (isDown()) {
        return;
    }

    Vec3f side;
    MR::calcSideVec(&side, this);
    f32 sideSpeed = side.dot(mVelocity);
    Vec3f sideVel;
    sideVel.scale(sideSpeed, side);
    sideVel.scale(cSideDamping);
    mVelocity.scaleAdd(-side.dot(mVelocity), side, mVelocity);
    mVelocity.add(sideVel);

    f32 damping = 0.95f;
    if (isNerve(GET_NERVE(BasaBasa, BasaBasaNrvAttackEnd))) {
        damping = 0.96f;
    } else if (isNerve(GET_NERVE(BasaBasa, BasaBasaNrvChaseStart))) {
        damping = 0.45f;
    }

    mVelocity.scale(damping);

    f32 maxSpeed = 8.0f;
    if (isNerve(GET_NERVE(BasaBasa, BasaBasaNrvChaseStart)) || isNerve(GET_NERVE(BasaBasa, BasaBasaNrvHitBack))) {
        maxSpeed = 15.0f;
    } else if (isNerve(GET_NERVE(BasaBasa, BasaBasaNrvAttack))) {
        maxSpeed = 23.0f;
    }

    if (mVelocity.length() > maxSpeed) {
        mVelocity.setLength(maxSpeed);
    } else if (MR::isNearZero(mVelocity)) {
        mVelocity.zero();
    }
}

// Near the target it matches the target's height; otherwise it hovers mAttackDistance - cHoverBelowAttack over the ground.
void BasaBasa::tuneHeight() {
    if (!MR::isShadowProjected(this, nullptr)) {
        return;
    }

    Vec3f offset;
    if (isNearTarget(mAttackDistance)) {
        offset.sub(*mTarget, mPosition);
        offset.scale(mGravity.dot(offset), mGravity);
    } else {
        offset.scale(MR::getShadowProjectionLength(this, nullptr) - (mAttackDistance - cHoverBelowAttack), mGravity);
    }

    if (offset.length() < cTuneMinOffset) {
        return;
    }

    Vec3f dir(offset);
    MR::normalize(&dir);
    mVelocity.scaleAdd(cFlyAccel, dir, mVelocity);
}

bool BasaBasa::isNearTarget(f32 distance) const {
    if (mTarget == MR::getPlayerCenterPos()) {
        return MR::isNearPlayer(this, distance);
    } else {
        return MR::isNear(this, *mTarget, distance);
    }
}

bool BasaBasa::isDown() const {
    return isNerve(GET_NERVE(BasaBasa, BasaBasaNrvTrampleDown)) || isNerve(GET_NERVE(BasaBasa, BasaBasaNrvPunchDown));
}

bool BasaBasa::isNrvEnableStun() const {
    return !isDown() && !isNerve(GET_NERVE(BasaBasa, BasaBasaNrvWait)) && !isNerve(GET_NERVE(BasaBasa, BasaBasaNrvAttachCelling));
}
