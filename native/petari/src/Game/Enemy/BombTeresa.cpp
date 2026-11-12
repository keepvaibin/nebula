#include "Game/Enemy/BombTeresa.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/Enemy/WalkerStateBindStarPointer.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/System/NerveExecutor.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorStateUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointController.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include <nebula_geometry.hpp>

#include <nebula_math.h>
#include "Game/System/LumaCore.hpp"

namespace NrvBombTeresa {
    NEW_NERVE(BombTeresaNrvAppear, BombTeresa, Appear);
    NEW_NERVE(BombTeresaNrvShadowAppear, BombTeresa, ShadowAppear);
    NEW_NERVE(BombTeresaNrvBallAppear, BombTeresa, BallAppear);
    NEW_NERVE(BombTeresaNrvWait, BombTeresa, Wait);
    NEW_NERVE(BombTeresaNrvWander, BombTeresa, Wander);
    NEW_NERVE(BombTeresaNrvChase, BombTeresa, Chase);
    NEW_NERVE(BombTeresaNrvAttackTongueFailed, BombTeresa, AttackTongueFailed);
    NEW_NERVE(BombTeresaNrvDirectTackleSign, BombTeresa, DirectTackleSign);
    NEW_NERVE(BombTeresaNrvDirectTackle, BombTeresa, DirectTackle);
    NEW_NERVE(BombTeresaNrvDrift, BombTeresa, Drift);
    NEW_NERVE(BombTeresaNrvDriftRelease, BombTeresa, DriftRelease);
    NEW_NERVE_ONEND(BombTeresaNrvBindStarPointer, BombTeresa, BindStarPointer, BindStarPointer);
    NEW_NERVE(BombTeresaNrvExplosion, BombTeresa, Explosion);
    NEW_NERVE(BombTeresaNrvDisperse, BombTeresa, Disperse);
    NEW_NERVE(BombTeresaNrvShock, BombTeresa, Shock);
    NEW_NERVE(BombTeresaNrvReadyRestart, BombTeresa, ReadyRestart);
};  // namespace NrvBombTeresa

BombTeresa::BombTeresa(const char* pName)
    : LiveActor(pName), mJointDelegator(), mJointDelegator2(), mScaleController(), mBindStarPointer(), mPoseQuat(0.0f, 0.0f, 0.0f, 1.0f), mFront(0, 0, 1),
      mTonguePos(0, 0, 0), mInitPos(0, 0, 0), mTongueRate(), mDriftPullSpeed(), mDriftSpinSpeed(), mDriftStep(), mIsDriftRelease(), mDisableRespawning(),
      mIsWanderAfterWait(false) {
    lumacore::setClass(this, lumacore::kClassId< BombTeresa >);
}

void BombTeresa::init(const TableIter& rIter) {
    initFromPlacementParam(rIter);
    initModelManagerWithAnm("BombTeresa", 0, 0);
    MR::connectToSceneEnemy(this);
    MR::initLightCtrl(this);
    initSensor();
    f32 scale = mScale.x;
    initBinder(scale * 70.0f, 0.0f, 0);
    initEffectKeeper(1, nullptr);
    initSound(8, false);
    MR::initShadowVolumeSphere(this, 80.0f);
    MR::onShadowFollowHostScale(this, nullptr);
    initTongue();
    initNerve(GET_NERVE(BombTeresa, BombTeresaNrvAppear));
    mScaleController = new AnimScaleController(nullptr);
    mBindStarPointer = new WalkerStateBindStarPointer(this, mScaleController);
    if (MR::isValidInfo(rIter)) {
        if (MR::useStageSwitchReadAppear(this, rIter)) {
            MR::syncStageSwitchAppear(this);
            makeActorDead();
            return;
        }

        makeActorAppeared();
        return;
    }

    makeActorDead();
}

void BombTeresa::initAfterPlacement() {
    MR::trySetMoveLimitCollision(this);
}

void BombTeresa::initFromPlacementParam(const TableIter& rIter) {
    if (MR::isValidInfo(rIter)) {
        MR::initDefaultPos(this, rIter);
        mScale.mult(1.0f);
        MR::makeQuatAndFrontFromRotate(&mPoseQuat, &mFront, this);
        mInitPos = mPosition;
        bool isRespawning;
        MR::getPlacementArg1WithInit(rIter, &isRespawning);
        mDisableRespawning = !isRespawning;
        MR::useStageSwitchWriteDead(this, rIter);
    }
}

void BombTeresa::initTongue() {
    mJointDelegator = MR::createJointDelegator< BombTeresa >(this, this, nullptr, &BombTeresa::rootTongueMtxCallBack, "Tongue1");
    mJointDelegator2 = MR::createJointDelegatorWithNullChildFunc(this, &BombTeresa::endTongueMtxCallBack, "Tongue2");
}

void BombTeresa::initSensor() {
    f32 mScaleXTemp = mScale.x;
    initHitSensor(2);
    MR::addHitSensorPosEye(this, "tungue", 8, (mScaleXTemp * 20.0f), &mTonguePos, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorEnemy(this, "body", 8, (mScaleXTemp * 85.0f), Vec3f(0.0f, 0.0f, 0.0f));
    MR::initStarPointerTarget(this, (mScaleXTemp * 85.0f), Vec3f(0.0f, 0.0f, 0.0f));
}

void BombTeresa::makeActorAppeared() {
    MR::showModel(this);
    LiveActor::makeActorAppeared();
    MR::onCalcGravity(this);
    MR::invalidateClipping(this);
    MR::startBrk(this, "Normal");
    mScale.set(1.0f, 1.0f, 1.0f);
    mTonguePos = mPosition;
    mTongueRate = 0.0f;
    setNerve(GET_NERVE(BombTeresa, BombTeresaNrvAppear));
}

void BombTeresa::control() {
    if (isNerve(GET_NERVE(BombTeresa, BombTeresaNrvWait)) || isNerve(GET_NERVE(BombTeresa, BombTeresaNrvWander)) ||
        isNerve(GET_NERVE(BombTeresa, BombTeresaNrvChase))) {
        MR::startLevelSound(this, "SE_EM_LV_BOMBTERE_MOVE");
    }

    mScaleController->updateNerve();
    if (isEnablePointBind()) {
        if (mBindStarPointer->tryStartPointBind()) {
            setNerve(GET_NERVE(BombTeresa, BombTeresaNrvBindStarPointer));
        }
    }
}

void BombTeresa::calcAndSetBaseMtx() {
    MR::setBaseTRMtx(this, mPoseQuat);
    Vec3f scale = mScaleController->mScale * mScale;
    MR::setBaseScale(this, scale);
    mJointDelegator->registerCallBack();
    mJointDelegator2->registerCallBack();
}

void BombTeresa::updateNormalVelocity() {
    MR::addVelocityKeepHeightUseShadow(this, 150.0f, 0.4f, 40.0f, nullptr);
    MR::attenuateVelocity(this, 0.9f);
    MR::reboundVelocityFromCollision(this, 0.0f, 0.0f, 1.0f);
    MR::blendQuatUpFront(&mPoseQuat, -mGravity, mFront, 0.1f, 0.6f);
}

void BombTeresa::updateDriftReleaseVelocity() {
    MR::addVelocityKeepHeightUseShadow(this, 150.0f, 0.4f, 40.0f, nullptr);
    MR::attenuateVelocity(this, 0.9f);
    MR::reboundVelocityFromCollision(this, 0.0f, 0.0f, 1.0f);
    MR::blendQuatUpFront(&mPoseQuat, -mGravity, mFront, 0.1f, 0.6f);
}

bool BombTeresa::rootTongueMtxCallBack(Pos3f* pMtx, const JointControllerInfo& /*rInfo*/) {
    if (mTongueRate == 0.0f) {
        return false;
    }

    Vec3f pos;
    Vec3f tonguePos;
    Vec3f side;
    pMtx->getTrans(pos);
    MR::copyJointPos(this, "Tongue2", &tonguePos);
    pMtx->getXDir(side);
    Vec3f dir = tonguePos - pos;
    if (MR::normalizeOrZero(&dir)) {
        dir.set(side);
    }

    MR::turnMtxToXDirRate(pMtx, dir, (0.5f * mTongueRate));
    f32 dist = MR::getEaseInOutValue(MR::normalize(pos.distance(tonguePos), 400.0f, 1000.0f), 1.0f, 0.5f, 1.0f);
    MR::preScaleMtx(pMtx->toMtxPtr(), Vec3f(1.0f, dist, dist));
    return true;
}

bool BombTeresa::endTongueMtxCallBack(Pos3f* pMtx, const JointControllerInfo& /*rInfo*/) {
    if (mTongueRate == 0.0f) {
        return false;
    }

    Vec3f pos;
    Vec3f tonguePos;
    Vec3f side;
    pMtx->getTrans(pos);
    MR::copyJointPos(this, "Tongue1", &tonguePos);
    MR::vecBlend(pos, mTonguePos, &pos, mTongueRate);
    pMtx->getXDir(side);
    Vec3f dir = pos - tonguePos;
    if (MR::normalizeOrZero(&dir)) {
        dir.set(side);
    }

    MR::turnMtxToXDirRate(pMtx, dir, mTongueRate);
    Vec3f side2;
    pMtx->getXDir(side2);
    pos += side2 * -35.0f * mTongueRate;
    pMtx->setTrans(pos);
    f32 dist = MR::getEaseInOutValue(MR::normalize(pos.distance(tonguePos), 400.0f, 1000.0f), 1.0f, 0.5f, 1.0f);
    MR::preScaleMtx(pMtx->toMtxPtr(), Vec3f(1.0f, dist, dist));
    return true;
}

void BombTeresa::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorEye(pSender) && MR::isSensorPlayer(pReceiver)) {
        if (isTouchTongue() && !MR::sendArbitraryMsg(ACTMES_BOMB_TERESA_TONGUE_TOUCH, pReceiver, pSender)) {
            setNerve(GET_NERVE(BombTeresa, BombTeresaNrvAttackTongueFailed));
        }
    } else if (MR::isSensorEnemy(pSender)) {
        if (MR::isSensorPlayer(pReceiver)) {
            if (isEnableHitExplosionToPlayer() && MR::sendMsgEnemyAttackExplosion(pReceiver, pSender)) {
                MR::zeroVelocity(this);
                setNerve(GET_NERVE(BombTeresa, BombTeresaNrvExplosion));
            }
        } else if (isEnableHitExplosionToEnemy()) {
            if (MR::sendMsgEnemyAttackExplosion(pReceiver, pSender)) {
                MR::tryRumbleDefaultHit(this, PLAYER_1);
                MR::zeroVelocity(this);
                setNerve(GET_NERVE(BombTeresa, BombTeresaNrvExplosion));
            } else {
                MR::sendMsgPush(pReceiver, pSender);
            }
        } else {
            MR::sendMsgPush(pReceiver, pSender);
        }
    }
}

bool BombTeresa::receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::isSensorEnemy(pReceiver)) {
        return false;
    }

    if (MR::isSensorEnemy(pSender)) {
        Vec3f deltaPos = MR::getSensorPos(pReceiver) - MR::getSensorPos(pSender);

        if (MR::normalizeOrZero(&deltaPos)) {
            MR::getRandomVector(&deltaPos, 1.0);
            MR::normalizeOrZero(&deltaPos);
        }

        mVelocity += deltaPos.multInLine(3.0f);
        return true;
    }

    return false;
}

bool BombTeresa::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgStarPieceAttack(msg) && isEnableStarPieceAttack()) {
        setNerve(GET_NERVE(BombTeresa, BombTeresaNrvDisperse));
        return true;
    }

    if (MR::isMsgPlayerSpinAttack(msg)) {
        return requestDrift();
    }

    if (MR::isMsgInvincibleAttack(msg) && isEnableShockWave()) {
        setNerve(GET_NERVE(BombTeresa, BombTeresaNrvDisperse));
        return true;
    }

    return false;
}

bool BombTeresa::receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgToEnemyAttackBlow(msg) && isEnableHitExplosion()) {
        MR::zeroVelocity(this);
        setNerve(GET_NERVE(BombTeresa, BombTeresaNrvDisperse));
        return false;
    }

    if (MR::isMsgToEnemyAttackShockWave(msg) && isEnableShockWave()) {
        Vec3f away;
        MR::calcSensorDirectionNormalize(&away, pSender, pReceiver);
        Vec3f velocity(away);
        velocity.mult(20.0f);
        mVelocity.set(velocity);
        setNerve(GET_NERVE(BombTeresa, BombTeresaNrvShock));
        return false;
    }

    if (MR::isMsgExplosionAttack(msg) && isEnableHitExplosion()) {
        MR::zeroVelocity(this);
        setNerve(GET_NERVE(BombTeresa, BombTeresaNrvExplosion));
        return true;
    }

    return false;
}

bool BombTeresa::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (msg == ACTMES_DISPERSE_BOMB_TERESA && isEnableDisperse()) {
        MR::zeroVelocity(this);
        setNerve(GET_NERVE(BombTeresa, BombTeresaNrvDisperse));
        return true;
    }

    if (MR::isMsgSpinStormRange(msg)) {
        return requestDrift();
    }

    return false;
}

bool BombTeresa::requestDrift() {
    if (isEnableDrift()) {
        MR::invalidateClipping(this);
        setNerve(GET_NERVE(BombTeresa, BombTeresaNrvDrift));
        return true;
    }

    return false;
}

bool BombTeresa::tryChaseEnd() {
    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(4)) || MR::isPlayerElementModeTeresa() || isOutOfRange(2000.0f)) {
        setNerve(GET_NERVE(BombTeresa, BombTeresaNrvWait));
        return true;
    }

    return false;
}

bool BombTeresa::tryDirectTackle() {
    if (!MR::isPlayerDamaging() && MR::isNearPlayer(this, 900.0f) && MR::isFaceToPlayerHorizontalDegree(this, mFront, 30.0f)) {
        setNerve(GET_NERVE(BombTeresa, BombTeresaNrvDirectTackleSign));
        return true;
    }

    return false;
}

bool BombTeresa::tryAbortDrift() {
    if (MR::isExistMapCollision(mPosition, mTonguePos - mPosition)) {
        setNerve(GET_NERVE(BombTeresa, BombTeresaNrvAttackTongueFailed));
        return true;
    }

    return false;
}

bool BombTeresa::tryDriftRelease() {
    if (mIsDriftRelease) {
        setNerve(GET_NERVE(BombTeresa, BombTeresaNrvDriftRelease));
        return true;
    }

    return false;
}

bool BombTeresa::tryDriftReleaseEnd() {
    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
        MR::deleteEffect(this, "SpinBlur");
        setNerve(GET_NERVE(BombTeresa, BombTeresaNrvWait));
        return true;
    }

    return false;
}

bool BombTeresa::tryExplosion() {
    if (MR::isBinded(this)) {
        if (MR::sendMsgEnemyAttackExplosionToBindedSensor(this, getSensor("body"))) {
            MR::zeroVelocity(this);
            setNerve(GET_NERVE(BombTeresa, BombTeresaNrvExplosion));
            MR::forceDeleteEffect(this, "SpinBlur");
            return true;
        }
    }

    return false;
}

bool BombTeresa::tryRevival() {
    MR::startBrk(this, "Normal");
    if (mDisableRespawning) {
        MR::invalidateHitSensors(this);
        setNerve(GET_NERVE(BombTeresa, BombTeresaNrvReadyRestart));
        return true;
    }

    kill();
    return false;
}

void BombTeresa::exeAppear() {
    if (MR::isFirstStep(this)) {
        MR::validateHitSensor(this, "body");
        MR::validateHitSensor(this, "tungue");
        MR::showModel(this);
        MR::startAction(this, "Appear");
        MR::emitEffect(this, "Appear");
        MR::startSound(this, "SE_EM_BOMBTERE_APPEAR");
        mTongueRate = 0.0f;
    }

    mTonguePos = mPosition;
    updateNormalVelocity();
    if (MR::isActionEnd(this)) {
        setNerve(GET_NERVE(BombTeresa, BombTeresaNrvWait));
    }
}

void BombTeresa::exeShadowAppear() {
    if (MR::isFirstStep(this)) {
        MR::emitEffect(this, "Shadow");
        MR::hideModelAndOnCalcAnim(this);
        MR::startAction(this, "ShadowAppear");
    }

    if (!MR::isBindedGround(this)) {
        MR::addVelocityToGravity(this, 1.0f);
    }

    MR::attenuateVelocity(this, MR::isLessStep(this, 130) ? 0.995f : 0.8f);
    MR::turnDirectionToPlayerDegree(this, &mFront, 3.0f);
    mTonguePos = mPosition;
    MR::blendQuatUpFront(&mPoseQuat, -mGravity, mFront, 0.1f, 0.6f);
    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(2))) {
        MR::deleteEffect(this, "Shadow");
        setNerve(GET_NERVE(BombTeresa, BombTeresaNrvAppear));
    }
}

void BombTeresa::exeBallAppear() {
    if (MR::isFirstStep(this)) {
        MR::validateHitSensor(this, "body");
        MR::validateHitSensor(this, "tungue");
        MR::emitEffect(this, "Ball");
        MR::startAction(this, "BallAppear");
        MR::showModel(this);
        mTongueRate = 0.0f;
    }

    mTonguePos = mPosition;
    MR::addVelocityToGravity(this, 1.0f);
    bool isBounced = MR::reboundVelocityFromCollision(this, 0.95f, 2.0f, 1.0f);
    MR::attenuateVelocity(this, 0.99f);
    if (isBounced) {
        MR::startSound(this, "SE_EM_BOMBTERE_BOUND");
    }

    if (MR::isLessStep(this, 160)) {
        MR::rotateQuatRollBall(&mPoseQuat, mVelocity, -mGravity, 70.0f);
        mPoseQuat.getZDir(mFront);
    } else {
        mFront.orthogonalize(mGravity);
        if (MR::normalizeOrZero(&mFront)) {
            MR::makeAxisVerticalZX(&mFront, mGravity);
        }

        MR::blendQuatUpFront(&mPoseQuat, -mGravity, mFront, 0.1f, 0.1f);
    }

    if (MR::isGreaterStep(this, 70)) {
        MR::deleteEffect(this, "Ball");
        MR::emitEffect(this, "Appear");
        MR::startSound(this, "SE_EM_BOMBTERE_APPEAR");
        MR::showModel(this);
        setNerve(GET_NERVE(BombTeresa, BombTeresaNrvDirectTackleSign));
    }
}

void BombTeresa::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::validateClipping(this);
        MR::startAction(this, "Wait");
        MR::startBrk(this, "Normal");
        mTongueRate = 0.0f;
    }

    mTonguePos = mPosition;
    updateNormalVelocity();
    if (MR::isInDeath(this, Vec3f(0.0f, 0.0f, 0.0f))) {
        tryRevival();
    } else if (MR::isNearPlayerAnyTime(this, 1800.0f)) {
        setNerve(GET_NERVE(BombTeresa, BombTeresaNrvChase));
    } else if (mIsWanderAfterWait) {
        if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
            setNerve(GET_NERVE(BombTeresa, BombTeresaNrvWander));
        }
    }
}

void BombTeresa::exeWander() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Wait");
        MR::startBrk(this, "Normal");
        mTongueRate = 0.0f;
    }

    mTonguePos = mPosition;

    MR::rotateDirectionGravityDegree(this, &mFront, getNerveStep() % 600 < 400 ? 0.5f : -0.5f);
    MR::addVelocityMoveToDirection(this, mFront, 0.5f);
    updateNormalVelocity();
    if (MR::isInDeath(this, Vec3f(0.0f, 0.0f, 0.0f))) {
        tryRevival();
    } else if (MR::isNearPlayerAnyTime(this, 1800.0f)) {
        setNerve(GET_NERVE(BombTeresa, BombTeresaNrvChase));
    }
}

void BombTeresa::exeChase() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Wait");
        MR::startSound(this, "SE_EV_BOMBTERE_LAUGH");
        mTongueRate = 0.0f;
    }

    MR::turnDirectionToPlayerDegree(this, &mFront, 1.4f);
    MR::addVelocityMoveToDirection(this, mFront, (0.9f * MR::calcNerveRate(this, NebulaFrameClock::stepsOfSeconds(1))));
    mTonguePos = mPosition;
    updateNormalVelocity();
    if (!tryChaseEnd() && tryDirectTackle()) {
        return;
    }
}

void BombTeresa::exeAttackTongueFailed() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Return");
    }

    MR::startLevelSound(this, "SE_EM_LV_BOMBTERE_TONGUE_BACK");
    mTongueRate = MR::calcNerveEaseInOutValue(this, 0, 1.0f, 0.0f);
    updateNormalVelocity();
    if (MR::isGreaterStep(this, 40)) {
        setNerve(GET_NERVE(BombTeresa, BombTeresaNrvWait));
    }
}

void BombTeresa::exeDirectTackleSign() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_EV_BOMBTERE_TACKLE");
        MR::startAction(this, "TackleSign");
        mTongueRate = 0.0f;
    }

    MR::turnDirectionToPlayerDegree(this, &mFront, 10.0f);
    MR::addVelocityKeepHeight(this, *MR::getPlayerCenterPos(), 100.0f, 0.6f, 100.0f);
    updateNormalVelocity();
    mTonguePos = mPosition;
    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(BombTeresa, BombTeresaNrvDirectTackle));
    }
}

void BombTeresa::exeDirectTackle() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Tackle");
        mTongueRate = 0.0f;
    }

    MR::startLevelSound(this, "SE_EM_LV_BOMBTERE_TACKLE");
    MR::turnDirectionToPlayerDegree(this, &mFront, 0.8f);
    MR::addVelocityMoveToDirection(this, mFront, (1.5f * MR::calcNerveRate(this, 50)));
    MR::addVelocityKeepHeight(this, *MR::getPlayerCenterPos(), 100.0f, 0.6f, 100.0f);
    updateNormalVelocity();
    mTonguePos = mPosition;
    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(2))) {
        setNerve(GET_NERVE(BombTeresa, BombTeresaNrvWander));
    }
}

void BombTeresa::exeDrift() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Spin");
        MR::startBrk(this, "Normal");
        MR::emitEffect(this, "SpinBlur");
        MR::startSound(this, "SE_EM_BOMBTERE_TONGUE_CATCH");
        mDriftStep = 40;
        mIsDriftRelease = false;
        mDriftPullSpeed = 7.0f;
        mDriftSpinSpeed = 7.0f;
        mTongueRate = 1.0f;
    }

    // The swing sound plays while it swings across the view (its side close to the camera's).
    Vec3f side;
    side.cross(mGravity, mFront);
    Vec3f viewSide;
    viewSide.cross(mGravity, *MR::getPlayerPos() - MR::getCamPos());

    if (MR::isNearZero(side)) {
        MR::makeAxisVerticalZX(&side, mGravity);
    }

    if (MR::isNearZero(viewSide)) {
        MR::makeAxisVerticalZX(&viewSide, mGravity);
    }

    MR::normalize(&side);
    MR::normalize(&viewSide);
    if (side.dot(viewSide) >= 0.8f) {
        MR::startLevelSound(this, "SE_EM_LV_BOMBTERE_SWING");
    }

    // The tongue end eases into the player's right hand, 20 along it.
    Pos3f handMtx;
    MR::calcPlayerJointMtx(&handMtx, "HandR");
    Vec3f handPos;
    handMtx.getTrans(handPos);
    Vec3f handDir;
    handMtx.getZDir(handDir);
    if (!MR::normalizeOrZero(&handDir)) {
        handPos.scaleAdd(20.0f, handDir, handPos);
    }

    MR::vecBlend(mTonguePos, handPos, &mTonguePos, MR::calcNerveEaseInRate(this, 15));
    MR::turnDirectionToTarget(this, &mFront, *MR::getPlayerPos(), 0.0f);
    if (mDriftStep <= 0) {
        if (MR::isSpin(PLAYER_1)) {
            MR::tryRumblePadMiddle(this, PLAYER_1);
            mDriftStep = 40;
            mDriftPullSpeed += 20.0f;
            mDriftSpinSpeed += 20.0f;
            if (MR::isGreaterStep(this, 190)) {
                mIsDriftRelease = true;
            }
        }
    }

    addTeresaSpinPullVelocity(MR::calcNerveEaseInOutValue(this, 190, NebulaFrameClock::stepsOfSeconds(4), 150.0f, 70.0f));
    updateNormalVelocity();
    if (MR::isStep(this, 190)) {
        MR::startBrk(this, "CountDown");
    }

    mDriftSpinSpeed = MR::getInterpolateValue(0.15f, mDriftSpinSpeed, 7.0f);
    if (MR::isLessStep(this, 190)) {
        mDriftPullSpeed = MR::getInterpolateValue(0.15f, mDriftPullSpeed, 7.0f);
    } else {
        if (mDriftPullSpeed > 7.0f) {
            mDriftPullSpeed = MR::getInterpolateValue(0.15f, mDriftPullSpeed, 7.0f);
        }

        mDriftPullSpeed *= 0.992f;
    }

    mDriftStep--;
    if (!tryExplosion() && !tryDriftRelease() && tryAbortDrift()) {
        return;
    }
}

void BombTeresa::exeDriftRelease() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Drift");
        MR::tryRumblePadStrong(this, PLAYER_1);
        MR::startSound(this, "SE_EM_BOMBTERE_THROW");
    }

    mTongueRate = MR::calcNerveEaseInOutValue(this, 12, 1.0f, 0.0f);
    if (MR::isLessStep(this, 0)) {
        addTeresaSpinPullVelocity(150.0f);
        updateNormalVelocity();
    } else {
        updateDriftReleaseVelocity();
    }

    if (!tryExplosion() && tryDriftReleaseEnd()) {
        return;
    }
}

void BombTeresa::exeBindStarPointer() {
    MR::updateActorStateAndNextNerve(this, mBindStarPointer, GET_NERVE(BombTeresa, BombTeresaNrvWait));
}

void BombTeresa::endBindStarPointer() {
    mBindStarPointer->kill();
}

void BombTeresa::exeExplosion() {
    if (MR::isFirstStep(this)) {
        MR::shakeCameraNormalStrong();
        MR::tryRumblePadStrong(this, PLAYER_1);
        MR::deleteEffect(this, "SpinBlur");
        MR::emitEffect(this, "Explosion");
        MR::hideModel(this);
        MR::startSound(this, "SE_EM_BOMBTERE_EXPLOSION");
        mTongueRate = 0.0f;
        MR::zeroVelocity(this);
    }

    mTonguePos = mPosition;
    if (MR::isGreaterStep(this, 20)) {
        tryRevival();
    }
}

void BombTeresa::exeShock() {
    if (MR::isFirstStep(this)) {
        MR::emitEffect(this, "SpinBlur");
        mTongueRate = 0.0f;
    }

    mTonguePos = mPosition;
    MR::attenuateVelocity(this, 0.98f);
    if (MR::isGreaterStep(this, 20)) {
        MR::deleteEffect(this, "SpinBlur");
        MR::emitEffect(this, "MisFire");
        MR::startSound(this, "SE_EM_BOMBTERE_HIDE");
        MR::hideModel(this);
        tryRevival();
    }
}

void BombTeresa::exeDisperse() {
    if (MR::isFirstStep(this)) {
        MR::deleteEffect(this, "SpinBlur");
        MR::emitEffect(this, "MisFire");
        MR::startSound(this, "SE_EM_BOMBTERE_HIDE");
        MR::hideModel(this);
        mTongueRate = 0.0f;
        MR::zeroVelocity(this);
    }

    mTonguePos = mPosition;
    if (MR::isGreaterStep(this, 40)) {
        tryRevival();
    }
}

void BombTeresa::exeReadyRestart() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Hide");
        MR::resetPosition(this, mInitPos);
        MR::makeQuatAndFrontFromRotate(&mPoseQuat, &mFront, this);
        MR::zeroVelocity(this);
    }

    mTonguePos = mPosition;
    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(2))) {
        setNerve(GET_NERVE(BombTeresa, BombTeresaNrvAppear));
    }
}

bool BombTeresa::isTouchTongue() const {
    return isNerve(GET_NERVE(BombTeresa, BombTeresaNrvDrift));
}

bool BombTeresa::isEnableDrift() const {
    if (isNerve(GET_NERVE(BombTeresa, BombTeresaNrvBallAppear)) || isNerve(GET_NERVE(BombTeresa, BombTeresaNrvShadowAppear)) ||
        isNerve(GET_NERVE(BombTeresa, BombTeresaNrvDrift)) || isNerve(GET_NERVE(BombTeresa, BombTeresaNrvDriftRelease)) ||
        isNerve(GET_NERVE(BombTeresa, BombTeresaNrvExplosion)) || isNerve(GET_NERVE(BombTeresa, BombTeresaNrvShock)) ||
        isNerve(GET_NERVE(BombTeresa, BombTeresaNrvDisperse)) || isNerve(GET_NERVE(BombTeresa, BombTeresaNrvReadyRestart))) {
        return false;
    }

    return true;
}

bool BombTeresa::isEnablePointBind() const {
    if (isNerve(GET_NERVE(BombTeresa, BombTeresaNrvWait)) || isNerve(GET_NERVE(BombTeresa, BombTeresaNrvWander)) ||
        isNerve(GET_NERVE(BombTeresa, BombTeresaNrvChase)) || isNerve(GET_NERVE(BombTeresa, BombTeresaNrvAttackTongueFailed)) ||
        isNerve(GET_NERVE(BombTeresa, BombTeresaNrvDirectTackleSign)) || isNerve(GET_NERVE(BombTeresa, BombTeresaNrvDirectTackle)) ||
        isNerve(GET_NERVE(BombTeresa, BombTeresaNrvDriftRelease))) {
        return true;
    }

    return false;
}

bool BombTeresa::isEnableDisperse() const {
    if (isNerve(GET_NERVE(BombTeresa, BombTeresaNrvDrift))) {
        return false;
    }

    return isEnableDrift();
}

bool BombTeresa::isEnableHitExplosionToPlayer() const {
    if (isNerve(GET_NERVE(BombTeresa, BombTeresaNrvDrift))) {
        return MR::isGreaterStep(this, 190);
    }

    return isEnableHitExplosion();
}

bool BombTeresa::isEnableHitExplosionToEnemy() const {
    if (isNerve(GET_NERVE(BombTeresa, BombTeresaNrvDrift)) || isNerve(GET_NERVE(BombTeresa, BombTeresaNrvDriftRelease))) {
        return true;
    }

    return false;
}

bool BombTeresa::isEnableHitExplosion() const {
    if (isNerve(GET_NERVE(BombTeresa, BombTeresaNrvBallAppear)) || isNerve(GET_NERVE(BombTeresa, BombTeresaNrvShadowAppear)) ||
        isNerve(GET_NERVE(BombTeresa, BombTeresaNrvExplosion)) || isNerve(GET_NERVE(BombTeresa, BombTeresaNrvShock)) ||
        isNerve(GET_NERVE(BombTeresa, BombTeresaNrvDisperse)) || isNerve(GET_NERVE(BombTeresa, BombTeresaNrvDrift)) ||
        isNerve(GET_NERVE(BombTeresa, BombTeresaNrvReadyRestart))) {
        return false;
    }

    return true;
}

bool BombTeresa::isEnableStarPieceAttack() const {
    if (isNerve(GET_NERVE(BombTeresa, BombTeresaNrvBallAppear)) || isNerve(GET_NERVE(BombTeresa, BombTeresaNrvShadowAppear)) ||
        isNerve(GET_NERVE(BombTeresa, BombTeresaNrvExplosion)) || isNerve(GET_NERVE(BombTeresa, BombTeresaNrvShock)) ||
        isNerve(GET_NERVE(BombTeresa, BombTeresaNrvDisperse)) || isNerve(GET_NERVE(BombTeresa, BombTeresaNrvDrift)) ||
        isNerve(GET_NERVE(BombTeresa, BombTeresaNrvReadyRestart))) {
        return false;
    }

    return true;
}

bool BombTeresa::isEnableShockWave() const {
    if (isNerve(GET_NERVE(BombTeresa, BombTeresaNrvExplosion)) || isNerve(GET_NERVE(BombTeresa, BombTeresaNrvShock)) ||
        isNerve(GET_NERVE(BombTeresa, BombTeresaNrvDisperse))) {
        return false;
    }

    return true;
}

bool BombTeresa::appearNormal(const Vec3f& rPosition, const Vec3f& rVelocity) {
    if (!MR::isDead(this)) {
        return false;
    }

    setNerve(GET_NERVE(BombTeresa, BombTeresaNrvAppear));
    mPosition.set(rPosition);
    appear();
    MR::showModel(this);
    mVelocity.set(rVelocity);
    MR::invalidateClipping(this);
    MR::trySetMoveLimitCollision(this);
    return true;
}

bool BombTeresa::appearShadow(const Vec3f& rPosition, const Vec3f& rVelocity) {
    if (!appearNormal(rPosition, rVelocity)) {
        return false;
    }

    MR::invalidateHitSensors(this);
    MR::hideModelAndOnCalcAnim(this);
    setNerve(GET_NERVE(BombTeresa, BombTeresaNrvShadowAppear));
    return true;
}

// Circles the player, pulled in, at height above the player's center.
void BombTeresa::addTeresaSpinPullVelocity(f32 height) {
    Vec3f centerPos(*MR::getPlayerCenterPos());
    MR::addVelocityClockwiseToTarget(this, centerPos, mDriftPullSpeed);
    MR::addVelocityMoveToTarget(this, centerPos, 0.0f, mDriftSpinSpeed, 0.0f, 150.0f);
    MR::addVelocityKeepHeight(this, centerPos, height, 3.0f, 100.0f);
}

bool BombTeresa::requestDisperse() {
    if (isNerve(GET_NERVE(BombTeresa, BombTeresaNrvExplosion)) || isNerve(GET_NERVE(BombTeresa, BombTeresaNrvShock)) ||
        isNerve(GET_NERVE(BombTeresa, BombTeresaNrvDisperse))) {
        return false;
    }

    setNerve(GET_NERVE(BombTeresa, BombTeresaNrvDisperse));
    return true;
}

