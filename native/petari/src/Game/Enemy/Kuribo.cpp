#include "Game/Enemy/Kuribo.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/ItemGenerator.hpp"
#include "Game/Enemy/WalkerStateChase.hpp"
#include "Game/Enemy/WalkerStateFindPlayer.hpp"
#include "Game/Enemy/WalkerStateParam.hpp"
#include "Game/Enemy/WalkerStateStagger.hpp"
#include "Game/Enemy/WalkerStateWander.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/Air.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorStateUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/BaseMatrixFollowTargetHolder.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/VectorUtil.hpp"
#include <nebula_math.h>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_KuriboParam_f1, "KuriboParam.f1", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_KuriboParam_f2, "KuriboParam.f2", 0.99f);
NEBULA_TUNABLE_NAMED(f32, cNum_KuriboParam_f3, "KuriboParam.f3", 0.93f);
NEBULA_TUNABLE_NAMED(f32, cNum_KuriboParam_f4, "KuriboParam.f4", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_KuriboParam_f5, "KuriboParam.f5", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_KuriboParam_f6, "KuriboParam.f6", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_KuriboParam_f7, "KuriboParam.f7", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_KuriboParam_f8, "KuriboParam.f8", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_KuriboParam_f9, "KuriboParam.f9", 0.4f);
NEBULA_TUNABLE_NAMED(f32, cNum_KuriboParam_f10, "KuriboParam.f10", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_KuriboParam_f11, "KuriboParam.f11", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 75.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f1, "initSensor.f1", 75.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f2, "initSensor.f2", 75.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f3, "initSensor.f3", 45.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f4, "initSensor.f4", 75.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f1, "control.f1", 0.05f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f2, "control.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f3, "control.f3", 0.05f);
NEBULA_TUNABLE_NAMED(f32, cNum_requestBlowDown_f1, "requestBlowDown.f1", 36.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_requestBlowDown_f2, "requestBlowDown.f2", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAppearFromBox_f1, "exeAppearFromBox.f1", 7.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAppearFromBox_f2, "exeAppearFromBox.f2", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAttackSuccess_f1, "exeAttackSuccess.f1", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeBlowDown_f1, "exeBlowDown.f1", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcPassiveMovement_f1, "calcPassiveMovement.f1", 0.93f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcPassiveMovement_f2, "calcPassiveMovement.f2", 0.99f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcPassiveMovement_f3, "calcPassiveMovement.f3", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcPassiveMovement_f4, "calcPassiveMovement.f4", 0.7f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_receiveMsgPush_addVelocityFromPush_1, "receiveMsgPush.addVelocityFromPush.1", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryNonActive_isNearPlayerAnyTime_1, "tryNonActive.isNearPlayerAnyTime.1", 3000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryActive_isNearPlayerAnyTime_1, "tryActive.isNearPlayerAnyTime.1", 3000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcPassiveMovement_addVelocityToGravity_1, "calcPassiveMovement.addVelocityToGravity.1", 1.5f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_tryEndBlow_1, "tryEndBlow.1", 10);
NEBULA_TUNABLE_NAMED(s32, cStep_tryEndBlowLand_1, "tryEndBlowLand.1", 15);
NEBULA_TUNABLE_NAMED(s32, cStep_exeBlow_1, "exeBlow.1", 10);
NEBULA_TUNABLE_NAMED(s32, cStep_exeHipDropDown_1, "exeHipDropDown.1", 40);
NEBULA_TUNABLE_NAMED(s32, cStep_exeFlatDown_1, "exeFlatDown.1", 20);

namespace {
    class KuriboParam {
    public:
        KuriboParam();

        WalkerStateParam mStateParam;
        WalkerStateStaggerParam mStaggerParam;
        WalkerStateFindPlayerParam mFindPlayerParam;
        WalkerStateChaseParam mChaseParam;
        WalkerStateWanderParam mWanderParam;
    };

    KuriboParam::KuriboParam() {
        mStateParam.mGravityAccel = cNum_KuriboParam_f1;
        mStateParam.mAirFriction = cNum_KuriboParam_f2;
        mStateParam.mGroundFriction = cNum_KuriboParam_f3;
        mStateParam.mPlayerNearDistance = cNum_KuriboParam_f4;
        mStateParam.mPlayerSightFanDegreeH = cNum_KuriboParam_f5;
        mStateParam.mPlayerSightFanDegreeV = cNum_KuriboParam_f6;
        mWanderParam.mSpeed = cNum_KuriboParam_f7;
        mWanderParam.mWaitTime = NebulaFrameClock::stepsOfSeconds(2);
        mWanderParam.mWalkTime = NebulaFrameClock::stepsOfSeconds(2);
        mWanderParam.mTurnMaxRateDegree = cNum_KuriboParam_f8;
        mChaseParam.mChaseSpeed = cNum_KuriboParam_f9;
        mFindPlayerParam.mTurnMaxRateDegree = cNum_KuriboParam_f10;
        mFindPlayerParam.mJumpVelocity = cNum_KuriboParam_f11;
    }

    static KuriboParam sParam;
};  // namespace

namespace NrvKuribo {
    NEW_NERVE_ONEND(KuriboNrvNonActive, Kuribo, NonActive, NonActive);
    NEW_NERVE(KuriboNrvAppearFromBox, Kuribo, AppearFromBox);
    NEW_NERVE(KuriboNrvBlow, Kuribo, Blow);
    NEW_NERVE(KuriboNrvBlowLand, Kuribo, BlowLand);
    NEW_NERVE(KuriboNrvWander, Kuribo, Wander);
    NEW_NERVE(KuriboNrvFindPlayer, Kuribo, FindPlayer);
    NEW_NERVE(KuriboNrvChase, Kuribo, Chase);
    NEW_NERVE(KuriboNrvAttackSuccess, Kuribo, AttackSuccess);
    NEW_NERVE_ONEND(KuriboNrvBindStarPointer, Kuribo, BindStarPointer, BindStarPointer);
    NEW_NERVE(KuriboNrvStagger, Kuribo, Stagger);
    NEW_NERVE(KuriboNrvFlatDown, Kuribo, FlatDown);
    NEW_NERVE(KuriboNrvHipDropDown, Kuribo, HipDropDown);
    NEW_NERVE(KuriboNrvPressDown, Kuribo, PressDown);
    NEW_NERVE(KuriboNrvBlowDown, Kuribo, BlowDown);
};  // namespace NrvKuribo

Kuribo::Kuribo(const char* pName)
    : LiveActor(pName), mScaleController(nullptr), mItemGenerator(nullptr), mStateWander(nullptr), mStateFindPlayer(nullptr),
      mBindStarPointer(nullptr), mStateStagger(nullptr), mStateChase(nullptr), mPoseQuat(0.0f, 0.0f, 0.0f, 1.0f), mFront(0, 0, 1), mIsNoGravitySupport(false), mHasItem(true) {
}

void Kuribo::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("Kuribo", nullptr, false);
    MR::connectToSceneEnemy(this);
    MR::initLightCtrl(this);
    MR::makeQuatAndFrontFromRotate(&mPoseQuat, &mFront, this);
    if (MR::isValidInfo(rIter)) {
        MR::getPlacementArg0WithInit(rIter, &mIsNoGravitySupport);
    }

    if (!mIsNoGravitySupport) {
        MR::onCalcGravity(this);
        MR::calcGravity(this);
    } else {
        MR::offCalcGravity(this);
        Vec3f up;
        MR::getUpVecFromQuat(&up, mPoseQuat);
        mGravity.set(up);
    }

    mScaleController = new AnimScaleController(nullptr);
    mItemGenerator = new ItemGenerator();
    MR::initStarPointerTarget(this, cNum_init_f1 * mScale.y, Vec3f(0.0f, cNum_init_f2 * mScale.y, 0.0f));
    initSound(4, false);
    if (MR::isEqualStageName("KoopaBattleVs2Galaxy")) {
        MR::setSeVersion(this, SeVersion_2D);
    } else {
        MR::setSeVersion(this, SeVersion_NORMAL);
    }

    initBinder((cNum_init_f3 * mScale.y), (cNum_init_f4 * mScale.y), 0);
    initEffectKeeper(1, nullptr);
    initSensor();
    MR::initShadowVolumeSphere(this, cNum_init_f5);
    initState();
    initNerve(GET_NERVE(Kuribo, KuriboNrvWander));
    initAppearState(rIter);
    MR::declareStarPiece(this, 3);
    MR::declareCoin(this, 1);
    MR::addBaseMatrixFollowTarget(this, rIter, nullptr, nullptr);
    if (MR::isValidInfo(rIter)) {
        MR::setGroupClipping(this, rIter, 32);
    }

    MR::useStageSwitchWriteDead(this, rIter);
    MR::useStageSwitchSleep(this, rIter);
    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::syncStageSwitchAppear(this);
        makeActorDead();
    } else {
        makeActorAppeared();
    }
}

void Kuribo::initAfterPlacement() {
    MR::turnQuatUpToGravity(&mPoseQuat, mPoseQuat, this);
    MR::trySetMoveLimitCollision(this);
}

void Kuribo::initState() {
    ::sParam = KuriboParam();  // again, with a mod's numbers
    mStateFindPlayer = new WalkerStateFindPlayer(this, &mFront, &::sParam.mStateParam, &::sParam.mFindPlayerParam);
    mBindStarPointer = new WalkerStateBindStarPointer(this, mScaleController);
    mStateStagger = new WalkerStateStagger(this, &mFront, &::sParam.mStateParam, &::sParam.mStaggerParam);
    mStateWander = new WalkerStateWander(this, &mFront, &::sParam.mStateParam, &::sParam.mWanderParam);
    mStateChase = new WalkerStateChase(this, &mFront, &::sParam.mStateParam, &::sParam.mChaseParam);
}

void Kuribo::initSensor() {
    f32 y_scale = mScale.y;
    initHitSensor(2);
    MR::addHitSensor(this, "body", ATYPE_KURIBO, 8, cNum_initSensor_f1 * y_scale, Vec3f(0.0f, cNum_initSensor_f2 * y_scale, 0.0f));
    MR::addHitSensor(this, "attack", ATYPE_ENEMY_ATTACK, 8, cNum_initSensor_f3 * y_scale, Vec3f(0.0f, cNum_initSensor_f4 * y_scale, 0.0f));
}

void Kuribo::initAppearState(const TableIter& rIter) {
    s32 state = -1;
    if (MR::isValidInfo(rIter)) {
        MR::getPlacementArg1NoInit(rIter, &state);
    }

    switch (state) {
    case -1:
        setNerve(GET_NERVE(Kuribo, KuriboNrvWander));
        break;
    case 0:
        setNerve(GET_NERVE(Kuribo, KuriboNrvAppearFromBox));
        mPoseQuat.getZDir(mFront);
        break;
    }
}

void Kuribo::makeActorAppeared() {
    LiveActor::makeActorAppeared();
    mItemGenerator->setTypeStarPeace(3);
}

void Kuribo::makeActorDead() {
    LiveActor::makeActorDead();
}

void Kuribo::kill() {
    if (MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
    }

    MR::emitEffect(this, "DeathSmokeKuribo");
    MR::startSoundSeVer(this, "SE_EM_EXPLODE_S");
    if (mHasItem) {
        mItemGenerator->generate(this);
    }

    LiveActor::kill();
}

void Kuribo::control() {
    mScaleController->updateNerve();
    if (mIsNoGravitySupport) {
        MR::calcGravityOrZero(this);
    }

    MR::blendQuatFromGroundAndFront(&mPoseQuat, this, mFront, cNum_control_f1, cNum_control_f2);
    if (!isNerve(GET_NERVE(Kuribo, KuriboNrvBindStarPointer))) {
        Vec3f groundMove(0, 0, 0);
        if (MR::calcVelocityAreaOrRailMoveOnGround(&groundMove, this)) {
            MR::addVelocity(this, groundMove * cNum_control_f3);
        }
    }

    if (!tryDead()) {
        if (tryPointBind()) {
            return;
        }
    }
}

void Kuribo::calcAndSetBaseMtx() {
    MR::setBaseTRMtx(this, mPoseQuat);
    Vec3f scale = mScaleController->mScale * mScale;
    MR::setBaseScale(this, scale);
}

void Kuribo::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if ((!MR::isSensorType(pSender, ATYPE_KURIBO) || ((isEnableAttack() || !MR::isSensorPlayer(pReceiver)) && !MR::isSensorEnemy(pReceiver)) ||
         !isEnablePushMove() || !MR::sendMsgPushAndKillVelocityToTarget(this, pReceiver, pSender)) &&
        isEnableAttack() && MR::isSensorPlayer(pReceiver) && MR::isSensorEnemyAttack(pSender)) {
        if (!MR::isPlayerHipDropFalling() && MR::sendMsgEnemyAttack(pReceiver, pSender)) {
            requestAttackSuccess();
        } else {
            MR::sendMsgPush(pReceiver, pSender);
        }
    }
}

bool Kuribo::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgLockOnStarPieceShoot(msg)) {
        return true;
    }

    if (MR::isMsgStarPieceAttack(msg)) {
        return requestStagger(pSender, pReceiver);
    }

    if (MR::isMsgPlayerTrample(msg)) {
        return requestFlatDown(pSender, pReceiver);
    }

    if (MR::isMsgJetTurtleAttack(msg) || MR::isMsgFireBallAttack(msg) || MR::isMsgInvincibleAttack(msg)) {
        return requestBlowDown(pSender, pReceiver);
    }

    if (MR::isMsgPlayerHipDrop(msg)) {
        return requestHipDropDown(pSender, pReceiver);
    }

    if (MR::isMsgPlayerHitAll(msg)) {
        if (!isEnableKick()) {
            return requestStagger(pSender, pReceiver);
        }

        return requestBlowDown(pSender, pReceiver);
    }

    return false;
}

bool Kuribo::receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgToEnemyAttackShockWave(msg)) {
        return requestStagger(pSender, pReceiver);
    }

    if (MR::isMsgToEnemyAttackTrample(msg)) {
        return requestPressDown();
    }

    if (MR::isMsgExplosionAttack(msg)) {
        return requestBlowDown(pSender, pReceiver);
    }

    if (MR::isMsgToEnemyAttackBlow(msg)) {
        return requestBlowDown(pSender, pReceiver);
    }

    return false;
}

bool Kuribo::receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorEnemyAttack(pReceiver)) {
        return false;
    }

    if (MR::isSensorEnemy(pSender) || (MR::isSensorRide(pSender) || (!isEnableAttack() && MR::isSensorPlayer(pSender)))) {
        if (isEnablePushMove()) {
            MR::addVelocityFromPush(this, cNum_receiveMsgPush_addVelocityFromPush_1, pSender, pReceiver);
            return true;
        }
    }

    return false;
}

bool Kuribo::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgInhaleBlackHole(msg)) {
        mItemGenerator->setTypeNone();
        kill();
        return true;
    }

    if (MR::isMsgPlayerKick(msg) && isEnableKick()) {
        return requestBlowDown(pSender, pReceiver);
    }

    return false;
}

bool Kuribo::requestDead() {
    if (isNerve(GET_NERVE(Kuribo, KuriboNrvNonActive)) || isNerve(GET_NERVE(Kuribo, KuriboNrvFlatDown)) ||
        isNerve(GET_NERVE(Kuribo, KuriboNrvHipDropDown)) || isNerve(GET_NERVE(Kuribo, KuriboNrvPressDown)) ||
        isNerve(GET_NERVE(Kuribo, KuriboNrvBlowDown))) {
        return false;
    }

    MR::deleteEffectAll(this);
    MR::clearHitSensors(this);
    MR::invalidateHitSensors(this);
    return true;
}

bool Kuribo::requestFlatDown(HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (MR::isSensorEnemyAttack(pReceiver)) {
        return false;
    }

    if (!isEnableStamp()) {
        return false;
    }

    if (!requestDead()) {
        return false;
    }

    MR::startSoundSeVer(this, "SE_EM_STOMPED_S");
    if (isUpsideDown()) {
        MR::startAction(this, "FlatDownReverse");
    } else {
        MR::startAction(this, "FlatDown");
    }

    setNerve(GET_NERVE(Kuribo, KuriboNrvFlatDown));
    MR::offBind(this);
    mItemGenerator->setTypeCoin(1);
    return true;
}

bool Kuribo::requestHipDropDown(HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (!requestDead()) {
        return false;
    }

    if (MR::isSensorEnemyAttack(pReceiver)) {
        return false;
    }

    MR::startSoundSeVer(this, "SE_EM_STOMPED_S");
    if (isUpsideDown()) {
        MR::startAction(this, "HipDropDownReverse");
    } else {
        MR::startAction(this, "HipDropDown");
    }

    setNerve(GET_NERVE(Kuribo, KuriboNrvHipDropDown));
    MR::offBind(this);
    mItemGenerator->setTypeCoin(1);
    return true;
}

bool Kuribo::requestPressDown() {
    if (!requestDead()) {
        return false;
    }

    MR::startSoundSeVer(this, "SE_EM_STOMPED_S");
    if (isUpsideDown()) {
        MR::startAction(this, "HipDropDownReverse");
    } else {
        MR::startAction(this, "HipDropDown");
    }

    setNerve(GET_NERVE(Kuribo, KuriboNrvPressDown));
    MR::zeroVelocity(this);
    MR::offBind(this);
    mItemGenerator->setTypeStarPeace(3);
    return true;
}

bool Kuribo::requestStagger(HitSensor* pSender, HitSensor* pReceiver) {
    if (isEnablePanch()) {
        mStateStagger->setPunchDirection(pSender, pReceiver);
        setNerve(GET_NERVE(Kuribo, KuriboNrvStagger));
        return true;
    }

    return false;
}

bool Kuribo::requestBlowDown(HitSensor* pSender, HitSensor* pReceiver) {
    if (!isEnablePanch()) {
        return false;
    }

    MR::deleteEffectAll(this);
    MR::setVelocityBlowAttack(this, pSender, pReceiver, cNum_requestBlowDown_f1, cNum_requestBlowDown_f2, 4);
    setNerve(GET_NERVE(Kuribo, KuriboNrvBlowDown));
    mItemGenerator->setTypeStarPeace(3);
    return true;
}

bool Kuribo::requestAttackSuccess() {
    if (isEnableAttack()) {
        mScaleController->stopAndReset();
        MR::deleteEffectAll(this);
        setNerve(GET_NERVE(Kuribo, KuriboNrvAttackSuccess));
        return true;
    }
    return false;
}

void Kuribo::onNoGravitySupport() {
    mIsNoGravitySupport = true;
    MR::offCalcGravity(this);
}

void Kuribo::setUp(const Vec3f& rPosition, const Quat4f& rPose, const Vec3f& rVelocity) {
    mPosition.set(rPosition);
    mVelocity.set(rVelocity);
    mPoseQuat.x = rPose.x;
    mPoseQuat.y = rPose.y;
    mPoseQuat.z = rPose.z;
    mPoseQuat.w = rPose.w;
    mPoseQuat.getZDir(mFront);
}

// Thrown out of a Kabokuri's pumpkin.
void Kuribo::appearBlowed(const Vec3f& rPosition, const Quat4f& rPose, const Vec3f& rVelocity) {
    setUp(rPosition, rPose, rVelocity);
    setNerve(GET_NERVE(Kuribo, KuriboNrvBlow));
    MR::startAction(this, "Blow");
    appear();
    if (mIsNoGravitySupport) {
        MR::calcGravityOrZero(this);
    }
}

// Left flattened when a Kabokuri's pumpkin is hip-dropped.
void Kuribo::appearHipDropped(const Vec3f& rPosition, const Quat4f& rPose) {
    setUp(rPosition, rPose, Vec3f(0.0f, 0.0f, 0.0f));
    appear();
    MR::startAction(this, "HipDropDown");
    setNerve(GET_NERVE(Kuribo, KuriboNrvHipDropDown));
    MR::offBind(this);
    mItemGenerator->setTypeCoin(1);
}

bool Kuribo::tryNonActive() {
    bool isNotNearPlayer = !MR::isNearPlayerAnyTime(this, cNum_tryNonActive_isNearPlayerAnyTime_1);
    if (isNotNearPlayer && MR::isBindedGround(this)) {
        MR::zeroVelocity(this);
        setNerve(GET_NERVE(Kuribo, KuriboNrvNonActive));
        return true;
    }

    return false;
}

bool Kuribo::tryActive() {
    if (MR::isNearPlayerAnyTime(this, cNum_tryActive_isNearPlayerAnyTime_1)) {
        setNerve(GET_NERVE(Kuribo, KuriboNrvWander));
        return true;
    }

    return false;
}

bool Kuribo::tryEndBlow() {
    if (MR::isBindedGround(this) && MR::isGreaterStep(this, cStep_tryEndBlow_1)) {
        MR::startAction(this, "Land");
        MR::onBind(this);
        setNerve(GET_NERVE(Kuribo, KuriboNrvBlowLand));
        return true;
    }

    return false;
}

bool Kuribo::tryEndBlowLand() {
    if (MR::isGreaterStep(this, cStep_tryEndBlowLand_1)) {
        setNerve(GET_NERVE(Kuribo, KuriboNrvChase));
        return true;
    }

    return false;
}

bool Kuribo::tryFind() {
    if (mStateFindPlayer->isInSightPlayer()) {
        setNerve(GET_NERVE(Kuribo, KuriboNrvFindPlayer));
        return true;
    }

    return false;
}

bool Kuribo::tryPointBind() {
    if (isEnablePointBind() && mBindStarPointer->tryStartPointBind()) {
        setNerve(GET_NERVE(Kuribo, KuriboNrvBindStarPointer));
        return true;
    }

    return false;
}

bool Kuribo::tryDead() {
    if (isEnableDead()) {
        if (MR::isInDeath(this, Vec3f(0.0f, 0.0f, 0.0f)) || MR::isBindedGroundDamageFire(this) || MR::isInWater(mPosition)) {
            mItemGenerator->setTypeNone();
            kill();
            return true;
        }

        if (MR::isPressedRoofAndGround(this) && requestPressDown()) {
            return true;
        }
    }

    return false;
}

void Kuribo::exeWander() {
    MR::updateActorState(this, mStateWander);
    if (!tryFind()) {
        if (tryNonActive()) {
            return;
        }
    }
}

void Kuribo::exeFindPlayer() {
    if (!MR::updateActorStateAndNextNerve(this, mStateFindPlayer, GET_NERVE(Kuribo, KuriboNrvChase))) {
        if (mStateFindPlayer->isFindJumpBegin()) {
            MR::startSoundSeVer(this, "SE_EM_KURIBO_FIND");
        }
    }
}

void Kuribo::exeChase() {
    if (MR::updateActorStateAndNextNerve(this, mStateChase, GET_NERVE(Kuribo, KuriboNrvWander))) {
        mStateWander->setWanderCenter(mPosition);
    }

    if (mStateChase->isRunning()) {
        MR::startLevelSoundSeVer(this, "SE_EM_LV_KURIBO_DASH1");
    }
}

void Kuribo::exeStagger() {
    if (!MR::updateActorStateAndNextNerve(this, mStateStagger, GET_NERVE(Kuribo, KuriboNrvWander))) {
        if (mStateStagger->isStaggerStart()) {
            MR::startSoundSeVer(this, "SE_EM_CRASH_S");
            MR::startBlowHitSound(this);
        }

        if (mStateStagger->isSwooning(15)) {
            MR::startLevelSoundSeVer(this, "SE_EM_LV_SWOON_S");
        }

        if (mStateStagger->isRecoverStart()) {
            MR::startSoundSeVer(this, "SE_EM_KURIBO_SWOON_RECOVER");
        }
    }
}

void Kuribo::exeNonActive() {
    if (MR::isFirstStep(this)) {
        calcAnim();
        MR::offBind(this);
        MR::offCalcGravity(this);
        MR::offCalcShadow(this, nullptr);
    }

    MR::zeroVelocity(this);
    if (tryActive()) {
        return;
    }
}

void Kuribo::endNonActive() {
    MR::onBind(this);
    MR::onCalcGravity(this);
    MR::onCalcShadow(this, nullptr);
}

void Kuribo::exeAppearFromBox() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "AppearFromBox");
        Vec3f zDir;
        mPoseQuat.getZDir(zDir);
        MR::setVelocitySeparateHV(this, zDir, cNum_exeAppearFromBox_f1, cNum_exeAppearFromBox_f2);
        MR::offBind(this);
    }
    exeBlow();
}

void Kuribo::exeBlow() {
    calcPassiveMovement();
    if (MR::isStep(this, cStep_exeBlow_1)) {
        MR::onBind(this);
    }

    if (tryEndBlow()) {
        return;
    }
}

void Kuribo::exeAttackSuccess() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Hit");
    }

    MR::turnDirectionToTargetUseGroundNormalDegree(this, &mFront, *MR::getPlayerPos(), cNum_exeAttackSuccess_f1);
    calcPassiveMovement();
    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
        setNerve(GET_NERVE(Kuribo, KuriboNrvWander));
    }
}

void Kuribo::exeBindStarPointer() {
    MR::updateActorStateAndNextNerve(this, mBindStarPointer, GET_NERVE(Kuribo, KuriboNrvWander));
}

void Kuribo::endBindStarPointer() {
    mBindStarPointer->kill();
}

void Kuribo::exeHipDropDown() {
    if (MR::isFirstStep(this)) {
        MR::startSoundSeVer(this, "SE_EM_CRASH_S");
        MR::zeroVelocity(this);
    }

    if (MR::isGreaterStep(this, cStep_exeHipDropDown_1)) {
        kill();
    }
}

void Kuribo::exeFlatDown() {
    if (MR::isFirstStep(this)) {
        MR::startSoundSeVer(this, "SE_EM_CRASH_S");
        MR::zeroVelocity(this);
    }

    if (MR::isGreaterStep(this, cStep_exeFlatDown_1)) {
        kill();
    }
}

void Kuribo::exePressDown() {
    if (MR::isFirstStep(this)) {
        MR::startSoundSeVer(this, "SE_EM_CRASH_S");
        MR::zeroVelocity(this);
    }

    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(3))) {
        kill();
    }
}

void Kuribo::exeBlowDown() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Blow");
        MR::startSoundSeVer(this, "SE_EM_CRASH_S");
        MR::startBlowHitSound(this);
    }

    calcPassiveMovement();
    MR::turnDirectionDegree(this, &mFront, -mVelocity, cNum_exeBlowDown_f1);
    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfMilliseconds(500))) {
        kill();
    }
}

void Kuribo::calcPassiveMovement() {
    if (!MR::isOnGround(this)) {
        MR::addVelocityToGravity(this, cNum_calcPassiveMovement_addVelocityToGravity_1);
    }

    f32 vel;

    if (MR::isOnGround(this)) {
        vel = cNum_calcPassiveMovement_f1;
    } else {
        vel = cNum_calcPassiveMovement_f2;
    }

    MR::attenuateVelocity(this, vel);
    if (MR::isBindedWall(this)) {
        MR::calcReboundVelocity(&mVelocity, *MR::getWallNormal(this), cNum_calcPassiveMovement_f3, cNum_calcPassiveMovement_f4);
    }
}

bool Kuribo::isEnableKick() const {
    if (isNerve(GET_NERVE(Kuribo, KuriboNrvStagger))) {
        return mStateStagger->isEnableKick();
    }

    return false;
}

bool Kuribo::isUpsideDown() const {
    if (isNerve(GET_NERVE(Kuribo, KuriboNrvStagger))) {
        return mStateStagger->isUpsideDown();
    }

    return false;
}

bool Kuribo::isEnableDead() const {
    if (isNerve(GET_NERVE(Kuribo, KuriboNrvWander)) || isNerve(GET_NERVE(Kuribo, KuriboNrvFindPlayer)) ||
        isNerve(GET_NERVE(Kuribo, KuriboNrvChase)) || isNerve(GET_NERVE(Kuribo, KuriboNrvStagger)) ||
        isNerve(GET_NERVE(Kuribo, KuriboNrvBindStarPointer)) || isNerve(GET_NERVE(Kuribo, KuriboNrvNonActive)) ||
        isNerve(GET_NERVE(Kuribo, KuriboNrvAttackSuccess))) {
        return true;
    }

    return false;
}

bool Kuribo::isEnablePointBind() const {
    if (isNerve(GET_NERVE(Kuribo, KuriboNrvWander)) || isNerve(GET_NERVE(Kuribo, KuriboNrvFindPlayer)) ||
        isNerve(GET_NERVE(Kuribo, KuriboNrvChase)) || isNerve(GET_NERVE(Kuribo, KuriboNrvAttackSuccess))) {
        return true;
    }

    return false;
}

bool Kuribo::isEnableAttack() const {
    if (isNerve(GET_NERVE(Kuribo, KuriboNrvWander)) || isNerve(GET_NERVE(Kuribo, KuriboNrvFindPlayer)) ||
        isNerve(GET_NERVE(Kuribo, KuriboNrvChase))) {
        return true;
    }

    return false;
}

bool Kuribo::isEnableStamp() const {
    if (isNerve(GET_NERVE(Kuribo, KuriboNrvNonActive)) || isNerve(GET_NERVE(Kuribo, KuriboNrvFlatDown)) ||
        isNerve(GET_NERVE(Kuribo, KuriboNrvHipDropDown)) || isNerve(GET_NERVE(Kuribo, KuriboNrvPressDown)) ||
        isNerve(GET_NERVE(Kuribo, KuriboNrvBlowDown)) || isNerve(GET_NERVE(Kuribo, KuriboNrvBlow))) {
        return false;
    }

    return true;
}

bool Kuribo::isEnablePanch() const {
    if (isNerve(GET_NERVE(Kuribo, KuriboNrvFlatDown)) || isNerve(GET_NERVE(Kuribo, KuriboNrvHipDropDown)) ||
        isNerve(GET_NERVE(Kuribo, KuriboNrvPressDown)) || isNerve(GET_NERVE(Kuribo, KuriboNrvBlowDown)) ||
        isNerve(GET_NERVE(Kuribo, KuriboNrvBlow))) {
        return false;
    }

    return true;
}

bool Kuribo::isEnablePushMove() const {
    if (isNerve(GET_NERVE(Kuribo, KuriboNrvFlatDown)) || isNerve(GET_NERVE(Kuribo, KuriboNrvHipDropDown)) ||
        isNerve(GET_NERVE(Kuribo, KuriboNrvPressDown)) || isNerve(GET_NERVE(Kuribo, KuriboNrvBlowDown))) {
        return false;
    }

    return true;
}

void Kuribo::exeBlowLand() {
    calcPassiveMovement();
    tryEndBlowLand();
}

namespace MR {
    LiveActor* createNoItemKuriboActor(const char* pName) {
        Kuribo* pKuribo = new Kuribo(pName);
        pKuribo->mHasItem = false;
        return pKuribo;
    }
};  // namespace MR
