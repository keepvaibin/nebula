#include "Game/Enemy/KuriboChief.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/Enemy/ItemGenerator.hpp"
#include "Game/Enemy/WalkerStateBindStarPointer.hpp"
#include "Game/Enemy/WalkerStateChase.hpp"
#include "Game/Enemy/WalkerStateFindPlayer.hpp"
#include "Game/Enemy/WalkerStateFunction.hpp"
#include "Game/Enemy/WalkerStateParam.hpp"
#include "Game/Enemy/WalkerStateStagger.hpp"
#include "Game/Enemy/WalkerStateWander.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/KeySwitch.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorStateUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/BaseMatrixFollowTargetHolder.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/Table.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_KuriboChiefParam_f1, "KuriboChiefParam.f1", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_KuriboChiefParam_f2, "KuriboChiefParam.f2", 0.99f);
NEBULA_TUNABLE_NAMED(f32, cNum_KuriboChiefParam_f3, "KuriboChiefParam.f3", 0.93f);
NEBULA_TUNABLE_NAMED(f32, cNum_KuriboChiefParam_f4, "KuriboChiefParam.f4", 1400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_KuriboChiefParam_f5, "KuriboChiefParam.f5", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_KuriboChiefParam_f6, "KuriboChiefParam.f6", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_KuriboChiefParam_f7, "KuriboChiefParam.f7", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_KuriboChiefParam_f8, "KuriboChiefParam.f8", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_KuriboChiefParam_f9, "KuriboChiefParam.f9", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_KuriboChiefParam_f10, "KuriboChiefParam.f10", 0.99f);
NEBULA_TUNABLE_NAMED(f32, cNum_KuriboChiefParam_f11, "KuriboChiefParam.f11", 0.6f);
NEBULA_TUNABLE_NAMED(f32, cNum_KuriboChiefParam_f12, "KuriboChiefParam.f12", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_KuriboChiefParam_f13, "KuriboChiefParam.f13", 35.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_KuriboChiefParam_f14, "KuriboChiefParam.f14", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_KuriboChiefParam_f15, "KuriboChiefParam.f15", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_KuriboChiefParam_f16, "KuriboChiefParam.f16", 0.6f);
NEBULA_TUNABLE_NAMED(f32, cNum_KuriboChiefParam_f17, "KuriboChiefParam.f17", 0.06f);
NEBULA_TUNABLE_NAMED(f32, cNum_KuriboChiefParam_f18, "KuriboChiefParam.f18", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_KuriboChiefParam_f19, "KuriboChiefParam.f19", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 350.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 350.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f1, "initSensor.f1", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f2, "initSensor.f2", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f3, "initSensor.f3", 120.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f4, "initSensor.f4", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f5, "initSensor.f5", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f6, "initSensor.f6", 130.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f7, "initSensor.f7", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f8, "initSensor.f8", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f9, "initSensor.f9", 130.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f10, "initSensor.f10", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f11, "initSensor.f11", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f12, "initSensor.f12", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initSensor_f13, "initSensor.f13", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_requestBlowDown_f1, "requestBlowDown.f1", 25.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_requestBlowDown_f2, "requestBlowDown.f2", 35.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeBlowDown_f1, "exeBlowDown.f1", 30.0f);

namespace {
    NEBULA_TUNABLE(f32, sBinderSize, 300.0f);
    NEBULA_TUNABLE(f32, sBinderYOffset, 300.0f);
    NEBULA_TUNABLE(f32, sUpVecBlendRate, 0.05f);
    NEBULA_TUNABLE(f32, sFrontVecBlendRate, 0.5f);
    NEBULA_TUNABLE(f32, sTurnLimitDegree, 5.0f);
    static const s32 sAttackSuccessTime = NebulaFrameClock::stepsOfSeconds(1);
    NEBULA_TUNABLE(s32, sBlowDownLandTime, 5);
    NEBULA_TUNABLE(s32, sBoundSoundStep, 27);
    NEBULA_TUNABLE(s32, sSwoonSoundWait, 56);
    NEBULA_TUNABLE(s32, sSpinSoundStartStep, 10);
    NEBULA_TUNABLE(s32, sSpinSoundEndStep, 105);
};  // namespace

namespace {
    class KuriboChiefParam {
    public:
        KuriboChiefParam();

        WalkerStateParam mStateParam1;
        WalkerStateParam mStateParam2;
        WalkerStateStaggerParam mStaggerParam;
        WalkerStateFindPlayerParam mFindPlayerParam;
        WalkerStateChaseParam mChaseParam;
        WalkerStateWanderParam mWanderParam;
        AnimScaleParam mScaleParam;
    };

    KuriboChiefParam::KuriboChiefParam() {
        mStateParam1.mGravityAccel = cNum_KuriboChiefParam_f1;
        mStateParam1.mAirFriction = cNum_KuriboChiefParam_f2;
        mStateParam1.mGroundFriction = cNum_KuriboChiefParam_f3;
        mStateParam1.mPlayerNearDistance = cNum_KuriboChiefParam_f4;
        mStateParam1.mPlayerSightFanDegreeH = cNum_KuriboChiefParam_f5;
        mStateParam1.mPlayerSightFanDegreeV = cNum_KuriboChiefParam_f6;
        mWanderParam.mSpeed = cNum_KuriboChiefParam_f7;
        mWanderParam.mWaitTime = NebulaFrameClock::stepsOfSeconds(2);
        mWanderParam.mWalkTime = NebulaFrameClock::stepsOfSeconds(5);
        mWanderParam.mTurnMaxRateDegree = 1.0f;
        mChaseParam.mChaseSpeed = cNum_KuriboChiefParam_f8;
        mStateParam2.mGravityAccel = cNum_KuriboChiefParam_f9;
        mStateParam2.mAirFriction = cNum_KuriboChiefParam_f10;
        mStateParam2.mGroundFriction = cNum_KuriboChiefParam_f11;
        mStaggerParam.mStaggerTime = NebulaFrameClock::stepsOfSeconds(4);
        mStaggerParam.mRotateRateDegree = 0.0f;
        mStaggerParam.mKickEnableStep = 70;
        mFindPlayerParam.mJumpStartStep = 56;
        mFindPlayerParam.mTurnMaxRateDegree = cNum_KuriboChiefParam_f12;
        mFindPlayerParam.mJumpVelocity = cNum_KuriboChiefParam_f13;
        mScaleParam.mAnimSpring = cNum_KuriboChiefParam_f14;
        mScaleParam.mPointerTime = cNum_KuriboChiefParam_f15;
        mScaleParam.mPointerBase = cNum_KuriboChiefParam_f16;
        mScaleParam.mPointerAmplitude = cNum_KuriboChiefParam_f17;
        mScaleParam.mPointerFreq = cNum_KuriboChiefParam_f18;
        mScaleParam.mHitReactionSpring = cNum_KuriboChiefParam_f19;
    }

    static KuriboChiefParam sParam;
};  // namespace

namespace NrvKuriboChief {
    NEW_NERVE(KuriboChiefNrvWander, KuriboChief, Wander);
    NEW_NERVE(KuriboChiefNrvFindPlayer, KuriboChief, FindPlayer);
    NEW_NERVE(KuriboChiefNrvChase, KuriboChief, Chase);
    NEW_NERVE(KuriboChiefNrvAttackSuccess, KuriboChief, AttackSuccess);
    NEW_NERVE(KuriboChiefNrvStagger, KuriboChief, Stagger);
    NEW_NERVE(KuriboChiefNrvTrample, KuriboChief, Trample);
    NEW_NERVE_ONEND(KuriboChiefNrvBindStarPointer, KuriboChief, BindStarPointer, BindStarPointer);
    NEW_NERVE(KuriboChiefNrvBlowDown, KuriboChief, BlowDown);
    NEW_NERVE(KuriboChiefNrvBlowDownLand, KuriboChief, BlowDownLand);
};  // namespace NrvKuriboChief

KuriboChief::KuriboChief(const char* pName) : LiveActor(pName) {
    mScaleController = nullptr;
    mStateWander = nullptr;
    mStateFindPlayer = nullptr;
    mStateChase = nullptr;
    mStateStagger = nullptr;
    mStateBindStarPointer = nullptr;
    mItemGenerator = nullptr;
    mKeySwitch = nullptr;
    mRotateQuat.x = 0.0f;
    mRotateQuat.y = 0.0f;
    mRotateQuat.z = 0.0f;
    mRotateQuat.w = 1.0f;
    mFront.set(0.0f, 0.0f, 1.0f);
}

void KuriboChief::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("KuriboChief", nullptr, false);
    MR::connectToSceneEnemy(this);
    MR::initLightCtrl(this);
    MR::onCalcGravity(this);
    mItemGenerator = new ItemGenerator();
    initEffectKeeper(1, nullptr);
    MR::initShadowFromCSV(this, "Shadow");
    ::sParam = KuriboChiefParam();  // again, with a mod's numbers
    mScaleController = new AnimScaleController(&::sParam.mScaleParam);
    MR::initStarPointerTargetAtJoint(this, "Body", cNum_init_f1, Vec3f(cNum_init_f2, 0.0f, 0.0f));
    initSensor();
    initBinder(::sBinderSize, ::sBinderYOffset, 0);
    initNerve(GET_NERVE(KuriboChief, KuriboChiefNrvWander));
    initSound(6, false);
    initState();
    initKeySwitch(rIter);
    MR::useStageSwitchWriteDead(this, rIter);
    MR::useStageSwitchSleep(this, rIter);
    MR::addBaseMatrixFollowTarget(this, rIter, nullptr, nullptr);
    MR::declareStarPiece(this, 6);

    s32 arg = -1;
    MR::getPlacementArg7WithInit(rIter, &arg);
    if (arg != -1) {
        MR::declareCameraRegisterVec(this, arg, &mPosition);
    }

    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::syncStageSwitchAppear(this);
        makeActorDead();
    } else {
        makeActorAppeared();
    }
}

void KuriboChief::initAfterPlacement() {
    MR::trySetMoveLimitCollision(this);
}

void KuriboChief::initSensor() {
    LiveActor::initHitSensor(8);
    MR::addHitSensorAtJointEnemy(this, "Body", "Body", 8, cNum_initSensor_f1, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorAtJointEnemy(this, "Head1", "Head", 8, cNum_initSensor_f2, Vec3f(cNum_initSensor_f3, 0.0f, 0.0f));
    MR::addHitSensorAtJointEnemy(this, "Head2", "Head", 8, cNum_initSensor_f4, Vec3f(cNum_initSensor_f5, 0.0f, 0.0f));
    MR::addHitSensorAtJointEnemy(this, "LegL", "LegL", 8, cNum_initSensor_f6, Vec3f(cNum_initSensor_f7, -cNum_initSensor_f8, 0.0f));
    MR::addHitSensorAtJointEnemy(this, "LegR", "LegR", 8, cNum_initSensor_f9, Vec3f(cNum_initSensor_f10, -cNum_initSensor_f11, 0.0f));
    MR::addHitSensorEnemy(this, "Punch", 8, cNum_initSensor_f12, Vec3f(0.0f, cNum_initSensor_f13, 0.0f));
}

void KuriboChief::initState() {
    mStateFindPlayer = new WalkerStateFindPlayer(this, &mFront, &::sParam.mStateParam1, &::sParam.mFindPlayerParam);
    mStateWander = new WalkerStateWander(this, &mFront, &::sParam.mStateParam1, &::sParam.mWanderParam);
    mStateChase = new WalkerStateChase(this, &mFront, &::sParam.mStateParam1, &::sParam.mChaseParam);
    mStateStagger = new WalkerStateStagger(this, &mFront, &::sParam.mStateParam2, &::sParam.mStaggerParam);
    mStateBindStarPointer = new WalkerStateBindStarPointer(this, mScaleController);
}

void KuriboChief::initKeySwitch(const TableIter& rIter) {
    if (MR::useStageSwitchWriteA(this, rIter)) {
        mKeySwitch = new KeySwitch("鍵スイッチ");
        mKeySwitch->initKeySwitchByOwner(rIter);
    }
}

void KuriboChief::kill() {
    if (MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
    }

    if (mKeySwitch != nullptr) {
        mKeySwitch->appearKeySwitch(mPosition);
    } else {
        mItemGenerator->generate(this);
    }

    LiveActor::kill();
}

void KuriboChief::control() {
    if (mScaleController != nullptr) {
        mScaleController->updateNerve();
    }

    MR::blendQuatFromGroundAndFront(&mRotateQuat, this, mFront, ::sUpVecBlendRate, ::sFrontVecBlendRate);
}

void KuriboChief::calcAndSetBaseMtx() {
    MR::setBaseTRMtx(this, mRotateQuat);
    MR::setBaseScale(this, mScaleController->mScale * mScale);
}

void KuriboChief::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (pSender != getSensor("Punch") &&
        (((isEnableAttack() || !MR::isSensorPlayer(pReceiver)) && !MR::isSensorEnemy(pReceiver)) ||
         !MR::sendMsgPushAndKillVelocityToTarget(this, pReceiver, pSender)) &&
        isEnableAttack() && MR::isSensorPlayer(pReceiver) && !MR::sendMsgEnemyAttack(pReceiver, pSender)) {
        MR::sendMsgPush(pReceiver, pSender);
    }
}

bool KuriboChief::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (pReceiver != getSensor("Punch")) {
        if (MR::isMsgStarPieceReflect(msg)) {
            mScaleController->startHitReaction();
            return true;
        }

        if (MR::isMsgInvincibleAttack(msg)) {
            return requestBlowDown(pSender, pReceiver);
        }

        return false;
    }

    if (MR::isMsgPlayerHitAll(msg)) {
        if (isEnableKick()) {
            return requestBlowDown(pSender, pReceiver);
        } else {
            return requestStagger(pSender, pReceiver);
        }
    }

    return false;
}

bool KuriboChief::receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (pReceiver == getSensor("Punch")) {
        return false;
    }

    if (MR::isMsgToEnemyAttackBlow(msg)) {
        return requestStagger(pSender, pReceiver);
    }

    if (MR::isMsgToEnemyAttackShockWave(msg)) {
        return requestStagger(pSender, pReceiver);
    }

    return false;
}

bool KuriboChief::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (pReceiver == getSensor("Punch")) {
        return false;
    }

    if (MR::isMsgInhaleBlackHole(msg)) {
        mItemGenerator->setTypeNone();
        kill();
        return true;
    }

    if (MR::isMsgPlayerKick(msg) && MR::isSensorPlayer(pSender) && isEnableKick() && requestBlowDown(pSender, pReceiver)) {
        mItemGenerator->setTypeStarPeace(6);
        return true;
    }

    return false;
}

bool KuriboChief::requestStagger(HitSensor* pSender, HitSensor* pReceiver) {
    if (isDown()) {
        return false;
    }

    mStateStagger->setPunchDirection(pSender, pReceiver);
    setNerve(GET_NERVE(KuriboChief, KuriboChiefNrvStagger));
    return true;
}

bool KuriboChief::requestBlowDown(HitSensor* pSender, HitSensor* pReceiver) {
    if (isDown()) {
        return false;
    }

    MR::setVelocityBlowAttack(this, pSender, pReceiver, cNum_requestBlowDown_f1, cNum_requestBlowDown_f2, 4);
    setNerve(GET_NERVE(KuriboChief, KuriboChiefNrvBlowDown));
    return true;
}

bool KuriboChief::tryFind() {
    if (mStateFindPlayer->isInSightPlayer()) {
        setNerve(GET_NERVE(KuriboChief, KuriboChiefNrvFindPlayer));
        return true;
    }

    return false;
}

bool KuriboChief::tryPointBind() {
    if (mStateBindStarPointer->tryStartPointBind()) {
        setNerve(GET_NERVE(KuriboChief, KuriboChiefNrvBindStarPointer));
        return true;
    }

    return false;
}

void KuriboChief::exeWander() {
    MR::updateActorState(this, mStateWander);
    if (!tryFind() && tryPointBind()) {
        return;
    }
}

void KuriboChief::exeFindPlayer() {
    if (!MR::updateActorStateAndNextNerve(this, mStateFindPlayer, GET_NERVE(KuriboChief, KuriboChiefNrvChase))) {
        if (mStateFindPlayer->isFindJumpBegin()) {
            MR::startSound(this, "SE_EM_KURIBOCHIEF_FIND");
        }

        if (mStateFindPlayer->isLandStart()) {
            MR::startSound(this, "SE_EM_KURIBOCHIEF_LAND");
        }

        if (tryPointBind()) {
            return;
        }
    }
}

void KuriboChief::exeChase() {
    if (MR::updateActorStateAndNextNerve(this, mStateChase, GET_NERVE(KuriboChief, KuriboChiefNrvWander))) {
        mStateWander->setWanderCenter(mPosition);
    }

    if (tryPointBind()) {
        return;
    }
}

void KuriboChief::exeStagger() {
    if (!MR::updateActorStateAndNextNerve(this, mStateStagger, GET_NERVE(KuriboChief, KuriboChiefNrvWander))) {
        if (mStateStagger->isStaggerStart()) {
            MR::startSound(this, "SE_EM_KURIBOCHIEF_BLOW");
            MR::startBlowHitSound(this);
        }

        if (MR::isStep(this, ::sBoundSoundStep)) {
            MR::startSound(this, "SE_EM_KURIBOCHIEF_BOUND");
        }

        if (mStateStagger->isSwooning(::sSwoonSoundWait)) {
            MR::startLevelSound(this, "SE_EM_LV_SWOON_S");
        }

        if (mStateStagger->isSpinning(::sSpinSoundStartStep, ::sSpinSoundEndStep)) {
            MR::startLevelSound(this, "SE_EM_LV_KURIBOCHIEF_STAGGER");
        }

        if (mStateStagger->isRecoverStart()) {
            MR::startSound(this, "SE_EM_KURIBOCHIEF_RECOVER");
        }
    }
}

void KuriboChief::exeTrample() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_EM_KURIBOCHIEF_TRAMPLE");
    }

    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfMilliseconds(500))) {
        setNerve(GET_NERVE(KuriboChief, KuriboChiefNrvWander));
    }
}

void KuriboChief::exeBindStarPointer() {
    MR::updateActorStateAndNextNerve(this, mStateBindStarPointer, GET_NERVE(KuriboChief, KuriboChiefNrvWander));
}

void KuriboChief::endBindStarPointer() {
    mStateBindStarPointer->kill();
}

void KuriboChief::exeAttackSuccess() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Hit");
    }

    MR::turnDirectionToPlayerDegree(this, &mFront, ::sTurnLimitDegree);
    WalkerStateFunction::calcPassiveMovement(this, &::sParam.mStateParam1);

    if (MR::isGreaterStep(this, ::sAttackSuccessTime)) {
        setNerve(GET_NERVE(KuriboChief, KuriboChiefNrvWander));
    }

    if (tryPointBind()) {
        return;
    }
}

void KuriboChief::exeBlowDown() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "BlowDown");
        MR::startBlowHitSound(this);
        MR::startSound(this, "SE_EM_KURIBOCHIEF_BLOW");
        MR::startSystemSE("SE_SY_VS_BOSS_LAST_HIT");
    }

    WalkerStateFunction::calcPassiveMovement(this, &::sParam.mStateParam1);
    MR::turnDirectionDegree(this, &mFront, -mVelocity, cNum_exeBlowDown_f1);

    if (MR::isGreaterStep(this, ::sBlowDownLandTime) && MR::isBindedGround(this)) {
        setNerve(GET_NERVE(KuriboChief, KuriboChiefNrvBlowDownLand));
        MR::zeroVelocity(this);
    }
}

void KuriboChief::exeBlowDownLand() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "BlowDownLand");
        MR::startSound(this, "SE_EM_KURIBOCHIEF_BOUND_LAST");
    }

    if (MR::isBckStopped(this)) {
        MR::emitEffect(this, "Death");
        MR::startSound(this, "SE_EM_KURIBOCHIEF_RUN");
        kill();
    }
}

bool KuriboChief::isEnableAttack() const {
    if (isNerve(GET_NERVE(KuriboChief, KuriboChiefNrvWander)) || isNerve(GET_NERVE(KuriboChief, KuriboChiefNrvFindPlayer)) ||
        isNerve(GET_NERVE(KuriboChief, KuriboChiefNrvChase))) {
        return true;
    }

    return false;
}

bool KuriboChief::isEnableKick() const {
    if (isNerve(GET_NERVE(KuriboChief, KuriboChiefNrvStagger))) {
        return mStateStagger->isEnableKick();
    }

    return false;
}

bool KuriboChief::isDown() const {
    if (isNerve(GET_NERVE(KuriboChief, KuriboChiefNrvBlowDown)) || isNerve(GET_NERVE(KuriboChief, KuriboChiefNrvBlowDownLand))) {
        return true;
    }

    return false;
}
