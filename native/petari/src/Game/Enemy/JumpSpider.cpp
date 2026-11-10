#include "Game/Enemy/JumpSpider.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/Enemy/SpinHitController.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/Nerve.hpp"
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
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f6, "init.f6", 110.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initAfterPlacement_f1, "initAfterPlacement.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initAfterPlacement_f2, "initAfterPlacement.f2", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f1, "exeWait.f1", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePreJump_f1, "exePreJump.f1", 0.05f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeJump_f1, "exeJump.f1", 22.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeJump_f2, "exeJump.f2", 0.02f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeJumpEnd_f1, "exeJumpEnd.f1", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHitToPlayer_f1, "exeHitToPlayer.f1", 0.05f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeStampFall_f1, "exeStampFall.f1", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_attackSensor_f1, "attackSensor.f1", 0.25f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_exeJump_applyVelocityDampAndGravity_1, "exeJump.applyVelocityDampAndGravity.1", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHitToPlayer_reboundVelocityFromCollision_1, "exeHitToPlayer.reboundVelocityFromCollision.1", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHitToPlayer_reboundVelocityFromCollision_2, "exeHitToPlayer.reboundVelocityFromCollision.2", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHitToPlayer_reboundVelocityFromCollision_3, "exeHitToPlayer.reboundVelocityFromCollision.3", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHitToPlayer_applyVelocityDampAndGravity_1, "exeHitToPlayer.applyVelocityDampAndGravity.1", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHitToPlayer_applyVelocityDampAndGravity_2, "exeHitToPlayer.applyVelocityDampAndGravity.2", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHitToPlayer_applyVelocityDampAndGravity_3, "exeHitToPlayer.applyVelocityDampAndGravity.3", 0.98f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHitToPlayer_applyVelocityDampAndGravity_4, "exeHitToPlayer.applyVelocityDampAndGravity.4", 0.98f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHitToPlayer_applyVelocityDampAndGravity_5, "exeHitToPlayer.applyVelocityDampAndGravity.5", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeNoPowerStart_applyVelocityDampAndGravity_1, "exeNoPowerStart.applyVelocityDampAndGravity.1", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeNoPowerStart_applyVelocityDampAndGravity_2, "exeNoPowerStart.applyVelocityDampAndGravity.2", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeNoPowerStart_applyVelocityDampAndGravity_3, "exeNoPowerStart.applyVelocityDampAndGravity.3", 0.98f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeNoPowerStart_applyVelocityDampAndGravity_4, "exeNoPowerStart.applyVelocityDampAndGravity.4", 0.98f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeNoPowerStart_applyVelocityDampAndGravity_5, "exeNoPowerStart.applyVelocityDampAndGravity.5", 1.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeHitToPlayer_1, "exeHitToPlayer.1", 58);


namespace {
    NEBULA_TUNABLE(f32, hGroundDamp, 0.8f);
    NEBULA_TUNABLE(f32, hAirDampH, 0.98f);
    NEBULA_TUNABLE(f32, hAirDampV, 0.98f);
    NEBULA_TUNABLE(f32, hJumpVelV, 1.0f);
    NEBULA_TUNABLE(s32, hStampDeathTime, 20);
    static const s32 hNoPowerWaitTime = NebulaFrameClock::stepsOfSeconds(4);
};  // namespace

namespace NrvJumpSpider {
    NEW_NERVE(HostTypeNrvWait, JumpSpider, Wait);
    NEW_NERVE(HostTypeNrvPreJump, JumpSpider, PreJump);
    NEW_NERVE(HostTypeNrvJump, JumpSpider, Jump);
    NEW_NERVE(HostTypeNrvJumpEnd, JumpSpider, JumpEnd);
    NEW_NERVE(HostTypeNrvHitToPlayer, JumpSpider, HitToPlayer);
    NEW_NERVE_ONEND(HostTypeNrvStampFall, JumpSpider, StampFall, StampFall);
    NEW_NERVE_ONEND(HostTypeNrvStampDeath, JumpSpider, StampDeath, StampDeath);
    NEW_NERVE_ONEND(HostTypeNrvHitBlow, JumpSpider, HitBlow, HitBlow);
    NEW_NERVE_ONEND(HostTypeNrvPointerAimed, JumpSpider, PointerAimed, PointerAimed);
    NEW_NERVE(HostTypeNrvPointerAimedEnd, JumpSpider, NoPowerStart);
    NEW_NERVE(HostTypeNrvStarPieceHit, JumpSpider, NoPowerStart);
    NEW_NERVE(HostTypeNrvPointerAimedLand, JumpSpider, NoPowerLand);
    NEW_NERVE(HostTypeNrvStarPieceHitLand, JumpSpider, NoPowerLand);
    NEW_NERVE(HostTypeNrvStarPieceHitWait, JumpSpider, NoPowerWait);
    NEW_NERVE(HostTypeNrvNoPowerEnd, JumpSpider, NoPowerEnd);
};  // namespace NrvJumpSpider

JumpSpider::JumpSpider(const char* pName) : LiveActor(pName), mSpinHitController(), mScaleController(), mFront(0, 0, 1), mJumpPos(0, 0, 0) {
}

void JumpSpider::init(const TableIter& rIter) {
    if (MR::isValidInfo(rIter)) {
        MR::initDefaultPos(this, rIter);
        MR::useStageSwitchWriteDead(this, rIter);
    }

    initModelManagerWithAnm("JumpSpider", nullptr, false);
    MR::initFur(this);
    Pos3f pos;
    pos.setInline(getBaseMtx());
    pos.getZDir(mFront);
    MR::connectToSceneEnemy(this);
    MR::initLightCtrl(this);
    f32 scaleY = mScale.y;
    initBinder(scaleY * cNum_init_f1, scaleY * cNum_init_f2, 0);
    initHitSensor(1);
    Vec3f offset1;
    offset1.set(0.0f, cNum_init_f3 * scaleY, 0.0f);
    MR::addHitSensorEnemy(this, "body", 32, scaleY * cNum_init_f4, offset1);
    initEffectKeeper(0, nullptr);
    initSound(4, false);
    initNerve(GET_NERVE(JumpSpider, HostTypeNrvWait));
    Vec3f offset2;
    offset2.x = 0.0f;
    offset2.y = cNum_init_f5;
    offset2.z = 0.0f;
    MR::initStarPointerTarget(this, cNum_init_f6, offset2);
    MR::declareStarPiece(this, 3);
    MR::declareCoin(this, 1);
    mSpinHitController = new SpinHitController(this, 3, false);
    mScaleController = new AnimScaleController(nullptr);
    MR::offCalcGravity(this);
    MR::initShadowFromCSV(this, "Shadow");
    MR::onCalcShadowAll(this);
    MR::emitEffect(this, "EyeLight");
}

void JumpSpider::initAfterPlacement() {
    MR::calcGravity(this);
    Vec3f lineStart(mPosition - mGravity * cNum_initAfterPlacement_f1);
    Vec3f floorNormal, floorPos;

    if (MR::getFirstPolyNormalOnLineToMap(&floorNormal, lineStart, mGravity * cNum_initAfterPlacement_f2, &floorPos, nullptr)) {
        mJumpPos.set(floorPos);
        mPosition.set(floorPos);
        mGravity.set(-floorNormal);
    } else {
        kill();
        return;
    }

    makeActorAppeared();
}

void JumpSpider::kill() {
    LiveActor::kill();
    MR::emitEffect(this, "Death");

    if (MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
    }
}

void JumpSpider::startClipped() {
    LiveActor::startClipped();
    ;
    MR::deleteEffect(this, "EyeLight");
}

void JumpSpider::endClipped() {
    LiveActor::endClipped();
    MR::emitEffect(this, "EyeLight");
}

void JumpSpider::control() {
    mScaleController->updateNerve();
    if (!isNerve(GET_NERVE(JumpSpider, HostTypeNrvStampFall)) && !isNerve(GET_NERVE(JumpSpider, HostTypeNrvStampDeath)) &&
        !isNerve(GET_NERVE(JumpSpider, HostTypeNrvHitBlow)) && !isNerve(GET_NERVE(JumpSpider, HostTypeNrvPointerAimed)) &&
        !isNerve(GET_NERVE(JumpSpider, HostTypeNrvStarPieceHit)) && !isNerve(GET_NERVE(JumpSpider, HostTypeNrvStarPieceHitLand)) &&
        !isNerve(GET_NERVE(JumpSpider, HostTypeNrvStarPieceHitWait)) && MR::isStarPointerPointing2POnPressButton(this, "弱", true, false)) {
        setNerve(GET_NERVE(JumpSpider, HostTypeNrvPointerAimed));
    }
}

void JumpSpider::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
    }

    if (MR::calcDistanceToPlayer(this) < cNum_exeWait_f1) {
        MR::startSound(this, "SE_EV_JUMPSPIDER_FIND");
        setNerve(GET_NERVE(JumpSpider, HostTypeNrvPreJump));
    }
}

void JumpSpider::exePreJump() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "JumpStart");
        mVelocity.zero();
    }

    Vec3f heightVec;
    MR::calcVecToPlayerH(&heightVec, this, nullptr);
    MR::turnVecToVecRadian(&mFront, mFront, heightVec, cNum_exePreJump_f1, mGravity);

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(JumpSpider, HostTypeNrvJump));
    }
}

void JumpSpider::exeJump() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Jump");
        MR::startSound(this, "SE_EM_JUMPSPIDER_JUMP");
        mVelocity.set(-mGravity * cNum_exeJump_f1);
    }

    MR::applyVelocityDampAndGravity(this, cNum_exeJump_applyVelocityDampAndGravity_1, ::hGroundDamp, ::hAirDampH, ::hAirDampV, ::hJumpVelV);
    Vec3f heightVec;
    MR::calcVecToPlayerH(&heightVec, this, nullptr);
    MR::turnVecToVecRadian(&mFront, mFront, heightVec, cNum_exeJump_f2, mGravity);

    if (MR::isOnGround(this)) {
        MR::startSound(this, "SE_EM_JUMPSPIDER_LAND");
        mPosition.set(mJumpPos);
        mVelocity.zero();
        MR::startBck(this, "JumpEnd");
        setNerve(GET_NERVE(JumpSpider, HostTypeNrvJumpEnd));
    }
}

void JumpSpider::exeJumpEnd() {

    mPosition.set(mJumpPos);

    if (MR::isBckStopped(this)) {
        if (cNum_exeJumpEnd_f1 < MR::calcDistanceToPlayer(this)) {
            setNerve(GET_NERVE(JumpSpider, HostTypeNrvWait));
        } else {
            setNerve(GET_NERVE(JumpSpider, HostTypeNrvPreJump));
        }
    }
}

void JumpSpider::exeHitToPlayer() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Hit");
        mVelocity.zero();
    }

    MR::reboundVelocityFromCollision(this, cNum_exeHitToPlayer_reboundVelocityFromCollision_1, cNum_exeHitToPlayer_reboundVelocityFromCollision_2, cNum_exeHitToPlayer_reboundVelocityFromCollision_3);
    MR::applyVelocityDampAndGravity(this, cNum_exeHitToPlayer_applyVelocityDampAndGravity_1, cNum_exeHitToPlayer_applyVelocityDampAndGravity_2, cNum_exeHitToPlayer_applyVelocityDampAndGravity_3, cNum_exeHitToPlayer_applyVelocityDampAndGravity_4, cNum_exeHitToPlayer_applyVelocityDampAndGravity_5);
    MR::killVelocityOnGroundH(this);

    if (MR::isGreaterStep(this, cStep_exeHitToPlayer_1)) {
        Vec3f heightVec;
        MR::calcVecToPlayerH(&heightVec, this, nullptr);
        MR::turnVecToVecRadian(&mFront, mFront, heightVec, cNum_exeHitToPlayer_f1, mGravity);
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(JumpSpider, HostTypeNrvJump));
    }
}

void JumpSpider::exeStampFall() {
    if (MR::isFirstStep(this)) {
        mVelocity.set(mGravity * cNum_exeStampFall_f1);
        MR::invalidateClipping(this);
    }

    if (MR::isOnGround(this)) {
        mVelocity.zero();
        mPosition.set(mJumpPos);
        setNerve(GET_NERVE(JumpSpider, HostTypeNrvStampDeath));
    }
}

void JumpSpider::endStampFall() {
    MR::validateClipping(this);
}

void JumpSpider::exeStampDeath() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Press");
        MR::invalidateClipping(this);
        mVelocity.zero();
    }

    mPosition.set(mJumpPos);

    if (MR::isGreaterStep(this, ::hStampDeathTime)) {
        MR::startSound(this, "SE_EM_EXPLODE_S");
        MR::appearCoinPop(this, mPosition, 1);
        kill();
    }
}

void JumpSpider::endStampDeath() {
    MR::validateClipping(this);
}

void JumpSpider::exeHitBlow() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Blow");
        MR::startBlowHitSound(this);
        MR::startSound(this, "SE_EV_JUMPSPIDER_DEAD");
        MR::onCalcGravity(this);
    }

    if (mSpinHitController->execute(this)) {
        MR::startSound(this, "SE_EM_EXPLODE_S");
    }
}

void JumpSpider::endHitBlow() {
    MR::validateClipping(this);
}

void JumpSpider::exePointerAimed() {
    if (MR::isFirstStep(this)) {
        MR::startPointerHitSound();
        MR::stopBck(this);
        mScaleController->startPointerHitVibration();
        MR::emitEffect(this, "Touch");
    }

    MR::startPointerFreezeLevelSound(this);

    if (!MR::isStarPointerPointing2POnPressButton(this, "弱", true, false)) {
        setNerve(GET_NERVE(JumpSpider, HostTypeNrvPointerAimedEnd));
    } else {
        mVelocity.zero();
    }
}

void JumpSpider::endPointerAimed() {
    mScaleController->startAnim();
    MR::deleteEffect(this, "Touch");
}

void JumpSpider::exeNoPowerStart() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "NoPowerStart");
        mVelocity.zero();
    }

    MR::applyVelocityDampAndGravity(this, cNum_exeNoPowerStart_applyVelocityDampAndGravity_1, cNum_exeNoPowerStart_applyVelocityDampAndGravity_2, cNum_exeNoPowerStart_applyVelocityDampAndGravity_3, cNum_exeNoPowerStart_applyVelocityDampAndGravity_4, cNum_exeNoPowerStart_applyVelocityDampAndGravity_5);

    if (MR::isOnGround(this) && MR::isBckStopped(this)) {
        mPosition.set(mJumpPos);
        MR::startSound(this, "SE_EM_JUMPSPIDER_LAND");

        if (isNerve(GET_NERVE(JumpSpider, HostTypeNrvStarPieceHit))) {
            mPosition.set(mJumpPos);
            mVelocity.zero();
            MR::startBck(this, "NoPowerLand");
            setNerve(GET_NERVE(JumpSpider, HostTypeNrvStarPieceHitLand));
        } else {
            MR::startBck(this, "NoPowerLand");
            setNerve(GET_NERVE(JumpSpider, HostTypeNrvPointerAimedLand));
        }
    }
}

void JumpSpider::exeNoPowerLand() {
    if (MR::isFirstStep(this)) {
        mVelocity.zero();
    }

    mPosition.set(mJumpPos);

    if (MR::isBckStopped(this)) {
        if (isNerve(GET_NERVE(JumpSpider, HostTypeNrvStarPieceHitLand))) {
            setNerve(GET_NERVE(JumpSpider, HostTypeNrvStarPieceHitWait));
        } else {
            setNerve(GET_NERVE(JumpSpider, HostTypeNrvNoPowerEnd));
        }
    }
}

void JumpSpider::exeNoPowerWait() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "NoPowerWait");
        mVelocity.zero();
    }

    mPosition.set(mJumpPos);
    MR::startLevelSound(this, "SE_EM_LV_SWOON_S");

    if (MR::isGreaterStep(this, ::hNoPowerWaitTime)) {
        setNerve(GET_NERVE(JumpSpider, HostTypeNrvNoPowerEnd));
    }
}

void JumpSpider::exeNoPowerEnd() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "NoPowerEnd");
        MR::startSound(this, "SE_EM_JUMPSPIDER_STEP");
    }

    mPosition.set(mJumpPos);

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(JumpSpider, HostTypeNrvWait));
    }
}

void JumpSpider::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(JumpSpider, HostTypeNrvHitBlow)) || isNerve(GET_NERVE(JumpSpider, HostTypeNrvStampDeath)) ||
        isNerve(GET_NERVE(JumpSpider, HostTypeNrvStampFall))) {
        return;
    }

    if (pSender != getSensor("body")) {
        return;
    }

    if (MR::isSensorPlayer(pReceiver)) {
        if (MR::isPlayerHipDropFalling() || MR::isPlayerHipDropLand()) {
            return;
        }

        if (isNerve(GET_NERVE(JumpSpider, HostTypeNrvWait)) || isNerve(GET_NERVE(JumpSpider, HostTypeNrvJumpEnd)) ||
            isNerve(GET_NERVE(JumpSpider, HostTypeNrvStarPieceHit)) || isNerve(GET_NERVE(JumpSpider, HostTypeNrvStarPieceHitLand)) ||
            isNerve(GET_NERVE(JumpSpider, HostTypeNrvStarPieceHitWait)) || isNerve(GET_NERVE(JumpSpider, HostTypeNrvPointerAimed)) ||
            isNerve(GET_NERVE(JumpSpider, HostTypeNrvPointerAimedLand)) || isNerve(GET_NERVE(JumpSpider, HostTypeNrvPointerAimedEnd)) ||
            isNerve(GET_NERVE(JumpSpider, HostTypeNrvNoPowerEnd))) {
            MR::sendMsgPush(pReceiver, pSender);
            return;
        }

        if (MR::isPlayerExistUp(this, getSensor("body")->mRadius, cNum_attackSensor_f1)) {
            MR::sendMsgPush(pReceiver, pSender);
            return;
        }

        if (MR::sendMsgEnemyAttack(pReceiver, pSender)) {
            setNerve(GET_NERVE(JumpSpider, HostTypeNrvHitToPlayer));
        } else {
            MR::sendMsgPush(pReceiver, pSender);
        }
    } else {
        MR::sendMsgPush(pReceiver, pSender);
    }
}

bool JumpSpider::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(JumpSpider, HostTypeNrvHitBlow)) || isNerve(GET_NERVE(JumpSpider, HostTypeNrvStampDeath)) ||
        isNerve(GET_NERVE(JumpSpider, HostTypeNrvStampFall))) {
        return false;
    }

    if (MR::isMsgLockOnStarPieceShoot(msg)) {
        return true;
    }

    if (MR::isMsgStarPieceAttack(msg)) {
        if (!isNerve(GET_NERVE(JumpSpider, HostTypeNrvStarPieceHit))) {
            setNerve(GET_NERVE(JumpSpider, HostTypeNrvStarPieceHit));
        }

        return true;
    }

    if (MR::isMsgPlayerHipDrop(msg) || MR::isMsgPlayerTrample(msg)) {
        MR::startSound(this, "SE_EM_STOMPED_S");
        MR::startSound(this, "SE_EV_JUMPSPIDER_DEAD");

        if (MR::isOnGround(this)) {
            setNerve(GET_NERVE(JumpSpider, HostTypeNrvStampDeath));
        } else {
            setNerve(GET_NERVE(JumpSpider, HostTypeNrvStampFall));
        }

        return true;
    }

    if (MR::isMsgPlayerHitAll(msg)) {
        mSpinHitController->start(this, pSender->mPosition, pReceiver->mPosition);
        setNerve(GET_NERVE(JumpSpider, HostTypeNrvHitBlow));
        return true;
    }

    return false;
}

bool JumpSpider::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgPlayerKick(msg) &&
        (isNerve(GET_NERVE(JumpSpider, HostTypeNrvStarPieceHit)) || isNerve(GET_NERVE(JumpSpider, HostTypeNrvStarPieceHitLand)) ||
         isNerve(GET_NERVE(JumpSpider, HostTypeNrvStarPieceHitWait)))) {
        mSpinHitController->start(this, pSender->mPosition, pReceiver->mPosition);
        setNerve(GET_NERVE(JumpSpider, HostTypeNrvHitBlow));
        return true;
    }

    return false;
}

void JumpSpider::calcAndSetBaseMtx() {
    Pos3f pos;
    MR::calcMtxFromGravityAndZAxis(&pos, this, mGravity, mFront);
    MR::setBaseTRMtx(this, pos);
    MR::setBaseScale(this, mScaleController->mScale * mScale);
}
