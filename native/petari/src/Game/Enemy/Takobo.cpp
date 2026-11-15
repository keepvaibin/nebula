#include "Game/Enemy/Takobo.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/Enemy/SpinHitController.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/MapObj/SpinningBox.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/System/LumaCore.hpp"

namespace NrvTakobo {
    NEW_NERVE_ONEND(HostTypeNrvWait, Takobo, Wait, Wait);
    NEW_NERVE_ONEND(HostTypeNrvMove, Takobo, Move, Move);
    NEW_NERVE(HostTypeNrvPress, Takobo, Press);
    NEW_NERVE_ONEND(HostTypeNrvAttack, Takobo, Attack, Attack);
    NEW_NERVE(HostTypeNrvHitPunch, Takobo, HitPunch);
    NEW_NERVE(HostTypeNrvStunStart, Takobo, StunStart);
    NEW_NERVE(HostTypeNrvStun, Takobo, Stun);
    NEW_NERVE(HostTypeNrvStunEnd, Takobo, StunEnd);
    NEW_NERVE(HostTypeNrvHitReaction, Takobo, HitReaction);
    NEW_NERVE(HostTypeNrvIce, Takobo, Ice);
    NEW_NERVE_ONEND(HostTypeNrvPointerAimed, Takobo, PointerAimed, PointerAimed);
};  // namespace NrvTakobo

namespace {
    const f32 sAttackDistance = 250.0f;
    const f32 sTurnDistance = 800.0f;
    const f32 sTurnRadian = 0.05f;
    const f32 sAttackAimRadian = 0.1f;
    const f32 sFireBounceSpeed = 28.0f;
    const s32 sWaitSteps = NebulaFrameClock::stepsOfSeconds(1);
    const s32 sPressSteps = NebulaFrameClock::stepsOfMilliseconds(500);
    const s32 sStunSteps = NebulaFrameClock::stepsOfSeconds(2);
    const s32 sAttackHitStart = 85;
    const s32 sAttackHitEnd = 90;
};  // namespace

Takobo::Takobo(const char* pName)
    : LiveActor(pName), mFront(0, 0, 1), mIsCheckFireGround(false), mMoveDir(1, 0, 0), mIsMoveBack(false), mMoveSpeed(5.0f),
      mMoveRange(500.0f), mMoveStart(-500.0f), mMoveEnd(500.0f), mMoveStep(0), mBox(nullptr), mScaleController(nullptr),
      mSpinController(nullptr) {
    lumacore::setClass(this, lumacore::kClassId< Takobo >);
}

void Takobo::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    mInitPos.set(mPosition);

    s32 checkFireGround = -1;
    MR::getPlacementArg0NoInit(rIter, &checkFireGround);
    mIsCheckFireGround = checkFireGround > 0;

    MR::useStageSwitchWriteDead(this, rIter);
    initModelManagerWithAnm("Takobo", nullptr, false);
    MR::connectToSceneEnemy(this);

    Pos3f mtx;
    mtx.set(getBaseMtx());
    mtx.getZDir(mFront);

    Vec3f up;
    mtx.getYDir(up);
    mGravity.set(-up);

    // Arg 1: 0 along X, 1 along X starting back, 3 along Z starting back, anything else along Z.
    s32 moveType = -1;
    MR::getPlacementArg1NoInit(rIter, &moveType);

    if (moveType == 0 || moveType == 1) {
        mtx.getXDir(mMoveDir);
    } else {
        mtx.getZDir(mMoveDir);
    }

    mIsMoveBack = moveType == 1 || moveType == 3;

    s32 moveSpeed = -1;
    MR::getPlacementArg2NoInit(rIter, &moveSpeed);
    // Any placed speed counts, as the game had it: a placed 0 makes the sweep time below infinite, which is a
    // placement's way of saying the enemy stands where it is (the Gateway planet's). Taking only positive speeds
    // set those walking their default range, off their small platforms.
    if (moveSpeed != -1) {
        mMoveSpeed = moveSpeed;
    }

    s32 moveRange = -1;
    MR::getPlacementArg3NoInit(rIter, &moveRange);
    if (moveRange != -1) {
        mMoveRange = moveRange;
    }

    MR::declareStarPiece(this, 3);
    MR::declareCoin(this, 1);
    initSound(4, false);
    initBinder(80.0f * mScale.y, 60.0f * mScale.y, 0);
    initEffectKeeper(0, nullptr);
    initSensor();
    MR::initShadowVolumeSphere(this, 70.0f * mScale.y);
    MR::onCalcShadow(this, nullptr);
    initNerve(GET_NERVE(Takobo, HostTypeNrvWait));

    mSpinController = new SpinHitController(this, 22, 21, 3, 1.5f, 20.0f, 35.0f, 3, false);
    MR::initStarPointerTarget(this, 110.0f, Vec3f(0.0f, 80.0f, 0.0f));
    mScaleController = new AnimScaleController(nullptr);
    MR::initLightCtrl(this);
    appear();
    getSensor("attack")->invalidate();
    mBox = new SpinningBox("スピニングボックス");
    mBox->mIsIceBox = true;
    mBox->initWithoutIter();
}

void Takobo::initAfterPlacement() {
    MR::calcGravity(this);
    MR::offBind(this);
}

void Takobo::initSensor() {
    f32 yScale = mScale.y;
    initHitSensor(4);
    // The body sphere sits on the ground: its radius is its height.
    Vec3f bodyOffset(0.0f, 70.0f * yScale, 0.0f);
    MR::addHitSensor(this, "body", ATYPE_TAKOBO, 32, bodyOffset.y, bodyOffset);
    MR::addHitSensorAtJointEnemyAttack(this, "attack", "Hear", 0x10, (80.0f * yScale), Vec3f(10.0f, 2.0f, 0.0f));
}

void Takobo::kill() {
    MR::emitEffect(this, "TakoboDeath");
    MR::startSound(this, "SE_EM_EXPLODE_S");

    if (MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
    }

    LiveActor::kill();
}

void Takobo::control() {
    mScaleController->updateNerve();

    if (isNerve(GET_NERVE(Takobo, HostTypeNrvWait)) || isNerve(GET_NERVE(Takobo, HostTypeNrvMove)) || isNerve(GET_NERVE(Takobo, HostTypeNrvAttack)) ||
        isNerve(GET_NERVE(Takobo, HostTypeNrvHitReaction))) {
        if (MR::isStarPointerPointing2POnPressButton(this, "弱", true, false)) {
            setNerve(GET_NERVE(Takobo, HostTypeNrvPointerAimed));
            return;
        }
    }

    if (!mIsCheckFireGround) {
        return;
    }

    // Fire ground blows it off along the face normal and up.
    HitInfo info;
    if (Collision::checkStrikePointToMap(mPosition, &info) && MR::isGroundCodeDamageFire(&info.mParentTriangle)) {
        const Vec3f& rNormal = *info.mParentTriangle.getFaceNormal();
        Vec3f bounce(rNormal);
        bounce *= sFireBounceSpeed;
        Vec3f down(mGravity);
        down *= sFireBounceSpeed;
        bounce -= down;
        mVelocity = bounce;

        if (MR::isOnGround(this)) {
            Vec3f lift(rNormal);
            lift *= 5.0f;
            mPosition += lift;
        }

        setNerve(GET_NERVE(Takobo, HostTypeNrvHitPunch));
    }
}

void Takobo::generateCoin() {
    MR::appearCoinPop(this, mPosition - mGravity * 80.0f, 1);
}

bool Takobo::tryPress() {
    if (!isNerve(GET_NERVE(Takobo, HostTypeNrvPress))) {
        MR::startSound(this, "SE_EM_TAKOBO_STOMPED");
        setNerve(GET_NERVE(Takobo, HostTypeNrvPress));
        return true;
    }

    return false;
}

void Takobo::exeMove() {
    if (MR::isFirstStep(this)) {
        // Runs from where it stands to the far end of the range (-range..range around the start), 1.3 times the plain time.
        Vec3f fromRangeStart(mPosition);
        fromRangeStart -= mInitPos - mMoveDir * mMoveRange;
        f32 along = fromRangeStart.dot(mMoveDir);

        if (!mIsMoveBack) {
            mMoveEnd = mMoveRange;
            mMoveStart = -mMoveRange + along;
        } else {
            mMoveStart = -mMoveRange;
            mMoveEnd = -mMoveRange + along;
        }

        mMoveStep = (mMoveEnd - mMoveStart) / mMoveSpeed;
        mMoveStep *= 1.3;
    }

    f32 rate = MR::calcNerveRate(this, mMoveStep);

    if (mIsMoveBack) {
        rate = 1.0f - rate;
    }

    f32 ease = MR::getEaseInOutValue(rate, mMoveStart, mMoveEnd, 1.0f);
    mVelocity.set(mInitPos + mMoveDir * ease - mPosition);

    if (MR::isGreaterStep(this, mMoveStep)) {
        mIsMoveBack = !mIsMoveBack;
        setNerve(GET_NERVE(Takobo, HostTypeNrvMove));
        return;
    }

    Vec3f toPlayer(*MR::getPlayerPos() - mPosition);
    f32 playerDist = toPlayer.length();
    MR::vecKillElement(toPlayer, mGravity, &toPlayer);
    MR::normalizeOrZero(&toPlayer);

    if (playerDist < sAttackDistance) {
        if (MR::turnVecToVecRadian(&mFront, mFront, toPlayer, sTurnRadian, mGravity) < sAttackAimRadian) {
            setNerve(GET_NERVE(Takobo, HostTypeNrvAttack));
        }
    } else if (playerDist < sTurnDistance) {
        MR::turnVecToVecRadian(&mFront, mFront, toPlayer, sTurnRadian, mGravity);
    }
}

void Takobo::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Wait");
        mVelocity.zero();
    }

    if (MR::isGreaterStep(this, sWaitSteps)) {
        setNerve(GET_NERVE(Takobo, HostTypeNrvMove));
    }
}

void Takobo::exePress() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Press");
        MR::clearHitSensors(this);
        MR::invalidateHitSensors(this);
    }

    if (MR::isGreaterStep(this, sPressSteps)) {
        generateCoin();
        kill();
    }
}

void Takobo::exeHitPunch() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "HitPunch");
        MR::startSound(this, "SE_EM_TAKOBO_HITPUNCH");
        MR::startBlowHitSound(this);
        MR::clearHitSensors(this);
        MR::invalidateHitSensors(this);
        MR::onBind(this);
        MR::onCalcShadow(this, nullptr);
    }

    if (mSpinController->execute(this)) {
        MR::startSound(this, "SE_EM_EXPLODE_S");
    }
}

void Takobo::exeStunEnd() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "StunEnd");
    }

    if (MR::isActionEnd(this)) {
        setNerve(GET_NERVE(Takobo, HostTypeNrvWait));
    }
}

void Takobo::exeHitReaction() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "HitReaction");
    }

    if (MR::isActionEnd(this)) {
        setNerve(GET_NERVE(Takobo, HostTypeNrvWait));
    }
}

void Takobo::exeAttack() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Attack");
    }

    mVelocity.zero();

    if (MR::isStep(this, sAttackHitStart)) {
        MR::setBckRate(this, 1.0f);
        getSensor("attack")->validate();
    }

    if (MR::isStep(this, sAttackHitEnd)) {
        getSensor("attack")->invalidate();
    }

    if (MR::isActionEnd(this)) {
        setNerve(GET_NERVE(Takobo, HostTypeNrvWait));
    }
}

void Takobo::exeStunStart() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "StunStart");
        MR::setBckRate(this, 2.0f);
        MR::emitEffect(this, "Hit");
        MR::startSound(this, "SE_EM_TAKOBO_STARPIECE_HIT");
        mVelocity.zero();
    }

    if (MR::isActionEnd(this)) {
        setNerve(GET_NERVE(Takobo, HostTypeNrvStun));
    }
}

void Takobo::exeStun() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Stun");
    }

    MR::startLevelSound(this, "SE_EM_LV_SWOON_S");

    if (MR::isGreaterStep(this, sStunSteps)) {
        setNerve(GET_NERVE(Takobo, HostTypeNrvStunEnd));
    }
}

void Takobo::exeIce() {
    if (MR::isFirstStep(this)) {
        MR::hideModel(this);
        MR::invalidateHitSensors(this);
    }

    if (MR::isDead(mBox)) {
        mPosition.set(mBox->mPosition);
        generateCoin();
        kill();
    }
}

void Takobo::exePointerAimed() {
    if (MR::isFirstStep(this)) {
        MR::startPointerHitSound();
        MR::stopBck(this);
        mScaleController->startPointerHitVibration();
        MR::emitEffect(this, "Touch");
    }

    MR::startPointerFreezeLevelSound(this);
    if (!MR::isStarPointerPointing2POnPressButton(this, "弱", true, false)) {
        setNerve(GET_NERVE(Takobo, HostTypeNrvWait));
    } else {
        mVelocity.zero();
    }
}

void Takobo::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(Takobo, HostTypeNrvIce))) {
        return;
    }

    if (MR::isSensorEnemyAttack(pSender)) {
        if (MR::isSensorPlayer(pReceiver)) {
            MR::sendMsgEnemyAttackStrong(pReceiver, pSender);
        }

        return;
    }

    if (MR::isSensorPlayer(pReceiver)) {
        if (MR::isPlayerHipDropFalling() || MR::isPlayerHipDropLand()) {
            return;
        }

        if (isNerve(GET_NERVE(Takobo, HostTypeNrvMove)) || isNerve(GET_NERVE(Takobo, HostTypeNrvAttack))) {
            if (MR::isPlayerExistUp(this, getSensor("body")->mRadius, 0.25f)) {
                MR::sendMsgPush(pReceiver, pSender);
                return;
            }

            if (MR::sendMsgEnemyAttack(pReceiver, pSender)) {
                mScaleController->startHitReaction();
                setNerve(GET_NERVE(Takobo, HostTypeNrvHitReaction));
            } else {
                MR::sendMsgPush(pReceiver, pSender);
            }
        } else if (!isNerve(GET_NERVE(Takobo, HostTypeNrvPress)) && !isNerve(GET_NERVE(Takobo, HostTypeNrvHitPunch))) {
            MR::sendMsgPush(pReceiver, pSender);
        }
    }

    if (MR::isSensorEnemy(pReceiver) || MR::isSensorMapObj(pReceiver)) {
        MR::sendMsgPush(pReceiver, pSender);
    }
}

bool Takobo::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgPlayerKick(msg) && isNerve(GET_NERVE(Takobo, HostTypeNrvStun))) {
        mSpinController->start(this, pSender->mPosition, pReceiver->mPosition);
        setNerve(GET_NERVE(Takobo, HostTypeNrvHitPunch));

        return true;
    }

    return false;
}

bool Takobo::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgPlayerTrample(msg)) {
        if (MR::isSensorEnemyAttack(pReceiver)) {
            return false;
        }

        return tryPress();
    }

    if (MR::isMsgPlayerHipDrop(msg)) {
        if (MR::isSensorEnemyAttack(pReceiver) || isNerve(GET_NERVE(Takobo, HostTypeNrvHitPunch))) {
            return false;
        }

        return tryPress();
    }

    if (MR::isMsgFreezeAttack(msg)) {
        if (MR::isSensorEnemyAttack(pReceiver)) {
            return false;
        }

        mBox->generateIceBox(pSender, pReceiver);
        setNerve(GET_NERVE(Takobo, HostTypeNrvIce));

        return true;
    }

    if (isNerve(GET_NERVE(Takobo, HostTypeNrvHitPunch)) || isNerve(GET_NERVE(Takobo, HostTypeNrvPress))) {
        return false;
    }

    if (MR::isMsgLockOnStarPieceShoot(msg)) {
        return true;
    }

    if (MR::isMsgStarPieceAttack(msg)) {
        setNerve(GET_NERVE(Takobo, HostTypeNrvStunStart));

        return true;
    }

    if (MR::isMsgPlayerHitAll(msg)) {
        setNerve(GET_NERVE(Takobo, HostTypeNrvHitPunch));
        mSpinController->start(this, pSender->mPosition, pReceiver->mPosition);

        return true;
    }

    return false;
}

bool Takobo::receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (!pReceiver->isType(ATYPE_TAKOBO)) {
        return false;
    }

    if (isNerve(GET_NERVE(Takobo, HostTypeNrvHitPunch)) || isNerve(GET_NERVE(Takobo, HostTypeNrvPress))) {
        return false;
    }

    if (MR::isMsgToEnemyAttackTrample(msg)) {
        return tryPress();
    }

    if (MR::isMsgToEnemyAttackBlow(msg) || MR::isMsgExplosionAttack(msg)) {
        mSpinController->startWithoutStopScene(this, pSender->mPosition, pReceiver->mPosition);
        setNerve(GET_NERVE(Takobo, HostTypeNrvHitPunch));

        return true;
    }

    return false;
}

void Takobo::calcAndSetBaseMtx() {
    Pos3f mtx;
    MR::calcMtxFromGravityAndZAxis(&mtx, this, mGravity, mFront);
    MR::setBaseTRMtx(this, mtx);
    Vec3f scale = mScaleController->mScale * mScale;
    MR::setBaseScale(this, scale);
}

void Takobo::endPointerAimed() {
    mScaleController->startAnim();
    MR::deleteEffect(this, "Touch");
}

void Takobo::endAttack() {
    getSensor("attack")->invalidate();
}

void Takobo::endMove() {
    MR::onCalcAnim(this);
    mVelocity.zero();
}

void Takobo::endWait() {
    endMove();
}
