#include "Game/NPC/TrickRabbitFreeRun.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/WalkerStateBlowDamage.hpp"
#include "Game/Enemy/WalkerStateRunaway.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/SpotMarkLight.hpp"
#include "Game/NPC/RabbitStateCaught.hpp"
#include "Game/NPC/RabbitStateWaitStart.hpp"
#include "Game/NPC/TrickRabbitUtil.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 1500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 120.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveMsgBlowDamage_f1, "receiveMsgBlowDamage.f1", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveMsgBlowDamage_f2, "receiveMsgBlowDamage.f2", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePowerStarDemo_f1, "exePowerStarDemo.f1", 200.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_exeRunawayStart_checkPassBckFrame_1, "exeRunawayStart.checkPassBckFrame.1", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRunaway_checkPassBckFrame_1, "exeRunaway.checkPassBckFrame.1", 3.0f);

namespace {
    static MR::ActorMoveParam sStartAwayParam = {1.4f, 1.0f, 0.9f, 4.0f};
    NEBULA_TUNABLE(f32, sBodyRadius, 70.0f);
    NEBULA_TUNABLE(f32, sCatchRadius, 30.0f);
    NEBULA_TUNABLE(f32, sBindRadius, 60.0f);
    NEBULA_TUNABLE(f32, sBindYOffset, 60.0f);
    NEBULA_TUNABLE(f32, sUpVecBlendRate, 0.1f);
    NEBULA_TUNABLE(f32, sFrontVecBlendRate, 0.2f);
    NEBULA_TUNABLE(f32, sPressureRange, 600.0f);
    NEBULA_TUNABLE(f32, sNormalAccel, 0.7f);
    NEBULA_TUNABLE(f32, sPressureAccel, 1.5f);
    NEBULA_TUNABLE(f32, sBrakeAccelRate, 0.3f);
};  // namespace

namespace NrvTrickRabbitFreeRun {
    NEW_NERVE(TrickRabbitFreeRunNrvWaitStart, TrickRabbitFreeRun, WaitStart);
    NEW_NERVE(TrickRabbitFreeRunNrvTryDemo, TrickRabbitFreeRun, TryDemo);
    NEW_NERVE(TrickRabbitFreeRunNrvRunawayStart, TrickRabbitFreeRun, RunawayStart);
    NEW_NERVE(TrickRabbitFreeRunNrvRunaway, TrickRabbitFreeRun, Runaway);
    NEW_NERVE(TrickRabbitFreeRunNrvBlowDamage, TrickRabbitFreeRun, BlowDamage);
    NEW_NERVE(TrickRabbitFreeRunNrvCaught, TrickRabbitFreeRun, Caught);
    NEW_NERVE(TrickRabbitFreeRunNrvPowerStarDemo, TrickRabbitFreeRun, PowerStarDemo);
    NEW_NERVE(TrickRabbitFreeRunNrvGiveUp, TrickRabbitFreeRun, GiveUp);
};  // namespace NrvTrickRabbitFreeRun

TrickRabbitFreeRun::TrickRabbitFreeRun(const char* pName)
    : LiveActor(pName), mRotateQuat(0.0f, 0.0f, 0.0f, 1.0f), mFrontVec(0.0f, 0.0f, 1.0f), mStateWaitStart(), mStateRunaway(), mStateBlowDamage(),
      mStateCaught(), mMsgCtrl(), mFootPrint(), mSpotMarkLight() {
}

void TrickRabbitFreeRun::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("TrickRabbit", nullptr, false);
    MR::connectToSceneNpc(this);
    MR::initLightCtrl(this);
    MR::makeQuatAndFrontFromRotate(&mRotateQuat, &mFrontVec, this);

    mSpotMarkLight = new SpotMarkLight(this, cNum_init_f1, cNum_init_f2, nullptr);
    mSpotMarkLight->initWithoutIter();

    MR::initShadowFromCSV(this, "Shadow");

    mFootPrint = TrickRabbitUtil::createRabbitFootPrint(this);

    initEffectKeeper(0, nullptr);
    MR::onEmitEffectSyncClipping(this, "Light");
    MR::onForceDeleteEffectSyncClipping(this, "Light");
    initSensor();
    initBinder(::sBindRadius, ::sBindYOffset, 0);

    mMsgCtrl = MR::createTalkCtrl(this, rIter, "TrickRabbit", Vec3f(0.0f, cNum_init_f3, 0.0f), nullptr);

    initState();
    initSound(4, false);
    MR::onCalcGravity(this);
    MR::declarePowerStar(this);
    initNerve(GET_NERVE(TrickRabbitFreeRun, TrickRabbitFreeRunNrvWaitStart));
    makeActorAppeared();
}

void TrickRabbitFreeRun::initAfterPlacement() {
    MR::trySetMoveLimitCollision(this);
}

void TrickRabbitFreeRun::initSensor() {
    initHitSensor(2);
    MR::addHitSensorAtJointEnemy(this, "Body", "Spine", 8, ::sBodyRadius, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorAtJointEnemy(this, "Catch", "Spine", 8, ::sCatchRadius, Vec3f(0.0f, 0.0f, 0.0f));
    MR::initStarPointerTargetAtJoint(this, "Spine", ::sBodyRadius, Vec3f(0.0f));
}

void TrickRabbitFreeRun::initState() {
    mStateCaught = new RabbitStateCaught(this, mMsgCtrl);
    mStateCaught->init();

    mStateWaitStart = new RabbitStateWaitStart(this, &mFrontVec, mMsgCtrl);
    mStateWaitStart->init();

    mStateRunaway = new WalkerStateRunaway(this, &mFrontVec, nullptr);
    mStateRunaway->init();

    mStateBlowDamage = new WalkerStateBlowDamage(this, &mFrontVec);
    mStateBlowDamage->init();
}

void TrickRabbitFreeRun::control() {
    MR::blendQuatUpFront(&mRotateQuat, -mGravity, mFrontVec, ::sUpVecBlendRate, ::sFrontVecBlendRate);
}

void TrickRabbitFreeRun::calcAndSetBaseMtx() {
    MR::setBaseTRMtx(this, mRotateQuat);
}

void TrickRabbitFreeRun::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::isSensorPlayer(pReceiver)) {
        return;
    }

    if (pSender == getSensor("Catch") && requestCaught()) {
        return;
    }

    if (isEnableCaught()) {
        return;
    }

    MR::sendMsgPush(pReceiver, pSender);
}

bool TrickRabbitFreeRun::receiveMsgPush(HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (MR::isSensorPlayer(pSender)) {
        return !isEnableCaught();
    }

    return false;
}

bool TrickRabbitFreeRun::receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgToEnemyAttackBlow(msg)) {
        return receiveMsgBlowDamage(pSender, pReceiver);
    }

    return false;
}

bool TrickRabbitFreeRun::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(TrickRabbitFreeRun, TrickRabbitFreeRunNrvWaitStart)) ||
        isNerve(GET_NERVE(TrickRabbitFreeRun, TrickRabbitFreeRunNrvGiveUp))) {
        return mStateWaitStart->receiveMsgPlayerAttack(msg, pSender, pReceiver);
    }

    if (MR::isMsgStarPieceAttack(msg)) {
        if (receiveMsgBlowDamage(pSender, pReceiver)) {
            return true;
        }
    }

    if (MR::isMsgPlayerSpinAttack(msg)) {
        return requestCaught();
    }

    return false;
}

bool TrickRabbitFreeRun::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(TrickRabbitFreeRun, TrickRabbitFreeRunNrvWaitStart)) ||
        isNerve(GET_NERVE(TrickRabbitFreeRun, TrickRabbitFreeRunNrvGiveUp))) {
        return mStateWaitStart->receiveOtherMsg(msg, pSender, pReceiver);
    }

    return false;
}

bool TrickRabbitFreeRun::receiveMsgBlowDamage(HitSensor* pSender, HitSensor* pReceiver) {
    if (isEnableBlowDamage()) {
        MR::setVelocitySeparateHV(this, pSender, pReceiver, cNum_receiveMsgBlowDamage_f1, cNum_receiveMsgBlowDamage_f2);
        setNerve(GET_NERVE(TrickRabbitFreeRun, TrickRabbitFreeRunNrvBlowDamage));

        return true;
    }

    return false;
}

bool TrickRabbitFreeRun::requestCaught() {
    if (isEnableCaught()) {
        MR::invalidateClipping(this);
        MR::forwardNode(mMsgCtrl);
        setNerve(GET_NERVE(TrickRabbitFreeRun, TrickRabbitFreeRunNrvCaught));

        return true;
    }

    return false;
}

void TrickRabbitFreeRun::exeWaitStart() {
    if (MR::isFirstStep(this)) {
        mStateWaitStart->setTalkActionName("StartEvent");
        mStateWaitStart->mIsKillAfterTalk = true;
    }

    if (MR::updateActorState(this, mStateWaitStart)) {
        MR::requestStartDemoMarioPuppetable(this, "逃走", GET_NERVE(TrickRabbitFreeRun, TrickRabbitFreeRunNrvRunawayStart),
                                            GET_NERVE(TrickRabbitFreeRun, TrickRabbitFreeRunNrvTryDemo));
    }
}

void TrickRabbitFreeRun::exeTryDemo() {
}

void TrickRabbitFreeRun::exeRunawayStart() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayer("BattleWait");
        MR::startAction(this, "Run");
        MR::startEventBGM(MR::EventBgmID_Chase);
    }

    MR::escapeFromPlayer(this, &mFrontVec, ::sStartAwayParam);

    if (MR::checkPassBckFrame(this, cNum_exeRunawayStart_checkPassBckFrame_1)) {
        startJumpSound();
    }

    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(2))) {
        MR::endDemo(this, "逃走");
        setNerve(GET_NERVE(TrickRabbitFreeRun, TrickRabbitFreeRunNrvRunaway));
    }
}

void TrickRabbitFreeRun::exeRunaway() {
    MR::updateActorState(this, mStateRunaway);

    if (mStateRunaway->isRunning() && MR::checkPassBckFrame(this, cNum_exeRunaway_checkPassBckFrame_1)) {
        startJumpSound();
    }

    f32 speed = ::sNormalAccel;

    if (MR::isNearPlayerHorizontal(this, ::sPressureRange)) {
        speed = ::sPressureAccel;
    }

    if (MR::isBindedGroundWater(this) || MR::isBindedGroundBrake(this)) {
        speed *= ::sBrakeAccelRate;
    }

    mStateRunaway->mRunawaySpeed = speed;

    if (MR::isBindedGround(this)) {
        mFootPrint->addPrint(mPosition, mFrontVec, *MR::getGroundNormal(this), false);
    }
}

void TrickRabbitFreeRun::exeBlowDamage() {
    MR::updateActorStateAndNextNerve(this, mStateBlowDamage, GET_NERVE(TrickRabbitFreeRun, TrickRabbitFreeRunNrvRunaway));
}

void TrickRabbitFreeRun::exeCaught() {
    if (MR::isFirstStep(this)) {
        MR::stopStageBGM(60);
    }

    if (MR::updateActorState(this, mStateCaught)) {
        setNerve(GET_NERVE(TrickRabbitFreeRun, TrickRabbitFreeRunNrvPowerStarDemo));
    }
}

void TrickRabbitFreeRun::exePowerStarDemo() {
    if (MR::isFirstStep(this)) {
        MR::endDemo(this, "捕まり");
        MR::startAfterBossBGM();
        MR::requestAppearPowerStar(this, mPosition - mGravity * cNum_exePowerStarDemo_f1);
        MR::startAction(this, "Wait");
        mFootPrint->clear();
        mSpotMarkLight->kill();
    }

    MR::zeroVelocity(this);

    if (MR::isEndPowerStarAppearDemo(this)) {
        MR::forwardNode(mMsgCtrl);
        setNerve(GET_NERVE(TrickRabbitFreeRun, TrickRabbitFreeRunNrvGiveUp));
    }
}

void TrickRabbitFreeRun::exeGiveUp() {
    if (MR::isFirstStep(this)) {
        mStateWaitStart->setTalkActionName("GiveUpTalk");
        mStateWaitStart->mIsKillAfterTalk = false;
    }

    MR::updateActorState(this, mStateWaitStart);
}

bool TrickRabbitFreeRun::isEnableCaught() const {
    if (isNerve(GET_NERVE(TrickRabbitFreeRun, TrickRabbitFreeRunNrvRunaway)) ||
        isNerve(GET_NERVE(TrickRabbitFreeRun, TrickRabbitFreeRunNrvBlowDamage))) {
        return true;
    }

    return false;
}

bool TrickRabbitFreeRun::isEnableBlowDamage() const {
    return isNerve(GET_NERVE(TrickRabbitFreeRun, TrickRabbitFreeRunNrvRunaway));
}

void TrickRabbitFreeRun::startJumpSound() {
    if (MR::isBindedGroundWater(this)) {
        MR::startSound(this, "SE_SM_RABBIT_JUMP_WATER");
    } else if (MR::isBindedGroundBrake(this)) {
        MR::startSound(this, "SE_SM_RABBIT_JUMP_WATER");
    } else {
        MR::startSound(this, "SE_SM_RABBIT_JUMP");
    }
}
