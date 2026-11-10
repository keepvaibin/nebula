#include "Game/Enemy/SamboHead.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/Enemy/SpinHitController.hpp"
#include "Game/Enemy/WalkerStateBindStarPointer.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 50.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_updateBounceFall_reboundVelocityFromCollision_1, "updateBounceFall.reboundVelocityFromCollision.1", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateBounceFall_reboundVelocityFromCollision_2, "updateBounceFall.reboundVelocityFromCollision.2", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateBounceFall_reboundVelocityFromCollision_3, "updateBounceFall.reboundVelocityFromCollision.3", 1.0f);

namespace NrvSamboHead {
    NEW_NERVE(HostTypeNrvWaitUnderGround, SamboHead, WaitUnderGround);
    NEW_NERVE(HostTypeNrvHide, SamboHead, Hide);
    NEW_NERVE(HostTypeNrvAppear, SamboHead, Appear);
    NEW_NERVE(HostTypeNrvAppearImmediately, SamboHead, Appear);
    NEW_NERVE(HostTypeNrvWait, SamboHead, Wait);
    NEW_NERVE(HostTypeNrvPrePursue, SamboHead, PrePursue);
    NEW_NERVE(HostTypeNrvPursue, SamboHead, Pursue);
    NEW_NERVE(HostTypeNrvPursueEnd, SamboHead, PursueEnd);
    NEW_NERVE(HostTypeNrvHitToPlayer, SamboHead, HitToPlayer);
    NEW_NERVE(HostTypeNrvStarPieceHit, SamboHead, StarPieceHit);
    NEW_NERVE_ONEND(HostTypeNrvStampFall, SamboHead, StampFall, StampFall);
    NEW_NERVE_ONEND(HostTypeNrvStampDeath, SamboHead, StampDeath, StampDeath);
    NEW_NERVE_ONEND(HostTypeNrvHitBlow, SamboHead, HitBlow, HitBlow);
    NEW_NERVE(HostTypeNrvBuryDeath, SamboHead, BuryDeath);
    NEW_NERVE_ONEND(HostTypeNrvBindStarPointer, SamboHead, BindStarPointer, BindStarPointer);
    NEW_NERVE(HostTypeNrvBindStarPointerEnd, SamboHead, BindStarPointerEnd);
};  // namespace NrvSamboHead

namespace {
    NEBULA_TUNABLE(f32, cAppearDistance, 800.0f);
    NEBULA_TUNABLE(f32, cTurnDistance, 500.0f);
    NEBULA_TUNABLE(f32, cGiveUpDistance, 1000.0f);
    const s32 cUnderGroundSteps = NebulaFrameClock::stepsOfSeconds(2);
    const s32 cHideSteps = NebulaFrameClock::stepsOfSeconds(2);
    const s32 cWaitHideSteps = NebulaFrameClock::stepsOfSeconds(2);
    const s32 cShadowHideStep = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(s32, cShadowShowStep, 63);
    NEBULA_TUNABLE(f32, cAppearImmediatelyFrame, 45.0f);
    // Appear's rise hits the player only after this step.
    NEBULA_TUNABLE(s32, cAppearAttackStep, 45);
    NEBULA_TUNABLE(s32, cHitTurnStep, 58);
    NEBULA_TUNABLE(s32, cStampDeathSteps, 35);
    NEBULA_TUNABLE(f32, cTurnRadian, 0.05f);
    // Pursue starts once a turn step is below this.
    NEBULA_TUNABLE(f32, cFacingRadian, 0.015f);
    NEBULA_TUNABLE(f32, cRunSpeed, 7.0f);
    NEBULA_TUNABLE(f32, cJumpSpeed, 25.0f);
    NEBULA_TUNABLE(f32, cStampFallSpeed, 30.0f);
    NEBULA_TUNABLE(f32, cGravity, 1.5f);
    NEBULA_TUNABLE(f32, cGroundDampH, 0.80f);
    NEBULA_TUNABLE(f32, cAirDampH, 0.98f);
    NEBULA_TUNABLE(f32, cDampVUp, 0.98f);
    NEBULA_TUNABLE(f32, cVelHMin, 1.0f);
    NEBULA_TUNABLE(f32, cPushScale, 2.0f);
    NEBULA_TUNABLE(f32, cPlayerExistRatio, 0.25f);
    NEBULA_TUNABLE(f32, cRadius, 100.0f);
    NEBULA_TUNABLE(f32, cPointerRadius, 100.0f);
    NEBULA_TUNABLE(f32, cShadowRadius, 60.0f);
    NEBULA_TUNABLE(s32, cSensorCount, 32);
    NEBULA_TUNABLE(s32, cStarPieceCount, 3);
    NEBULA_TUNABLE(s32, cCoinCount, 1);
    NEBULA_TUNABLE(s32, cSpinItemCount, 3);
}  // namespace

SamboHead::SamboHead(const char* pName)
    : LiveActor(pName), mSpinCtrl(), mScaleController(), mBindStarPointer(), mFront(0, 0, 1), mInitPos(0, 0, 0), mIsPursueAlways(false) {
    lumacore::setClass(this, lumacore::kClassId< SamboHead >);
}

void SamboHead::init(const TableIter& rIter) {
    if (MR::isValidInfo(rIter)) {
        MR::initDefaultPos(this, rIter);
        MR::useStageSwitchWriteDead(this, rIter);
        if (MR::useStageSwitchReadAppear(this, rIter)) {
            MR::syncStageSwitchAppear(this);
        }

        MR::useStageSwitchReadA(this, rIter);
        s32 pursueAlways = -1;
        MR::getPlacementArg0NoInit(rIter, &pursueAlways);
        mIsPursueAlways = pursueAlways != -1;
    }

    mInitPos.set(mPosition);
    initModelManagerWithAnm("SamboHead", nullptr, false);
    Pos3f mtx = getBaseMtx();
    mtx.getZDir(mFront);
    MR::connectToSceneEnemy(this);
    MR::initLightCtrl(this);
    f32 scaleY = mScale.y;
    initBinder((cRadius * scaleY), (cRadius * scaleY), 0);
    MR::initStarPointerTarget(this, cPointerRadius,Vec3f(0.0f, cNum_init_f1, 0.0f));
    initHitSensor(1);
    MR::addHitSensorEnemy(this, "body", cSensorCount, cRadius * scaleY, Vec3f(0.0f, cRadius * scaleY, 0.0f));
    initEffectKeeper(1, nullptr);
    initSound(4, false);
    initNerve(GET_NERVE(SamboHead, HostTypeNrvWaitUnderGround));
    MR::onCalcGravity(this);
    MR::initShadowVolumeSphere(this, (cShadowRadius * scaleY));
    MR::declareStarPiece(this, cStarPieceCount);
    MR::declareCoin(this, cCoinCount);
    mScaleController = new AnimScaleController(nullptr);
    mBindStarPointer = new WalkerStateBindStarPointer(this, mScaleController);

    if (MR::isValidSwitchAppear(this)) {
        makeActorDead();
    } else {
        makeActorAppeared();
    }

    mSpinCtrl = new SpinHitController(this, cSpinItemCount, false);
}

void SamboHead::appear() {
    LiveActor::appear();
    setNerve(GET_NERVE(SamboHead, HostTypeNrvAppearImmediately));
}

void SamboHead::kill() {
    LiveActor::kill();
    MR::emitEffect(this, "Death");
    MR::startSound(this, "SE_EM_EXPLODE_S");

    if (MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
    }
}

void SamboHead::control() {
    mScaleController->updateNerve();

    if (!isNerve(GET_NERVE(SamboHead, HostTypeNrvStampFall)) && !isNerve(GET_NERVE(SamboHead, HostTypeNrvStampDeath)) &&
        !isNerve(GET_NERVE(SamboHead, HostTypeNrvHitBlow)) && !isNerve(GET_NERVE(SamboHead, HostTypeNrvBuryDeath)) &&
        MR::isBindedGroundSinkDeath(this)) {
        setNerve(GET_NERVE(SamboHead, HostTypeNrvBuryDeath));
        return;
    }

    if (!isPursuing() && !isNerve(GET_NERVE(SamboHead, HostTypeNrvHitToPlayer))) {
        return;
    }

    if (mBindStarPointer->tryStartPointBind()) {
        setNerve(GET_NERVE(SamboHead, HostTypeNrvBindStarPointer));
    }
}

void SamboHead::exeWaitUnderGround() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "AppearMini");
        mPosition.set(mInitPos);
        MR::invalidateShadow(this, nullptr);
        getSensor("body")->invalidate();
    }

    if (!MR::isValidSwitchA(this) || MR::isOnSwitchA(this)) {
        mVelocity.zero();
        f32 dist = MR::calcDistanceToPlayer(this);

        if (MR::isGreaterStep(this, cUnderGroundSteps) && dist < cAppearDistance) {
            getSensor("body")->validate();
            setNerve(GET_NERVE(SamboHead, HostTypeNrvAppear));
        }
    }
}

void SamboHead::exeHide() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Hide");
    }

    mVelocity.zero();

    if (MR::isStep(this, cShadowHideStep)) {
        MR::invalidateShadow(this, nullptr);
    }

    if (MR::isGreaterStep(this, cHideSteps)) {
        setNerve(GET_NERVE(SamboHead, HostTypeNrvWaitUnderGround));
    }
}

void SamboHead::exeAppear() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Appear");

        if (isNerve(GET_NERVE(SamboHead, HostTypeNrvAppearImmediately))) {
            MR::setBckFrame(this, cAppearImmediatelyFrame);
        }
    }

    mVelocity.zero();

    if (MR::isStep(this, cShadowShowStep)) {
        MR::validateShadow(this, nullptr);
    }

    if (MR::isActionEnd(this)) {
        setNerve(GET_NERVE(SamboHead, HostTypeNrvPrePursue));
    }
}

void SamboHead::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Wait");
    }

    updateBounceFall();
    f32 dist = MR::calcDistanceToPlayer(this);

    if (cAppearDistance < dist && !mIsPursueAlways) {
        if (MR::isGreaterStep(this, cWaitHideSteps)) {
            setNerve(GET_NERVE(SamboHead, HostTypeNrvHide));
        }
    } else if (dist < cTurnDistance || mIsPursueAlways) {
        if (turnToPlayer() < cFacingRadian) {
            if (mIsPursueAlways) {
                setNerve(GET_NERVE(SamboHead, HostTypeNrvPursue));
            } else {
                setNerve(GET_NERVE(SamboHead, HostTypeNrvPrePursue));
            }
        }
    } else {
        turnToPlayer();
    }
}

void SamboHead::exePrePursue() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Find");
    }

    turnToPlayer();

    if (MR::isActionEnd(this) && MR::isOnGround(this)) {
        setNerve(GET_NERVE(SamboHead, HostTypeNrvPursue));
        MR::startSound(this, "SE_EM_SAMBOHEAD_HOP_LAND");
    } else {
        updateBounceFall();
    }
}

void SamboHead::exePursue() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Run");
        mVelocity.set(mFront * cRunSpeed);
        MR::addVelocityJump(this, cJumpSpeed);
    }

    updateFall();
    turnToPlayer();

    if (MR::isOnGround(this)) {
        setNerve(GET_NERVE(SamboHead, HostTypeNrvPursueEnd));
        MR::startSound(this, "SE_EM_SAMBOHEAD_HOP_LAND");
    }
}

void SamboHead::exePursueEnd() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Land");
    }

    mVelocity.zero();

    if (MR::isActionEnd(this)) {
        if (cGiveUpDistance < MR::calcDistanceToPlayer(this)) {
            setNerve(GET_NERVE(SamboHead, HostTypeNrvWait));
        } else {
            setNerve(GET_NERVE(SamboHead, HostTypeNrvPursue));
        }
    }
}

void SamboHead::exeHitToPlayer() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Hit");
        MR::startSound(this, "SE_EM_SAMBOHEAD_ATTACK");
    }

    updateBounceFall();

    if (MR::isGreaterStep(this, cHitTurnStep)) {
        turnToPlayer();
    }

    if (MR::isActionEnd(this)) {
        setNerve(GET_NERVE(SamboHead, HostTypeNrvWait));
    }
}

void SamboHead::exeStarPieceHit() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "StarPieceHit");
    }

    MR::startLevelSound(this, "SE_EM_LV_SWOON_S");
    updateBounceFall();

    if (MR::isActionEnd(this)) {
        setNerve(GET_NERVE(SamboHead, HostTypeNrvWait));
    }
}

void SamboHead::exeStampFall() {
    if (MR::isFirstStep(this)) {
        mVelocity.set(mGravity * cStampFallSpeed);
        MR::invalidateClipping(this);
    }

    if (MR::isOnGround(this)) {
        setNerve(GET_NERVE(SamboHead, HostTypeNrvStampDeath));
    }
}

void SamboHead::exeStampDeath() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Press");
        MR::invalidateClipping(this);
    }

    MR::killVelocityOnGroundH(this);

    if (MR::isGreaterStep(this, cStampDeathSteps)) {
        kill();
        MR::appearCoinPop(this, mPosition, cCoinCount);
    }
}

void SamboHead::exeHitBlow() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Blow");
        MR::startBlowHitSound(this);
        getSensor("body")->invalidate();
    }

    if (mSpinCtrl->execute(this)) {
        MR::startSound(this, "SE_EM_EXPLODE_S");
        getSensor("body")->validate();
    }
}

void SamboHead::exeBuryDeath() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Sink");
    }

    mVelocity.zero();

    if (MR::isActionEnd(this)) {
        kill();
    }
}

void SamboHead::exeBindStarPointer() {
    if (MR::isFirstStep(this)) {
        MR::startPointerHitSound();
    }

    MR::startPointerFreezeLevelSound(this);
    MR::updateActorStateAndNextNerve(this, mBindStarPointer, GET_NERVE(SamboHead, HostTypeNrvBindStarPointerEnd));
}

void SamboHead::exeBindStarPointerEnd() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Wait");
    }

    updateBounceFall();

    if (MR::isOnGround(this)) {
        setNerve(GET_NERVE(SamboHead, HostTypeNrvWait));
    }
}

void SamboHead::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (pSender != getSensor("body")) {
        return;
    }

    if (!MR::isSensorPlayer(pReceiver)) {
        if (MR::isSensorEnemy(pReceiver)) {
            MR::sendMsgPush(pReceiver, pSender);
        }
        return;
    }

    if (MR::isPlayerHipDropFalling() || MR::isPlayerHipDropLand()) {
        return;
    }

    bool isPursuingNow = isPursuing();
    if (isPursuingNow || (isNerve(GET_NERVE(SamboHead, HostTypeNrvAppear)) && MR::isGreaterStep(this, cAppearAttackStep))) {
        if (MR::isPlayerExistUp(this, pSender->mRadius, cPlayerExistRatio)) {
            MR::sendMsgPush(pReceiver, pSender);
            return;
        }

        if (MR::sendMsgEnemyAttack(pReceiver, pSender)) {
            if (isPursuingNow) {
                setNerve(GET_NERVE(SamboHead, HostTypeNrvHitToPlayer));
            }
            return;
        }

        MR::sendMsgPush(pReceiver, pSender);
        return;
    }

    if (!isInactive()) {
        MR::sendMsgPush(pReceiver, pSender);
    }
}

bool SamboHead::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (isInactive()) {
        return false;
    }

    if (MR::isMsgLockOnStarPieceShoot(msg)) {
        return true;
    }

    if (MR::isMsgStarPieceAttack(msg)) {
        if (isNerve(GET_NERVE(SamboHead, HostTypeNrvStarPieceHit))) {
            mScaleController->startHitReaction();
        } else {
            setNerve(GET_NERVE(SamboHead, HostTypeNrvStarPieceHit));
        }

        return true;
    }

    if (MR::isMsgPlayerHipDrop(msg) || MR::isMsgPlayerTrample(msg)) {
        MR::startSound(this, "SE_EM_STOMPED_S");
        if (MR::isOnGround(this)) {
            setNerve(GET_NERVE(SamboHead, HostTypeNrvStampDeath));
        } else {
            setNerve(GET_NERVE(SamboHead, HostTypeNrvStampFall));
        }

        return true;
    }

    if (MR::isMsgPlayerHitAll(msg)) {
        mSpinCtrl->start(this, pSender->mPosition, pReceiver->mPosition);
        setNerve(GET_NERVE(SamboHead, HostTypeNrvHitBlow));
        return true;
    }

    return false;
}

bool SamboHead::receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::isSensorEnemy(pSender) && !MR::isSensorMapObj(pSender)) {
        return false;
    }

    if (isInactive()) {
        return false;
    }

    MR::addVelocityFromPush(this, cPushScale, pSender, pReceiver);
    return true;
}

bool SamboHead::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (isInactive()) {
        return false;
    }

    if (MR::isMsgInhaleBlackHole(msg)) {
        kill();
        return true;
    }

    if (MR::isMsgPlayerKick(msg) && MR::isPlayerExistSide(this, getSensor("body")->mRadius, cPlayerExistRatio) &&
        isNerve(GET_NERVE(SamboHead, HostTypeNrvStarPieceHit))) {
        mSpinCtrl->start(this, pSender->mPosition, pReceiver->mPosition);
        setNerve(GET_NERVE(SamboHead, HostTypeNrvHitBlow));
        return true;
    }

    return false;
}

void SamboHead::calcAndSetBaseMtx() {
    Pos3f mtx;
    MR::calcMtxFromGravityAndZAxis(&mtx, this, mGravity, mFront);
    MR::setBaseTRMtx(this, mtx);
    Vec3f scale = mScaleController->mScale * mScale;
    MR::setBaseScale(this, scale);
}

void SamboHead::endBindStarPointer() {
    mBindStarPointer->kill();
}

void SamboHead::endHitBlow() {
    MR::validateClipping(this);
}

void SamboHead::endStampDeath() {
    MR::validateClipping(this);
}

void SamboHead::endStampFall() {
    MR::validateClipping(this);
}

f32 SamboHead::turnToPlayer() {
    Vec3f toPlayer;
    MR::calcVecToPlayerH(&toPlayer, this, nullptr);
    return MR::turnVecToVecRadian(&mFront, mFront, toPlayer, cTurnRadian, mGravity);
}

void SamboHead::updateFall() {
    MR::applyVelocityDampAndGravity(this, cGravity, cGroundDampH, cAirDampH, cDampVUp, cVelHMin);
    MR::killVelocityOnGroundH(this);
}

void SamboHead::updateBounceFall() {
    MR::reboundVelocityFromCollision(this, cNum_updateBounceFall_reboundVelocityFromCollision_1, cNum_updateBounceFall_reboundVelocityFromCollision_2, cNum_updateBounceFall_reboundVelocityFromCollision_3);
    updateFall();
}

bool SamboHead::isPursuing() const {
    return isNerve(GET_NERVE(SamboHead, HostTypeNrvWait)) || isNerve(GET_NERVE(SamboHead, HostTypeNrvPrePursue)) ||
           isNerve(GET_NERVE(SamboHead, HostTypeNrvPursue)) || isNerve(GET_NERVE(SamboHead, HostTypeNrvPursueEnd));
}

bool SamboHead::isInactive() const {
    return isNerve(GET_NERVE(SamboHead, HostTypeNrvWaitUnderGround)) || isNerve(GET_NERVE(SamboHead, HostTypeNrvHide)) ||
           isNerve(GET_NERVE(SamboHead, HostTypeNrvStampFall)) || isNerve(GET_NERVE(SamboHead, HostTypeNrvStampDeath)) ||
           isNerve(GET_NERVE(SamboHead, HostTypeNrvHitBlow));
}
