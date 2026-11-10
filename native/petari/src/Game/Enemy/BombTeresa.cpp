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
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f1, "initSensor.f1", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f2, "initSensor.f2", 85.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f3, "initSensor.f3", 85.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateNormalVelocity_f1, "updateNormalVelocity.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateNormalVelocity_f2, "updateNormalVelocity.f2", 0.6f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateDriftReleaseVelocity_f1, "updateDriftReleaseVelocity.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateDriftReleaseVelocity_f2, "updateDriftReleaseVelocity.f2", 0.6f);
NEBULA_TUNABLE_NAMED(f32, cNum_rootTongueMtxCallBack_f1, "rootTongueMtxCallBack.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_rootTongueMtxCallBack_f2, "rootTongueMtxCallBack.f2", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_rootTongueMtxCallBack_f3, "rootTongueMtxCallBack.f3", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_rootTongueMtxCallBack_f4, "rootTongueMtxCallBack.f4", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_endTongueMtxCallBack_f1, "endTongueMtxCallBack.f1", 35.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_endTongueMtxCallBack_f2, "endTongueMtxCallBack.f2", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_endTongueMtxCallBack_f3, "endTongueMtxCallBack.f3", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_endTongueMtxCallBack_f4, "endTongueMtxCallBack.f4", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveMsgPush_f1, "receiveMsgPush.f1", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveMsgEnemyAttack_f1, "receiveMsgEnemyAttack.f1", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryChaseEnd_f1, "tryChaseEnd.f1", 2000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryDirectTackle_f1, "tryDirectTackle.f1", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeShadowAppear_f1, "exeShadowAppear.f1", 0.995f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeShadowAppear_f2, "exeShadowAppear.f2", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeShadowAppear_f3, "exeShadowAppear.f3", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeShadowAppear_f4, "exeShadowAppear.f4", 0.6f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeBallAppear_f1, "exeBallAppear.f1", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeBallAppear_f2, "exeBallAppear.f2", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeBallAppear_f3, "exeBallAppear.f3", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWander_f1, "exeWander.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWander_f2, "exeWander.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWander_f3, "exeWander.f3", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeChase_f1, "exeChase.f1", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDirectTackleSign_f1, "exeDirectTackleSign.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDirectTackleSign_f2, "exeDirectTackleSign.f2", 0.6f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDirectTackleSign_f3, "exeDirectTackleSign.f3", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDirectTackle_f1, "exeDirectTackle.f1", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDirectTackle_f2, "exeDirectTackle.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDirectTackle_f3, "exeDirectTackle.f3", 0.6f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDirectTackle_f4, "exeDirectTackle.f4", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDrift_f1, "exeDrift.f1", 7.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDrift_f2, "exeDrift.f2", 7.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDrift_f3, "exeDrift.f3", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDrift_f4, "exeDrift.f4", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDrift_f5, "exeDrift.f5", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDrift_f6, "exeDrift.f6", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDrift_f7, "exeDrift.f7", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDrift_f8, "exeDrift.f8", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDrift_f9, "exeDrift.f9", 0.15f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDrift_f10, "exeDrift.f10", 7.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDrift_f11, "exeDrift.f11", 0.15f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDrift_f12, "exeDrift.f12", 7.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDrift_f13, "exeDrift.f13", 7.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDrift_f14, "exeDrift.f14", 0.15f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDrift_f15, "exeDrift.f15", 7.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDrift_f16, "exeDrift.f16", 0.992f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDriftRelease_f1, "exeDriftRelease.f1", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_addTeresaSpinPullVelocity_f1, "addTeresaSpinPullVelocity.f1", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_addTeresaSpinPullVelocity_f2, "addTeresaSpinPullVelocity.f2", 100.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_updateNormalVelocity_addVelocityKeepHeightUseShadow_1, "updateNormalVelocity.addVelocityKeepHeightUseShadow.1", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateNormalVelocity_addVelocityKeepHeightUseShadow_2, "updateNormalVelocity.addVelocityKeepHeightUseShadow.2", 0.4f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateNormalVelocity_addVelocityKeepHeightUseShadow_3, "updateNormalVelocity.addVelocityKeepHeightUseShadow.3", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateNormalVelocity_attenuateVelocity_1, "updateNormalVelocity.attenuateVelocity.1", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateNormalVelocity_reboundVelocityFromCollision_1, "updateNormalVelocity.reboundVelocityFromCollision.1", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateNormalVelocity_reboundVelocityFromCollision_2, "updateNormalVelocity.reboundVelocityFromCollision.2", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateNormalVelocity_reboundVelocityFromCollision_3, "updateNormalVelocity.reboundVelocityFromCollision.3", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateDriftReleaseVelocity_addVelocityKeepHeightUseShadow_1, "updateDriftReleaseVelocity.addVelocityKeepHeightUseShadow.1", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateDriftReleaseVelocity_addVelocityKeepHeightUseShadow_2, "updateDriftReleaseVelocity.addVelocityKeepHeightUseShadow.2", 0.4f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateDriftReleaseVelocity_addVelocityKeepHeightUseShadow_3, "updateDriftReleaseVelocity.addVelocityKeepHeightUseShadow.3", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateDriftReleaseVelocity_attenuateVelocity_1, "updateDriftReleaseVelocity.attenuateVelocity.1", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateDriftReleaseVelocity_reboundVelocityFromCollision_1, "updateDriftReleaseVelocity.reboundVelocityFromCollision.1", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateDriftReleaseVelocity_reboundVelocityFromCollision_2, "updateDriftReleaseVelocity.reboundVelocityFromCollision.2", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateDriftReleaseVelocity_reboundVelocityFromCollision_3, "updateDriftReleaseVelocity.reboundVelocityFromCollision.3", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryDirectTackle_isNearPlayer_1, "tryDirectTackle.isNearPlayer.1", 900.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeShadowAppear_addVelocityToGravity_1, "exeShadowAppear.addVelocityToGravity.1", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeShadowAppear_turnDirectionToPlayerDegree_1, "exeShadowAppear.turnDirectionToPlayerDegree.1", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeBallAppear_addVelocityToGravity_1, "exeBallAppear.addVelocityToGravity.1", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeBallAppear_reboundVelocityFromCollision_1, "exeBallAppear.reboundVelocityFromCollision.1", 0.95f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeBallAppear_reboundVelocityFromCollision_2, "exeBallAppear.reboundVelocityFromCollision.2", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeBallAppear_reboundVelocityFromCollision_3, "exeBallAppear.reboundVelocityFromCollision.3", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeBallAppear_attenuateVelocity_1, "exeBallAppear.attenuateVelocity.1", 0.99f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_isNearPlayerAnyTime_1, "exeWait.isNearPlayerAnyTime.1", 1800.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWander_isNearPlayerAnyTime_1, "exeWander.isNearPlayerAnyTime.1", 1800.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeChase_turnDirectionToPlayerDegree_1, "exeChase.turnDirectionToPlayerDegree.1", 1.4f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDirectTackleSign_turnDirectionToPlayerDegree_1, "exeDirectTackleSign.turnDirectionToPlayerDegree.1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDirectTackle_turnDirectionToPlayerDegree_1, "exeDirectTackle.turnDirectionToPlayerDegree.1", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeShock_attenuateVelocity_1, "exeShock.attenuateVelocity.1", 0.98f);
NEBULA_TUNABLE_NAMED(f32, cNum_addTeresaSpinPullVelocity_addVelocityMoveToTarget_1, "addTeresaSpinPullVelocity.addVelocityMoveToTarget.1", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_addTeresaSpinPullVelocity_addVelocityMoveToTarget_2, "addTeresaSpinPullVelocity.addVelocityMoveToTarget.2", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_addTeresaSpinPullVelocity_addVelocityMoveToTarget_3, "addTeresaSpinPullVelocity.addVelocityMoveToTarget.3", 150.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeShadowAppear_1, "exeShadowAppear.1", 130);
NEBULA_TUNABLE_NAMED(s32, cStep_exeBallAppear_1, "exeBallAppear.1", 160);
NEBULA_TUNABLE_NAMED(s32, cStep_exeBallAppear_2, "exeBallAppear.2", 70);
NEBULA_TUNABLE_NAMED(s32, cStep_exeAttackTongueFailed_1, "exeAttackTongueFailed.1", 40);
NEBULA_TUNABLE_NAMED(s32, cStep_exeDrift_1, "exeDrift.1", 190);
NEBULA_TUNABLE_NAMED(s32, cStep_exeDrift_2, "exeDrift.2", 190);
NEBULA_TUNABLE_NAMED(s32, cStep_exeDrift_3, "exeDrift.3", 190);
NEBULA_TUNABLE_NAMED(s32, cStep_exeExplosion_1, "exeExplosion.1", 20);
NEBULA_TUNABLE_NAMED(s32, cStep_exeShock_1, "exeShock.1", 20);
NEBULA_TUNABLE_NAMED(s32, cStep_exeDisperse_1, "exeDisperse.1", 40);
NEBULA_TUNABLE_NAMED(s32, cStep_isEnableHitExplosionToPlayer_1, "isEnableHitExplosionToPlayer.1", 190);

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
}

void BombTeresa::init(const TableIter& rIter) {
    initFromPlacementParam(rIter);
    initModelManagerWithAnm("BombTeresa", 0, 0);
    MR::connectToSceneEnemy(this);
    MR::initLightCtrl(this);
    initSensor();
    f32 scale = mScale.x;
    initBinder(scale * cNum_init_f1, 0.0f, 0);
    initEffectKeeper(1, nullptr);
    initSound(8, false);
    MR::initShadowVolumeSphere(this, cNum_init_f2);
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
    MR::addHitSensorPosEye(this, "tungue", 8, (mScaleXTemp * cNum_initSensor_f1), &mTonguePos, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorEnemy(this, "body", 8, (mScaleXTemp * cNum_initSensor_f2), Vec3f(0.0f, 0.0f, 0.0f));
    MR::initStarPointerTarget(this, (mScaleXTemp * cNum_initSensor_f3), Vec3f(0.0f, 0.0f, 0.0f));
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
    MR::addVelocityKeepHeightUseShadow(this, cNum_updateNormalVelocity_addVelocityKeepHeightUseShadow_1, cNum_updateNormalVelocity_addVelocityKeepHeightUseShadow_2, cNum_updateNormalVelocity_addVelocityKeepHeightUseShadow_3, nullptr);
    MR::attenuateVelocity(this, cNum_updateNormalVelocity_attenuateVelocity_1);
    MR::reboundVelocityFromCollision(this, cNum_updateNormalVelocity_reboundVelocityFromCollision_1, cNum_updateNormalVelocity_reboundVelocityFromCollision_2, cNum_updateNormalVelocity_reboundVelocityFromCollision_3);
    MR::blendQuatUpFront(&mPoseQuat, -mGravity, mFront, cNum_updateNormalVelocity_f1, cNum_updateNormalVelocity_f2);
}

void BombTeresa::updateDriftReleaseVelocity() {
    MR::addVelocityKeepHeightUseShadow(this, cNum_updateDriftReleaseVelocity_addVelocityKeepHeightUseShadow_1, cNum_updateDriftReleaseVelocity_addVelocityKeepHeightUseShadow_2, cNum_updateDriftReleaseVelocity_addVelocityKeepHeightUseShadow_3, nullptr);
    MR::attenuateVelocity(this, cNum_updateDriftReleaseVelocity_attenuateVelocity_1);
    MR::reboundVelocityFromCollision(this, cNum_updateDriftReleaseVelocity_reboundVelocityFromCollision_1, cNum_updateDriftReleaseVelocity_reboundVelocityFromCollision_2, cNum_updateDriftReleaseVelocity_reboundVelocityFromCollision_3);
    MR::blendQuatUpFront(&mPoseQuat, -mGravity, mFront, cNum_updateDriftReleaseVelocity_f1, cNum_updateDriftReleaseVelocity_f2);
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

    MR::turnMtxToXDirRate(pMtx, dir, (cNum_rootTongueMtxCallBack_f1 * mTongueRate));
    f32 dist = MR::getEaseInOutValue(MR::normalize(pos.distance(tonguePos), cNum_rootTongueMtxCallBack_f2, cNum_rootTongueMtxCallBack_f3), 1.0f, cNum_rootTongueMtxCallBack_f4, 1.0f);
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
    pos += side2 * -cNum_endTongueMtxCallBack_f1 * mTongueRate;
    pMtx->setTrans(pos);
    f32 dist = MR::getEaseInOutValue(MR::normalize(pos.distance(tonguePos), cNum_endTongueMtxCallBack_f2, cNum_endTongueMtxCallBack_f3), 1.0f, cNum_endTongueMtxCallBack_f4, 1.0f);
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

        mVelocity += deltaPos.multInLine(cNum_receiveMsgPush_f1);
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
        velocity.mult(cNum_receiveMsgEnemyAttack_f1);
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
    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(4)) || MR::isPlayerElementModeTeresa() || isOutOfRange(cNum_tryChaseEnd_f1)) {
        setNerve(GET_NERVE(BombTeresa, BombTeresaNrvWait));
        return true;
    }

    return false;
}

bool BombTeresa::tryDirectTackle() {
    if (!MR::isPlayerDamaging() && MR::isNearPlayer(this, cNum_tryDirectTackle_isNearPlayer_1) && MR::isFaceToPlayerHorizontalDegree(this, mFront, cNum_tryDirectTackle_f1)) {
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
        MR::addVelocityToGravity(this, cNum_exeShadowAppear_addVelocityToGravity_1);
    }

    MR::attenuateVelocity(this, MR::isLessStep(this, cStep_exeShadowAppear_1) ? cNum_exeShadowAppear_f1 : cNum_exeShadowAppear_f2);
    MR::turnDirectionToPlayerDegree(this, &mFront, cNum_exeShadowAppear_turnDirectionToPlayerDegree_1);
    mTonguePos = mPosition;
    MR::blendQuatUpFront(&mPoseQuat, -mGravity, mFront, cNum_exeShadowAppear_f3, cNum_exeShadowAppear_f4);
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
    MR::addVelocityToGravity(this, cNum_exeBallAppear_addVelocityToGravity_1);
    bool isBounced = MR::reboundVelocityFromCollision(this, cNum_exeBallAppear_reboundVelocityFromCollision_1, cNum_exeBallAppear_reboundVelocityFromCollision_2, cNum_exeBallAppear_reboundVelocityFromCollision_3);
    MR::attenuateVelocity(this, cNum_exeBallAppear_attenuateVelocity_1);
    if (isBounced) {
        MR::startSound(this, "SE_EM_BOMBTERE_BOUND");
    }

    if (MR::isLessStep(this, cStep_exeBallAppear_1)) {
        MR::rotateQuatRollBall(&mPoseQuat, mVelocity, -mGravity, cNum_exeBallAppear_f1);
        mPoseQuat.getZDir(mFront);
    } else {
        mFront.orthogonalize(mGravity);
        if (MR::normalizeOrZero(&mFront)) {
            MR::makeAxisVerticalZX(&mFront, mGravity);
        }

        MR::blendQuatUpFront(&mPoseQuat, -mGravity, mFront, cNum_exeBallAppear_f2, cNum_exeBallAppear_f3);
    }

    if (MR::isGreaterStep(this, cStep_exeBallAppear_2)) {
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
    } else if (MR::isNearPlayerAnyTime(this, cNum_exeWait_isNearPlayerAnyTime_1)) {
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

    MR::rotateDirectionGravityDegree(this, &mFront, getNerveStep() % 600 < 400 ? cNum_exeWander_f1 : -cNum_exeWander_f2);
    MR::addVelocityMoveToDirection(this, mFront, cNum_exeWander_f3);
    updateNormalVelocity();
    if (MR::isInDeath(this, Vec3f(0.0f, 0.0f, 0.0f))) {
        tryRevival();
    } else if (MR::isNearPlayerAnyTime(this, cNum_exeWander_isNearPlayerAnyTime_1)) {
        setNerve(GET_NERVE(BombTeresa, BombTeresaNrvChase));
    }
}

void BombTeresa::exeChase() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Wait");
        MR::startSound(this, "SE_EV_BOMBTERE_LAUGH");
        mTongueRate = 0.0f;
    }

    MR::turnDirectionToPlayerDegree(this, &mFront, cNum_exeChase_turnDirectionToPlayerDegree_1);
    MR::addVelocityMoveToDirection(this, mFront, (cNum_exeChase_f1 * MR::calcNerveRate(this, NebulaFrameClock::stepsOfSeconds(1))));
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
    if (MR::isGreaterStep(this, cStep_exeAttackTongueFailed_1)) {
        setNerve(GET_NERVE(BombTeresa, BombTeresaNrvWait));
    }
}

void BombTeresa::exeDirectTackleSign() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_EV_BOMBTERE_TACKLE");
        MR::startAction(this, "TackleSign");
        mTongueRate = 0.0f;
    }

    MR::turnDirectionToPlayerDegree(this, &mFront, cNum_exeDirectTackleSign_turnDirectionToPlayerDegree_1);
    MR::addVelocityKeepHeight(this, *MR::getPlayerCenterPos(), cNum_exeDirectTackleSign_f1, cNum_exeDirectTackleSign_f2, cNum_exeDirectTackleSign_f3);
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
    MR::turnDirectionToPlayerDegree(this, &mFront, cNum_exeDirectTackle_turnDirectionToPlayerDegree_1);
    MR::addVelocityMoveToDirection(this, mFront, (cNum_exeDirectTackle_f1 * MR::calcNerveRate(this, 50)));
    MR::addVelocityKeepHeight(this, *MR::getPlayerCenterPos(), cNum_exeDirectTackle_f2, cNum_exeDirectTackle_f3, cNum_exeDirectTackle_f4);
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
        mDriftPullSpeed = cNum_exeDrift_f1;
        mDriftSpinSpeed = cNum_exeDrift_f2;
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
    if (side.dot(viewSide) >= cNum_exeDrift_f3) {
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
        handPos.scaleAdd(cNum_exeDrift_f4, handDir, handPos);
    }

    MR::vecBlend(mTonguePos, handPos, &mTonguePos, MR::calcNerveEaseInRate(this, 15));
    MR::turnDirectionToTarget(this, &mFront, *MR::getPlayerPos(), 0.0f);
    if (mDriftStep <= 0) {
        if (MR::isSpin(PLAYER_1)) {
            MR::tryRumblePadMiddle(this, PLAYER_1);
            mDriftStep = 40;
            mDriftPullSpeed += cNum_exeDrift_f5;
            mDriftSpinSpeed += cNum_exeDrift_f6;
            if (MR::isGreaterStep(this, cStep_exeDrift_1)) {
                mIsDriftRelease = true;
            }
        }
    }

    addTeresaSpinPullVelocity(MR::calcNerveEaseInOutValue(this, 190, NebulaFrameClock::stepsOfSeconds(4), cNum_exeDrift_f7, cNum_exeDrift_f8));
    updateNormalVelocity();
    if (MR::isStep(this, cStep_exeDrift_2)) {
        MR::startBrk(this, "CountDown");
    }

    mDriftSpinSpeed = MR::getInterpolateValue(cNum_exeDrift_f9, mDriftSpinSpeed, cNum_exeDrift_f10);
    if (MR::isLessStep(this, cStep_exeDrift_3)) {
        mDriftPullSpeed = MR::getInterpolateValue(cNum_exeDrift_f11, mDriftPullSpeed, cNum_exeDrift_f12);
    } else {
        if (mDriftPullSpeed > cNum_exeDrift_f13) {
            mDriftPullSpeed = MR::getInterpolateValue(cNum_exeDrift_f14, mDriftPullSpeed, cNum_exeDrift_f15);
        }

        mDriftPullSpeed *= cNum_exeDrift_f16;
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
        addTeresaSpinPullVelocity(cNum_exeDriftRelease_f1);
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
    if (MR::isGreaterStep(this, cStep_exeExplosion_1)) {
        tryRevival();
    }
}

void BombTeresa::exeShock() {
    if (MR::isFirstStep(this)) {
        MR::emitEffect(this, "SpinBlur");
        mTongueRate = 0.0f;
    }

    mTonguePos = mPosition;
    MR::attenuateVelocity(this, cNum_exeShock_attenuateVelocity_1);
    if (MR::isGreaterStep(this, cStep_exeShock_1)) {
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
    if (MR::isGreaterStep(this, cStep_exeDisperse_1)) {
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
        return MR::isGreaterStep(this, cStep_isEnableHitExplosionToPlayer_1);
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
    MR::addVelocityMoveToTarget(this, centerPos, cNum_addTeresaSpinPullVelocity_addVelocityMoveToTarget_1, mDriftSpinSpeed, cNum_addTeresaSpinPullVelocity_addVelocityMoveToTarget_2, cNum_addTeresaSpinPullVelocity_addVelocityMoveToTarget_3);
    MR::addVelocityKeepHeight(this, centerPos, height, cNum_addTeresaSpinPullVelocity_f1, cNum_addTeresaSpinPullVelocity_f2);
}

bool BombTeresa::requestDisperse() {
    if (isNerve(GET_NERVE(BombTeresa, BombTeresaNrvExplosion)) || isNerve(GET_NERVE(BombTeresa, BombTeresaNrvShock)) ||
        isNerve(GET_NERVE(BombTeresa, BombTeresaNrvDisperse))) {
        return false;
    }

    setNerve(GET_NERVE(BombTeresa, BombTeresaNrvDisperse));
    return true;
}

