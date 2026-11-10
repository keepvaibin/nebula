#include "Game/Enemy/KuriboMini.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/Enemy/ItemGenerator.hpp"
#include "Game/Enemy/WalkerStateBindStarPointer.hpp"
#include "Game/Enemy/WalkerStateChase.hpp"
#include "Game/Enemy/WalkerStateFindPlayer.hpp"
#include "Game/Enemy/WalkerStateParam.hpp"
#include "Game/Enemy/WalkerStateStagger.hpp"
#include "Game/Enemy/WalkerStateWander.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorStateUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/Table.hpp"
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

NEBULA_TUNABLE_NAMED(f32, cNum_KuriboMiniParam_f1, "KuriboMiniParam.f1", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_KuriboMiniParam_f2, "KuriboMiniParam.f2", 0.99f);
NEBULA_TUNABLE_NAMED(f32, cNum_KuriboMiniParam_f3, "KuriboMiniParam.f3", 0.93f);
NEBULA_TUNABLE_NAMED(f32, cNum_KuriboMiniParam_f4, "KuriboMiniParam.f4", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_KuriboMiniParam_f5, "KuriboMiniParam.f5", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_KuriboMiniParam_f6, "KuriboMiniParam.f6", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_KuriboMiniParam_f7, "KuriboMiniParam.f7", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_KuriboMiniParam_f8, "KuriboMiniParam.f8", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_KuriboMiniParam_f9, "KuriboMiniParam.f9", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_KuriboMiniParam_f10, "KuriboMiniParam.f10", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_KuriboMiniParam_f11, "KuriboMiniParam.f11", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f1, "initSensor.f1", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f2, "initSensor.f2", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f3, "initSensor.f3", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f4, "initSensor.f4", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f1, "control.f1", 0.05f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f2, "control.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_requestBlowDown_f1, "requestBlowDown.f1", 22.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_requestBlowDown_f2, "requestBlowDown.f2", 25.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeBlowDown_f1, "exeBlowDown.f1", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcPassiveMovement_f1, "calcPassiveMovement.f1", 0.93f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcPassiveMovement_f2, "calcPassiveMovement.f2", 0.99f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcPassiveMovement_f3, "calcPassiveMovement.f3", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcPassiveMovement_f4, "calcPassiveMovement.f4", 0.7f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_receiveMsgPush_addVelocityFromPush_1, "receiveMsgPush.addVelocityFromPush.1", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAttackSuccess_turnDirectionToPlayerDegree_1, "exeAttackSuccess.turnDirectionToPlayerDegree.1", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcPassiveMovement_addVelocityToGravity_1, "calcPassiveMovement.addVelocityToGravity.1", 1.5f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeHipDropDown_1, "exeHipDropDown.1", 40);
NEBULA_TUNABLE_NAMED(s32, cStep_exeFlatDown_1, "exeFlatDown.1", 20);

namespace {
    class KuriboMiniParam {
    public:
        KuriboMiniParam();

        WalkerStateParam mStateParam;
        WalkerStateStaggerParam mStaggerParam;
        WalkerStateFindPlayerParam mFindPlayerParam;
        WalkerStateChaseParam mChaseParam;
        WalkerStateWanderParam mWanderParam;
    };

    KuriboMiniParam::KuriboMiniParam() {
        mStateParam.mGravityAccel = cNum_KuriboMiniParam_f1;
        mStateParam.mAirFriction = cNum_KuriboMiniParam_f2;
        mStateParam.mGroundFriction = cNum_KuriboMiniParam_f3;
        mStateParam.mPlayerNearDistance = cNum_KuriboMiniParam_f4;
        mStateParam.mPlayerSightFanDegreeH = cNum_KuriboMiniParam_f5;
        mStateParam.mPlayerSightFanDegreeV = cNum_KuriboMiniParam_f6;
        mWanderParam.mSpeed = cNum_KuriboMiniParam_f7;
        mWanderParam.mWaitTime = NebulaFrameClock::stepsOfSeconds(2);
        mWanderParam.mWalkTime = NebulaFrameClock::stepsOfSeconds(2);
        mWanderParam.mTurnMaxRateDegree = cNum_KuriboMiniParam_f8;
        mChaseParam.mChaseSpeed = cNum_KuriboMiniParam_f9;
        mFindPlayerParam.mTurnMaxRateDegree = cNum_KuriboMiniParam_f10;
        mFindPlayerParam.mJumpVelocity = cNum_KuriboMiniParam_f11;
    }

    static KuriboMiniParam sParam;
};  // namespace

namespace NrvKuriboMini {
    NEW_NERVE(KuriboMiniNrvWander, KuriboMini, Wander);
    NEW_NERVE(KuriboMiniNrvFindPlayer, KuriboMini, FindPlayer);
    NEW_NERVE(KuriboMiniNrvChase, KuriboMini, Chase);
    NEW_NERVE(KuriboMiniNrvStagger, KuriboMini, Stagger);
    NEW_NERVE_ONEND(KuriboMiniNrvBindStarPointer, KuriboMini, BindStarPointer, BindStarPointer);
    NEW_NERVE(KuriboMiniNrvAttackSuccess, KuriboMini, AttackSuccess);
    NEW_NERVE(KuriboMiniNrvHipDropDown, KuriboMini, HipDropDown);
    NEW_NERVE(KuriboMiniNrvPressDown, KuriboMini, PressDown);
    NEW_NERVE(KuriboMiniNrvFlatDown, KuriboMini, FlatDown);
    NEW_NERVE(KuriboMiniNrvBlowDown, KuriboMini, BlowDown);
};  // namespace NrvKuriboMini

KuriboMini::KuriboMini(const char* pName) : LiveActor(pName) {
    mScaleController = nullptr;
    mItemGenerator = nullptr;
    mStateWander = nullptr;
    mStateFindPlayer = nullptr;
    mStateChase = nullptr;
    mStateStagger = nullptr;
    mStateBindStarPointer = nullptr;
    mRotateQuat.x = 0.0f;
    mRotateQuat.y = 0.0f;
    mRotateQuat.z = 0.0f;
    mRotateQuat.w = 1.0f;
    mFront.set(0.0f, 0.0f, 1.0f);
}

void KuriboMini::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("KuriboMini", nullptr, false);
    MR::connectToSceneEnemy(this);
    MR::makeQuatAndFrontFromRotate(&mRotateQuat, &mFront, this);
    MR::onCalcGravity(this);
    MR::initLightCtrl(this);
    MR::declareStarPiece(this, 3);
    MR::declareCoin(this, 1);
    mItemGenerator = new ItemGenerator();
    mScaleController = new AnimScaleController(nullptr);
    initSound(4, false);
    initEffectKeeper(1, nullptr);
    MR::initStarPointerTarget(this, cNum_init_f1, Vec3f(0.0f, cNum_init_f2, 0.0f));
    MR::initShadowVolumeSphere(this, cNum_init_f3);
    initSensor();
    initBinder(cNum_init_f4, cNum_init_f5, 0);
    initNerve(GET_NERVE(KuriboMini, KuriboMiniNrvWander));
    initState();

    if (MR::isValidInfo(rIter)) {
        MR::setGroupClipping(this, rIter, 32);
    }

    MR::useStageSwitchWriteDead(this, rIter);
    MR::useStageSwitchSleep(this, rIter);
    if (MR::useStageSwitchReadB(this, rIter)) {
        MR::listenStageSwitchOnB(this, MR::Functor< KuriboMini >(this, &KuriboMini::makeActorDead));
    }

    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::syncStageSwitchAppear(this);
        makeActorDead();
    } else {
        makeActorAppeared();
    }
}

void KuriboMini::initAfterPlacement() {
    MR::trySetMoveLimitCollision(this);
}

void KuriboMini::initSensor() {
    initHitSensor(2);
    MR::addHitSensorEnemy(this, "body", 8, cNum_initSensor_f1, Vec3f(0.0f, cNum_initSensor_f2, 0.0f));
    MR::addHitSensorEnemyAttack(this, "attack", 8, cNum_initSensor_f3, Vec3f(0.0f, cNum_initSensor_f4, 0.0f));
}

void KuriboMini::initState() {
    ::sParam = KuriboMiniParam();  // again, with a mod's numbers
    mStateFindPlayer = new WalkerStateFindPlayer(this, &mFront, &::sParam.mStateParam, &::sParam.mFindPlayerParam);
    mStateWander = new WalkerStateWander(this, &mFront, &::sParam.mStateParam, &::sParam.mWanderParam);
    mStateChase = new WalkerStateChase(this, &mFront, &::sParam.mStateParam, &::sParam.mChaseParam);
    mStateStagger = new WalkerStateStagger(this, &mFront, &::sParam.mStateParam, &::sParam.mStaggerParam);
    mStateBindStarPointer = new WalkerStateBindStarPointer(this, mScaleController);
}

void KuriboMini::makeActorAppeared() {
    LiveActor::makeActorAppeared();
    mItemGenerator->setTypeStarPeace(3);
}

void KuriboMini::kill() {
    if (MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
    }

    MR::emitEffect(this, "Death");
    MR::startSound(this, "SE_EM_EXPLODE_S");
    mItemGenerator->generate(this);
    LiveActor::kill();
}

void KuriboMini::control() {
    MR::blendQuatFromGroundAndFront(&mRotateQuat, this, mFront, cNum_control_f1, cNum_control_f2);
    mScaleController->updateNerve();
}

void KuriboMini::calcAndSetBaseMtx() {
    MR::setBaseTRMtx(this, mRotateQuat);
    MR::setBaseScale(this, mScaleController->mScale * mScale);
}

void KuriboMini::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!isDown() &&
        (MR::isSensorEnemyAttack(pSender) || ((isEnableAttack() || !MR::isSensorPlayer(pReceiver)) && !MR::isSensorEnemy(pReceiver)) ||
         !MR::sendMsgPushAndKillVelocityToTarget(this, pReceiver, pSender)) &&
        isEnableAttack() && MR::isSensorPlayer(pReceiver) && MR::isSensorEnemyAttack(pSender)) {
        if (!MR::isPlayerHipDropFalling() && MR::sendMsgEnemyAttack(pReceiver, pSender)) {
            requestAttackSuccess();
        } else {
            MR::sendMsgPush(pReceiver, pSender);
        }
    }
}

bool KuriboMini::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (isDown()) {
        return false;
    }

    if (MR::isMsgLockOnStarPieceShoot(msg)) {
        return true;
    }

    if (MR::isMsgStarPieceAttack(msg)) {
        return requestStagger(pSender, pReceiver);
    }

    if (MR::isMsgPlayerTrample(msg) && requestFlatDown(pSender, pReceiver)) {
        mItemGenerator->setTypeCoin(1);
        return true;
    }

    if (MR::isMsgInvincibleAttack(msg) && requestBlowDown(pSender, pReceiver)) {
        mItemGenerator->setTypeCoin(1);
        return true;
    }

    if (MR::isMsgPlayerHipDrop(msg) && requestHipDropDown(pSender, pReceiver)) {
        mItemGenerator->setTypeCoin(1);
        return true;
    }

    if (MR::isMsgPlayerHitAll(msg) && requestBlowDown(pSender, pReceiver)) {
        mItemGenerator->setTypeCoin(1);
        return true;
    }

    return false;
}

bool KuriboMini::receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (isDown()) {
        return false;
    }

    if (MR::isMsgToEnemyAttackTrample(msg) && requestPressDown()) {
        mItemGenerator->setTypeStarPeace(3);
        return true;
    }

    if (MR::isMsgToEnemyAttackShockWave(msg)) {
        return requestStagger(pSender, pReceiver);
    }

    if ((MR::isMsgExplosionAttack(msg) || MR::isMsgToEnemyAttackBlow(msg)) && requestBlowDown(pSender, pReceiver)) {
        mItemGenerator->setTypeStarPeace(3);
        return true;
    }

    return false;
}

bool KuriboMini::receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver) {
    if (isDown()) {
        return false;
    }

    if (MR::isSensorEnemyAttack(pReceiver)) {
        return false;
    }

    if ((MR::isSensorEnemy(pSender) || MR::isSensorRide(pSender) || (!isEnableAttack() && MR::isSensorPlayer(pSender)))) {
        if (!isDown()) {
            MR::addVelocityFromPush(this, cNum_receiveMsgPush_addVelocityFromPush_1, pSender, pReceiver);
            return true;
        }
    }

    return false;
}

bool KuriboMini::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (isDown()) {
        return false;
    }

    if (MR::isMsgInhaleBlackHole(msg)) {
        mItemGenerator->setTypeNone();
        kill();
        return true;
    }

    if (MR::isMsgPlayerKick(msg) && isEnableKick() && requestBlowDown(pSender, pReceiver)) {
        mItemGenerator->setTypeStarPeace(3);
        return true;
    }

    return false;
}

bool KuriboMini::requestHipDropDown(HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (isDown()) {
        return false;
    }

    if (MR::isSensorEnemyAttack(pReceiver)) {
        return false;
    }

    MR::startSound(this, "SE_EM_STOMPED_S");
    MR::startAction(this, "FlatDown");
    setNerve(GET_NERVE(KuriboMini, KuriboMiniNrvHipDropDown));
    MR::offBind(this);
    return true;
}

bool KuriboMini::requestFlatDown(HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (isDown()) {
        return false;
    }

    MR::startSound(this, "SE_EM_STOMPED_S");
    MR::startAction(this, "FlatDown");
    setNerve(GET_NERVE(KuriboMini, KuriboMiniNrvFlatDown));
    MR::offBind(this);
    return true;
}

bool KuriboMini::requestPressDown() {
    if (isDown()) {
        return false;
    }

    MR::startSound(this, "SE_EM_STOMPED_S");
    MR::startAction(this, "FlatDown");
    setNerve(GET_NERVE(KuriboMini, KuriboMiniNrvPressDown));
    MR::offBind(this);
    return true;
}

bool KuriboMini::requestBlowDown(HitSensor* pSender, HitSensor* pReceiver) {
    if (isDown()) {
        return false;
    }

    MR::setVelocityBlowAttack(this, pSender, pReceiver, cNum_requestBlowDown_f1, cNum_requestBlowDown_f2, 4);
    setNerve(GET_NERVE(KuriboMini, KuriboMiniNrvBlowDown));
    return true;
}

bool KuriboMini::requestStagger(HitSensor* pSender, HitSensor* pReceiver) {
    if (isDown()) {
        return false;
    }

    mStateStagger->setPunchDirection(pSender, pReceiver);
    setNerve(GET_NERVE(KuriboMini, KuriboMiniNrvStagger));
    return true;
}

bool KuriboMini::requestAttackSuccess() {
    if (isDown()) {
        return false;
    }

    if (isEnableAttack()) {
        setNerve(GET_NERVE(KuriboMini, KuriboMiniNrvAttackSuccess));
        return true;
    }

    return false;
}

bool KuriboMini::tryFind() {
    if (mStateFindPlayer->isInSightPlayer()) {
        setNerve(GET_NERVE(KuriboMini, KuriboMiniNrvFindPlayer));
        return true;
    }

    return false;
}

bool KuriboMini::tryPointBind() {
    if (mStateBindStarPointer->tryStartPointBind()) {
        setNerve(GET_NERVE(KuriboMini, KuriboMiniNrvBindStarPointer));
        return true;
    }

    return false;
}

bool KuriboMini::tryDeadMap() {
    if (MR::isInDeath(this, Vec3f(0.0f, 0.0f, 0.0f)) || MR::isBindedGroundDamageFire(this) || MR::isInWater(mPosition)) {
        mItemGenerator->setTypeNone();
        kill();
        return true;
    }

    return false;
}

void KuriboMini::exeWander() {
    MR::updateActorState(this, mStateWander);
    if (!tryFind() && !tryDeadMap() && tryPointBind()) {
        return;
    }
}

void KuriboMini::exeFindPlayer() {
    if (!MR::updateActorStateAndNextNerve(this, mStateFindPlayer, GET_NERVE(KuriboMini, KuriboMiniNrvChase))) {
        if (mStateFindPlayer->isFindJumpBegin()) {
            MR::startSound(this, "SE_EM_KURIBOMINI_FIND");
        }

        if (!tryDeadMap() && tryPointBind()) {
            return;
        }
    }
}

void KuriboMini::exeChase() {
    if (MR::updateActorStateAndNextNerve(this, mStateChase, GET_NERVE(KuriboMini, KuriboMiniNrvWander))) {
        mStateWander->setWanderCenter(mPosition);
    }

    if (mStateChase->isRunning()) {
        MR::startLevelSound(this, "SE_EM_LV_KURIBOMINI_DASH");
    }

    if (!tryDeadMap() && tryPointBind()) {
        return;
    }
}

void KuriboMini::exeStagger() {
    if (!MR::updateActorStateAndNextNerve(this, mStateStagger, GET_NERVE(KuriboMini, KuriboMiniNrvWander))) {
        if (mStateStagger->isStaggerStart()) {
            MR::startSound(this, "SE_EM_CRASH_S");
            MR::startBlowHitSound(this);
        }

        if (mStateStagger->isSwooning(15)) {
            MR::startLevelSound(this, "SE_EM_LV_SWOON_S");
        }

        if (mStateStagger->isRecoverStart()) {
            MR::startSound(this, "SE_EM_KURIBO_SWOON_RECOVER");
        }

        if (tryDeadMap()) {
            return;
        }
    }
}

void KuriboMini::exeBindStarPointer() {
    if (!MR::updateActorStateAndNextNerve(this, mStateBindStarPointer, GET_NERVE(KuriboMini, KuriboMiniNrvWander))) {
        tryDeadMap();
    }
}

void KuriboMini::endBindStarPointer() {
    mStateBindStarPointer->kill();
}

void KuriboMini::exeAttackSuccess() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Hit");
    }
    MR::turnDirectionToPlayerDegree(this, &mFront, cNum_exeAttackSuccess_turnDirectionToPlayerDegree_1);
    calcPassiveMovement();

    if (tryDeadMap() || tryPointBind()) {
        return;
    }

    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
        setNerve(GET_NERVE(KuriboMini, KuriboMiniNrvWander));
    }
}

void KuriboMini::exeHipDropDown() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_EM_CRASH_S");
        MR::zeroVelocity(this);
    }

    if (MR::isGreaterStep(this, cStep_exeHipDropDown_1)) {
        kill();
    }
}

void KuriboMini::exeFlatDown() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_EM_CRASH_S");
        MR::zeroVelocity(this);
    }

    if (MR::isGreaterStep(this, cStep_exeFlatDown_1)) {
        kill();
    }
}

void KuriboMini::exePressDown() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_EM_CRASH_S");
        MR::zeroVelocity(this);
    }

    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(3))) {
        kill();
    }
}

void KuriboMini::exeBlowDown() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "BlowDown");
        MR::startSound(this, "SE_EM_CRASH_S");
        MR::startBlowHitSound(this);
    }

    calcPassiveMovement();
    MR::turnDirectionDegree(this, &mFront, -mVelocity, cNum_exeBlowDown_f1);

    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfMilliseconds(500))) {
        kill();
    }
}

void KuriboMini::calcPassiveMovement() {
    if (!MR::isOnGround(this)) {
        MR::addVelocityToGravity(this, cNum_calcPassiveMovement_addVelocityToGravity_1);
    }

    f32 velocity;
    if (MR::isOnGround(this)) {
        velocity = cNum_calcPassiveMovement_f1;
    } else {
        velocity = cNum_calcPassiveMovement_f2;
    }

    MR::attenuateVelocity(this, velocity);
    if (MR::isBindedWall(this)) {
        MR::calcReboundVelocity(&mVelocity, *MR::getWallNormal(this), cNum_calcPassiveMovement_f3, cNum_calcPassiveMovement_f4);
    }
}

bool KuriboMini::isEnableAttack() const {
    if (isNerve(GET_NERVE(KuriboMini, KuriboMiniNrvWander)) || isNerve(GET_NERVE(KuriboMini, KuriboMiniNrvFindPlayer)) ||
        isNerve(GET_NERVE(KuriboMini, KuriboMiniNrvChase))) {
        return true;
    }

    return false;
}

bool KuriboMini::isEnableKick() const {
    return isNerve(GET_NERVE(KuriboMini, KuriboMiniNrvStagger));
}

bool KuriboMini::isDown() const {
    return isNerve(GET_NERVE(KuriboMini, KuriboMiniNrvFlatDown)) || isNerve(GET_NERVE(KuriboMini, KuriboMiniNrvHipDropDown)) ||
           isNerve(GET_NERVE(KuriboMini, KuriboMiniNrvPressDown)) || isNerve(GET_NERVE(KuriboMini, KuriboMiniNrvBlowDown));
}
