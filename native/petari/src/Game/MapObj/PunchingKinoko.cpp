#include "Game/MapObj/PunchingKinoko.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include <nebula_geometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f1, "initSensor.f1", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f2, "initSensor.f2", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f3, "initSensor.f3", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initShadow_f1, "initShadow.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initShadow_f2, "initShadow.f2", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initShadow_f3, "initShadow.f3", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initShadow_f4, "initShadow.f4", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_attackSensor_f1, "attackSensor.f1", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_attackSensor_f2, "attackSensor.f2", 45.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_attackSensor_f3, "attackSensor.f3", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_attackSensor_f4, "attackSensor.f4", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_attackSensor_f5, "attackSensor.f5", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_attackSensor_f6, "attackSensor.f6", 1.6f);
NEBULA_TUNABLE_NAMED(f32, cNum_attackSensor_f7, "attackSensor.f7", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveMsgPlayerAttack_f1, "receiveMsgPlayerAttack.f1", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveMsgPlayerAttack_f2, "receiveMsgPlayerAttack.f2", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveMsgPush_f1, "receiveMsgPush.f1", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveMsgPush_f2, "receiveMsgPush.f2", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_requestPunch_f1, "requestPunch.f1", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_requestEnemyBlow_f1, "requestEnemyBlow.f1", 25.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_requestTrample_f1, "requestTrample.f1", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f1, "exeWait.f1", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f2, "exeWait.f2", 35.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f3, "exeWait.f3", 0.05f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSwing_f1, "exeSwing.f1", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSwing_f2, "exeSwing.f2", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSwing_f3, "exeSwing.f3", 0.05f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSwing_f4, "exeSwing.f4", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSwing_f5, "exeSwing.f5", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeCrushed_f1, "exeCrushed.f1", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_addVelocityKeepHeight_f1, "addVelocityKeepHeight.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_addVelocityKeepHeight_f2, "addVelocityKeepHeight.f2", 60.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_control_reboundVelocityFromCollision_1, "control.reboundVelocityFromCollision.1", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_reboundVelocityFromCollision_2, "control.reboundVelocityFromCollision.2", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_reboundVelocityFromCollision_3, "control.reboundVelocityFromCollision.3", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_attenuateVelocity_1, "exeWait.attenuateVelocity.1", 0.94f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSwing_attenuateVelocity_1, "exeSwing.attenuateVelocity.1", 0.97f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePointSnaped_attenuateVelocity_1, "exePointSnaped.attenuateVelocity.1", 0.99f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePunched_attenuateVelocity_1, "exePunched.attenuateVelocity.1", 0.99f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePunchedBrake_attenuateVelocity_1, "exePunchedBrake.attenuateVelocity.1", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHitted_attenuateVelocity_1, "exeHitted.attenuateVelocity.1", 0.99f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeCrushedEnd_attenuateVelocity_1, "exeCrushedEnd.attenuateVelocity.1", 0.94f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exePointSnaped_1, "exePointSnaped.1", 6);
NEBULA_TUNABLE_NAMED(s32, cStep_exePunched_1, "exePunched.1", 5);
NEBULA_TUNABLE_NAMED(s32, cStep_exePunchedBrake_1, "exePunchedBrake.1", 40);
NEBULA_TUNABLE_NAMED(s32, cStep_exeHitted_1, "exeHitted.1", 5);
NEBULA_TUNABLE_NAMED(s32, cStep_exeCrushedEnd_1, "exeCrushedEnd.1", 10);

namespace NrvPunchingKinoko {
    NEW_NERVE(PunchingKinokoNrvWait, PunchingKinoko, Wait);
    NEW_NERVE(PunchingKinokoNrvSwing, PunchingKinoko, Swing);
    NEW_NERVE(PunchingKinokoNrvPunched, PunchingKinoko, Punched);
    NEW_NERVE(PunchingKinokoNrvPunchedBrake, PunchingKinoko, PunchedBrake);
    NEW_NERVE(PunchingKinokoNrvHitted, PunchingKinoko, Hitted);
    NEW_NERVE(PunchingKinokoNrvPointSnaped, PunchingKinoko, PointSnaped);
    NEW_NERVE(PunchingKinokoNrvCrushed, PunchingKinoko, Crushed);
    NEW_NERVE(PunchingKinokoNrvCrushedEnd, PunchingKinoko, CrushedEnd);
};  // namespace NrvPunchingKinoko

namespace {
    // The head rests this far above the stem's base.
    NEBULA_TUNABLE(f32, cHeadHeight, 130.0f);
    NEBULA_TUNABLE(f32, cSpringRate, 0.008f);
};  // namespace

PunchingKinoko::PunchingKinoko(const char* pName)
    : LiveActor(pName), mGroundChecker(nullptr), mScaleController(nullptr), mDelegator(nullptr), mCameraVecId(-1), mCameraTarget(0, 0, 0),
      mShadowNormal(0, 1, 0), mStarPointerHitCoolDown(0), mIsPointerHeld(true), mInvincibleHitCoolDown(-1) {
}

// The head is its own actor (mGroundChecker) on a spring to the rest point; the stem is a shadow line between the two.
void PunchingKinoko::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("PunchingKinoko", nullptr, false);
    MR::connectToSceneNoSilhouettedMapObj(this);
    MR::calcGravity(this);
    mCameraTarget.set(mPosition);

    mGroundChecker = new GroundChecker("頭コリジョン", cNum_init_f1, 0.0f);
    MR::calcPositionUpOffset(&mGroundChecker->mPosition, this, cHeadHeight);
    MR::resetPosition(mGroundChecker);
    MR::onCalcGravity(mGroundChecker);

    mScaleController = new AnimScaleController(nullptr);

    MR::initStarPointerTargetAtPos(this, cNum_init_f2, &mGroundChecker->mPosition, Vec3f(0, 0, 0));

    initShadow();
    initSensor();
    initEffectKeeper(0, nullptr);
    initSound(4, false);
    initCamera(rIter);
    initJointControl();
    initNerve(GET_NERVE(PunchingKinoko, PunchingKinokoNrvWait));

    makeActorAppeared();

    if (MR::useStageSwitchReadB(this, rIter)) {
        MR::listenStageSwitchOnB(this, MR::Functor(this, &PunchingKinoko::kill));
    }
    MR::useStageSwitchSleep(this, rIter);
}

void PunchingKinoko::initSensor() {
    initHitSensor(2);
    MR::addHitSensorAtJointEnemy(this, "Head", "Ball", 16, cNum_initSensor_f1, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorEnemy(this, "Body", 16, cNum_initSensor_f2, Vec3f(0.0f, cNum_initSensor_f3, 0.0f));
}

void PunchingKinoko::initShadow() {
    MR::initShadowController(this, 3);
    MR::addShadowVolumeSphere(this, "体", cNum_initShadow_f1);
    MR::onCalcShadowOneTime(this, "体");
    MR::addShadowVolumeSphere(this, "頭", cNum_initShadow_f2);
    MR::setShadowDropPositionPtr(this, "頭", &mGroundChecker->mPosition);
    MR::setShadowDropDirectionPtr(this, "頭", &mGroundChecker->mGravity);
    MR::addShadowVolumeLine(this, "つた", this, "体", cNum_initShadow_f3, this, "頭", cNum_initShadow_f4);
}

void PunchingKinoko::initCamera(const TableIter& rIter) {
    MR::getPlacementArg7WithInit(rIter, &mCameraVecId);
    if (mCameraVecId != -1) {
        MR::declareCameraRegisterVec(this, mCameraVecId, &mCameraTarget);
    }
}

void PunchingKinoko::initJointControl() {
    mDelegator = MR::createJointDelegatorWithNullChildFunc(this, &PunchingKinoko::ballMtxCallBack, "Ball");
}

// The ball joint points from the base to the head, squashed along mShadowNormal, and sits on the head.
bool PunchingKinoko::ballMtxCallBack(Pos3f* pMtx, const JointControllerInfo& /*rJoint*/) {
    Vec3f stemDir;
    stemDir.sub(mGroundChecker->mPosition, mPosition);
    MR::orthogonalize(pMtx);
    if (!MR::normalizeOrZero(&stemDir)) {
        MR::turnMtxToYDirRate(pMtx, stemDir, 1.0f);
    }

    Pos3f squash;
    squash.identity();

    MR::scaleMtxToDir(&squash, mShadowNormal, mScaleController->mScale);
    pMtx->concat(squash, *pMtx);
    pMtx->setTrans(mGroundChecker->mPosition);

    return true;
}

void PunchingKinoko::makeActorAppeared() {
    LiveActor::makeActorAppeared();
    mGroundChecker->makeActorAppeared();
}

void PunchingKinoko::kill() {
    LiveActor::kill();
}

void PunchingKinoko::makeActorDead() {
    LiveActor::makeActorDead();
    mGroundChecker->makeActorDead();
}

void PunchingKinoko::control() {
    if (mStarPointerHitCoolDown > 0) {
        mStarPointerHitCoolDown--;
    }
    mScaleController->update();
    mGroundChecker->movement();
    MR::reboundVelocityFromCollision(mGroundChecker, cNum_control_reboundVelocityFromCollision_1, cNum_control_reboundVelocityFromCollision_2, cNum_control_reboundVelocityFromCollision_3);
    if (mInvincibleHitCoolDown >= 0) {
        mInvincibleHitCoolDown--;
    }
}

void PunchingKinoko::calcAndSetBaseMtx() {
    Pos3f baseMtx;
    Vec3f up;
    up.negate(mGravity);
    MR::makeMtxUpNoSupportPos(&baseMtx, up, mPosition);
    MR::setBaseTRMtx(this, baseMtx);
    mDelegator->registerCallBack();
}

// The head hits by its speed: the player is flipped from 15, flipped along the swing from 30, flipped
// hardest from 45; enemies are blown. A hit bounces the head back off the receiver at 30% speed.
void PunchingKinoko::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (isCrushed()) {
        return;
    }

    if (pSender == getSensor("Body")) {
        if (isNerve(GET_NERVE(PunchingKinoko, PunchingKinokoNrvWait))) {
            MR::sendMsgPush(pReceiver, pSender);
        }
        return;
    }

    if (isNerve(GET_NERVE(PunchingKinoko, PunchingKinokoNrvCrushedEnd)) && MR::isSensorPlayer(pReceiver) &&
        MR::sendMsgEnemyAttackFlipWeakJump(pReceiver, pSender)) {
        return;
    }

    Vec3f dir;
    f32 speed;
    MR::separateScalarAndDirection(&speed, &dir, mGroundChecker->mVelocity);
    bool hit = false;
    if (MR::isSensorPlayer(pReceiver)) {
        if (!isEnableHitPlayer()) {
            MR::sendMsgPush(pReceiver, pSender);
        } else if (speed >= cNum_attackSensor_f1) {
            if (speed >= cNum_attackSensor_f2) {
                hit = MR::sendMsgEnemyAttackFlipMaximumToDir(pReceiver, pSender, dir * cNum_attackSensor_f3);
            } else {
                hit = MR::sendMsgEnemyAttackFlipToDir(pReceiver, pSender, dir * cNum_attackSensor_f4);
            }
            if (hit) {
                MR::startSoundPlayer("SE_PM_WALL_HIT_BODY");
                MR::scatterStarPiecePlayer(10);
            }
        } else if (speed >= cNum_attackSensor_f5) {
            hit = MR::sendMsgEnemyAttackFlipWeak(pReceiver, pSender);
        } else {
            MR::sendMsgPush(pReceiver, pSender);
        }
    } else {
        if (isEnableEnemyAttack()) {
            hit = MR::sendMsgToEnemyAttackBlow(pReceiver, pSender);
        }
        if (!hit) {
            MR::sendMsgPush(pReceiver, pSender);
        }
    }

    if (hit) {
        MR::emitEffectHitBetweenSensors(this, pSender, pReceiver, 0.0f, "Hit");
        Vec3f toHead;
        MR::calcSensorDirectionNormalize(&toHead, pReceiver, pSender);
        f32 reflect = mGroundChecker->mVelocity.dot(toHead) * cNum_attackSensor_f6;
        mGroundChecker->mVelocity -= toHead * reflect;
        mGroundChecker->mVelocity.mult(cNum_attackSensor_f7);
        setNerve(GET_NERVE(PunchingKinoko, PunchingKinokoNrvWait));
    }
}

bool PunchingKinoko::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (isCrushed()) {
        return false;
    }

    if (pReceiver == getSensor("Body")) {
        return false;
    }

    if (MR::isMsgInvincibleAttack(msg)) {
        if (mInvincibleHitCoolDown < 0) {
            MR::startBlowHitSound(this);
            mInvincibleHitCoolDown = 10;
        }

        return requestEnemyBlow(pSender, pReceiver);
    }

    if (MR::isMsgPlayerHipDrop(msg)) {
        return requestCrush();
    }

    if (MR::isMsgLockOnStarPieceShoot(msg)) {
        return true;
    }

    // A star piece pushes the head away at 15 unless it already moves away faster.
    if (MR::isMsgStarPieceAttack(msg)) {
        Vec3f away;
        MR::calcSensorDirectionNormalize(&away, pSender, pReceiver);

        if (mGroundChecker->mVelocity.dot(away) < cNum_receiveMsgPlayerAttack_f1) {
            mGroundChecker->mVelocity.add(away * cNum_receiveMsgPlayerAttack_f2);
        }

        MR::startSound(this, "SE_OJ_PNC_KINOKO_BOUND");
        return true;
    }

    if (MR::isMsgPlayerSpinAttack(msg)) {
        return requestPunch(pSender, pReceiver);
    }

    if (MR::isMsgPlayerTrample(msg)) {
        return requestTrample(pSender, pReceiver);
    }

    return false;
}

bool PunchingKinoko::receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (isCrushed()) {
        return false;
    }

    if (pReceiver == getSensor("Body")) {
        return false;
    }

    if (MR::isMsgToEnemyAttackTrample(msg)) {
        return requestCrush();
    }

    if (MR::isMsgToEnemyAttackBlow(msg)) {
        return requestEnemyBlow(pSender, pReceiver);
    }

    return false;
}

bool PunchingKinoko::receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver) {
    if (isCrushed()) {
        return false;
    }

    if (pReceiver == getSensor("Body")) {
        return false;
    }

    // Pushed away along the floor until it moves away at 3.
    Vec3f away;
    MR::calcSensorHorizonNormalize(&away, mGravity, pSender, pReceiver);
    f32 push = away.dot(mGroundChecker->mVelocity);
    if (push < 0.0f) {
        push = cNum_receiveMsgPush_f1;
    } else {
        push = cNum_receiveMsgPush_f2 - push;
    }

    if (push > 0.0f) {
        mGroundChecker->mVelocity.add(away * push);
        return true;
    }

    return false;
}

bool PunchingKinoko::requestPunch(HitSensor* pOtherSensor, HitSensor* pMySensor) {
    if (isCrushed()) {
        return false;
    }

    if (pMySensor == getSensor("Body")) {
        return false;
    }

    if (isEnablePunched()) {
        MR::invalidateClipping(this);
        Vec3f away;
        MR::calcSensorHorizonNormalize(&away, mGravity, pOtherSensor, pMySensor);
        mGroundChecker->mVelocity.add(away * cNum_requestPunch_f1);
        MR::startSpinHitSound(this);
        MR::startBlowHitSound(this);
        MR::tryRumblePadStrong(this, PLAYER_1);
        MR::stopScene(5);
        setNerve(GET_NERVE(PunchingKinoko, PunchingKinokoNrvPunched));

        return true;
    }

    return false;
}

bool PunchingKinoko::requestEnemyBlow(HitSensor* pOtherSensor, HitSensor* pMySensor) {
    if (!isEnableBlowed()) {
        return false;
    }

    MR::invalidateClipping(this);
    Vec3f away;
    MR::calcSensorHorizonNormalize(&away, mGravity, pOtherSensor, pMySensor);
    mGroundChecker->mVelocity.add(away * cNum_requestEnemyBlow_f1);
    MR::startSound(this, "SE_OJ_PNC_KINOKO_HIT_SELF");
    setNerve(GET_NERVE(PunchingKinoko, PunchingKinokoNrvWait));

    return true;
}

bool PunchingKinoko::requestTrample(HitSensor* pOtherSensor, HitSensor* pMySensor) {
    if (isEnableTrample()) {
        Vec3f away;
        MR::calcSensorDirectionNormalize(&away, pOtherSensor, pMySensor);

        mGroundChecker->mVelocity.add(away * cNum_requestTrample_f1);
        return true;
    }
    return false;
}

bool PunchingKinoko::requestCrush() {
    if (isEnableCrushed()) {
        MR::invalidateClipping(this);
        setNerve(GET_NERVE(PunchingKinoko, PunchingKinokoNrvCrushed));
        return true;
    }
    return false;
}

// Pulls the head toward its rest point; returns the offset to it before the pull.
Vec3f PunchingKinoko::addSpringToRest() {
    Vec3f restPos;
    MR::calcPositionUpOffset(&restPos, this, cHeadHeight);
    Vec3f toRest = restPos - mGroundChecker->mPosition;
    MR::addVelocity(mGroundChecker, toRest * cSpringRate);
    return toRest;
}

// A 2P pointer flick faster than 5 per frame across the head knocks it along the flick, every 30 frames at most;
// the button must be pressed while pointing, not held from before.
void PunchingKinoko::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBrk(this, "Normal");
        MR::validateClipping(this);
        mIsPointerHeld = true;
    }

    addSpringToRest();
    MR::attenuateVelocity(mGroundChecker, cNum_exeWait_attenuateVelocity_1);

    if (MR::isStarPointerPointing2POnPressButton(this, nullptr, false, false)) {
        if (mStarPointerHitCoolDown == 0 && !mIsPointerHeld && MR::getStarPointerScreenSpeed(1) > cNum_exeWait_f1) {
            Vec3f flickDir(0.0f, 0.0f, 0.0f);
            if (MR::calcStarPointerWorldVelocityDirectionOnPlane(&flickDir, mGroundChecker->mPosition, mGravity, 1)) {
                mGroundChecker->mVelocity.add(flickDir * cNum_exeWait_f2);
                MR::startBlowHitSound(this);
                MR::start2PAttackAssistSound();
                MR::tryRumblePadWeak(this, PLAYER_2);
                mStarPointerHitCoolDown = 30;
                setNerve(GET_NERVE(PunchingKinoko, PunchingKinokoNrvPointSnaped));
                return;
            }
        }
        mIsPointerHeld = true;
    } else {
        mIsPointerHeld = false;
    }
    MR::vecBlend(mCameraTarget, mPosition, &mCameraTarget, cNum_exeWait_f3);
}

void PunchingKinoko::exeSwing() {
    f32 lastSpeed = MR::calcVelocityLength(mGroundChecker);
    Vec3f toRest = addSpringToRest();
    MR::attenuateVelocity(mGroundChecker, cNum_exeSwing_attenuateVelocity_1);
    f32 speed = MR::calcVelocityLength(mGroundChecker);

    // The bound sound plays as the swing passes 20 going up.
    if (lastSpeed < cNum_exeSwing_f1 && speed >= cNum_exeSwing_f2) {
        MR::startSound(this, "SE_OJ_PNC_KINOKO_BOUND");
    }

    MR::vecBlend(mCameraTarget, mPosition, &mCameraTarget, cNum_exeSwing_f3);

    if (speed < cNum_exeSwing_f4 && toRest.length() < cNum_exeSwing_f5) {
        setNerve(GET_NERVE(PunchingKinoko, PunchingKinokoNrvWait));
    }
}

void PunchingKinoko::exePointSnaped() {
    MR::startLevelSound(this, "SE_OJ_LV_PNC_KINOKO_PUNCHED");
    addVelocityKeepHeight();
    MR::attenuateVelocity(mGroundChecker, cNum_exePointSnaped_attenuateVelocity_1);
    if (MR::isGreaterStep(this, cStep_exePointSnaped_1)) {
        setNerve(GET_NERVE(PunchingKinoko, PunchingKinokoNrvSwing));
    }
}

void PunchingKinoko::exePunched() {
    MR::startLevelSound(this, "SE_OJ_LV_PNC_KINOKO_PUNCHED");
    addVelocityKeepHeight();
    MR::attenuateVelocity(mGroundChecker, cNum_exePunched_attenuateVelocity_1);
    MR::sendMsgEnemyAttackToBindedSensor(mGroundChecker, getSensor("Head"));
    mCameraTarget.set(mGroundChecker->mPosition);

    if (MR::isGreaterStep(this, cStep_exePunched_1)) {
        setNerve(GET_NERVE(PunchingKinoko, PunchingKinokoNrvPunchedBrake));
    }
}

// Swings back after 40 steps, or as soon as the head hits something it is bound to.
void PunchingKinoko::exePunchedBrake() {
    addVelocityKeepHeight();
    MR::attenuateVelocity(mGroundChecker, cNum_exePunchedBrake_attenuateVelocity_1);
    MR::startLevelSound(this, "SE_OJ_LV_PNC_KINOKO_PUNCHED");
    mCameraTarget.set(mGroundChecker->mPosition);
    if (!MR::isGreaterStep(this, cStep_exePunchedBrake_1) && !MR::sendMsgEnemyAttackToBindedSensor(mGroundChecker, getSensor("Head"))) {
        return;
    }
    MR::startSound(this, "SE_OJ_PNC_KINOKO_RETURN");
    setNerve(GET_NERVE(PunchingKinoko, PunchingKinokoNrvSwing));
}

void PunchingKinoko::exeHitted() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_OJ_PNC_KINOKO_HIT_SELF");
    }
    addVelocityKeepHeight();
    MR::attenuateVelocity(mGroundChecker, cNum_exeHitted_attenuateVelocity_1);
    if (MR::isGreaterStep(this, cStep_exeHitted_1)) {
        setNerve(GET_NERVE(PunchingKinoko, PunchingKinokoNrvWait));
    }
}

// The head drops onto the ground under it, 20 above, and squashes along the ground's normal.
void PunchingKinoko::exeCrushed() {
    if (MR::isFirstStep(this)) {
        MR::startBrk(this, "Press");

        if (MR::isShadowProjected(this, "頭")) {
            MR::getShadowProjectionNormal(this, "頭", &mShadowNormal);
            MR::getShadowProjectionPos(this, "頭", &mGroundChecker->mPosition);
            mGroundChecker->mPosition.add(mShadowNormal * cNum_exeCrushed_f1);
        } else {
            mShadowNormal.set(mGravity);
        }

        MR::offBind(mGroundChecker);
        mScaleController->startCrush();
        MR::startSound(this, "SE_OJ_PNC_KINOKO_CRASH");
    }

    MR::zeroVelocity(mGroundChecker);

    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(3))) {
        setNerve(GET_NERVE(PunchingKinoko, PunchingKinokoNrvCrushedEnd));
    }
}

void PunchingKinoko::exeCrushedEnd() {
    if (MR::isStep(this, cStep_exeCrushedEnd_1)) {
        MR::startBrk(this, "Revival");
        mScaleController->startAnim();
        MR::startSound(this, "SE_OJ_PNC_KINOKO_RECOVER");
    }

    addSpringToRest();
    MR::attenuateVelocity(mGroundChecker, cNum_exeCrushedEnd_attenuateVelocity_1);

    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
        setNerve(GET_NERVE(PunchingKinoko, PunchingKinokoNrvWait));
        MR::onBind(mGroundChecker);
    }
}

// Holds the head 130 above the ground under it while it flies.
void PunchingKinoko::addVelocityKeepHeight() {
    if (MR::isShadowProjected(this, "頭")) {
        Vec3f groundPos;
        MR::getShadowProjectionPos(this, "頭", &groundPos);
        MR::addVelocityKeepHeight(mGroundChecker, groundPos, cHeadHeight, cNum_addVelocityKeepHeight_f1, cNum_addVelocityKeepHeight_f2);
    }
}

bool PunchingKinoko::isEnablePunched() const {
    return isNerve(GET_NERVE(PunchingKinoko, PunchingKinokoNrvWait)) || isNerve(GET_NERVE(PunchingKinoko, PunchingKinokoNrvSwing));
}

bool PunchingKinoko::isEnableHitPlayer() const {
    return isNerve(GET_NERVE(PunchingKinoko, PunchingKinokoNrvSwing));
}

bool PunchingKinoko::isEnableEnemyAttack() const {
    return isNerve(GET_NERVE(PunchingKinoko, PunchingKinokoNrvSwing)) || isNerve(GET_NERVE(PunchingKinoko, PunchingKinokoNrvPunched)) ||
           isNerve(GET_NERVE(PunchingKinoko, PunchingKinokoNrvPunchedBrake)) || isNerve(GET_NERVE(PunchingKinoko, PunchingKinokoNrvPointSnaped));
}

bool PunchingKinoko::isEnableCrushed() const {
    return isNerve(GET_NERVE(PunchingKinoko, PunchingKinokoNrvWait)) || isNerve(GET_NERVE(PunchingKinoko, PunchingKinokoNrvCrushedEnd));
}

bool PunchingKinoko::isEnableTrample() const {
    return !isNerve(GET_NERVE(PunchingKinoko, PunchingKinokoNrvCrushed)) && !isNerve(GET_NERVE(PunchingKinoko, PunchingKinokoNrvCrushedEnd));
}

bool PunchingKinoko::isEnableBlowed() const {
    return isNerve(GET_NERVE(PunchingKinoko, PunchingKinokoNrvWait));
}

bool PunchingKinoko::isCrushed() const {
    return isNerve(GET_NERVE(PunchingKinoko, PunchingKinokoNrvCrushed));
}
