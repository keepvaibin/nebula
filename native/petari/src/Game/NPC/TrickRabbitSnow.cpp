#include "Game/NPC/TrickRabbitSnow.hpp"
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
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 1500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 120.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHideSnow_f1, "exeHideSnow.f1", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAppearFromSnow_f1, "exeAppearFromSnow.f1", 20.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_exeWaitSnow_turnDirectionToPlayerDegree_1, "exeWaitSnow.turnDirectionToPlayerDegree.1", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeStartHideSnow_checkPassBckFrame_1, "exeStartHideSnow.checkPassBckFrame.1", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAppearFromSnow_setVelocityJump_1, "exeAppearFromSnow.setVelocityJump.1", 25.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAppearFromSnow_addVelocityToGravity_1, "exeAppearFromSnow.addVelocityToGravity.1", 1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRunaway_checkPassBckFrame_1, "exeRunaway.checkPassBckFrame.1", 3.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeAppearFromSnow_1, "exeAppearFromSnow.1", 5);

TrickRabbitSnowListener::TrickRabbitSnowListener() {
}

namespace {
    static MR::ActorMoveParam sStartAwayParam = {1.4f, 1.0f, 0.9f, 4.0f};
    NEBULA_TUNABLE(f32, sBodyRadius, 70.0f);
    NEBULA_TUNABLE(f32, sCatchRadius, 30.0f);
    NEBULA_TUNABLE(f32, sBindRadius, 60.0f);
    NEBULA_TUNABLE(f32, sBindYOffset, 60.0f);
    NEBULA_TUNABLE(f32, sUpVecBlendRate, 0.1f);
    NEBULA_TUNABLE(f32, sFrontVecBlendRate, 0.2f);
    NEBULA_TUNABLE(f32, sStarPieceHitAccelH, 10.0f);
    NEBULA_TUNABLE(f32, sStarPieceHitAccelV, 40.0f);
    NEBULA_TUNABLE(f32, sAppearSnowJumpV, 20.0f);
    NEBULA_TUNABLE(f32, sAppearSnowJumpH, 20.0f);
    NEBULA_TUNABLE(f32, sPressureRange, 600.0f);
    NEBULA_TUNABLE(f32, sNormalAccel, 0.7f);
    NEBULA_TUNABLE(f32, sPressureAccel, 1.2f);
    NEBULA_TUNABLE(f32, sBrakeAccelRate, 0.99f);
};  // namespace

namespace NrvTrickRabbitFreeRun {
    NEW_NERVE(TrickRabbitSnowNrvWaitSnow, TrickRabbitSnow, WaitSnow);
    NEW_NERVE(TrickRabbitSnowNrvStartHideSnow, TrickRabbitSnow, StartHideSnow);
    NEW_NERVE(TrickRabbitSnowNrvVanish, TrickRabbitSnow, Vanish);
    NEW_NERVE(TrickRabbitSnowNrvHideSnow, TrickRabbitSnow, HideSnow);
    NEW_NERVE(TrickRabbitSnowNrvAppearFromSnow, TrickRabbitSnow, AppearFromSnow);
    NEW_NERVE(TrickRabbitSnowNrvRunaway, TrickRabbitSnow, Runaway);
    NEW_NERVE(TrickRabbitSnowNrvBlowDamage, TrickRabbitSnow, BlowDamage);
    NEW_NERVE(TrickRabbitSnowNrvCaught, TrickRabbitSnow, Caught);
    NEW_NERVE(TrickRabbitSnowNrvGiveUp, TrickRabbitSnow, GiveUp);
};  // namespace NrvTrickRabbitFreeRun

TrickRabbitSnow::TrickRabbitSnow(const char* pName)
    : LiveActor(pName), mFinishQuat(0.0f, 0.0f, 0.0f, 1.0f), mFinishPos(0.0f, 0.0f, 0.0f), mRotateQuat(0.0f, 0.0f, 0.0f, 1.0f),
      mFrontVec(0.0f, 0.0f, 1.0f), mStateWaitStart(), mStateRunaway(), mStateBlowDamage(), mStateCaught(), mMsgCtrl(), mFootPrint(), mSpotMarkLight(),
      mListener(), mNotCaughtableTimer(), mIsValidAppearStarPiece(true) {
    lumacore::setClass(this, lumacore::kClassId< TrickRabbitSnow >);
}

void TrickRabbitSnow::setListener(TrickRabbitSnowListener* pListener) {
    mListener = pListener;
}

void TrickRabbitSnow::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("TrickRabbit", nullptr, false);
    MR::connectToSceneNpc(this);
    MR::initLightCtrl(this);
    MR::makeQuatAndFrontFromRotate(&mRotateQuat, &mFrontVec, this);
    mFinishQuat.set(mRotateQuat);
    mFinishPos.set(mPosition);

    mSpotMarkLight = new SpotMarkLight(this, cNum_init_f1, cNum_init_f2, nullptr);
    mSpotMarkLight->initWithoutIter();

    MR::initShadowFromCSV(this, "Shadow");

    mFootPrint = TrickRabbitUtil::createRabbitFootPrint(this);

    initEffectKeeper(0, nullptr);
    MR::onEmitEffectSyncClipping(this, "Light");
    MR::onForceDeleteEffectSyncClipping(this, "Light");
    initSensor();
    initBinder(::sBindRadius, ::sBindYOffset, 0);

    mMsgCtrl = MR::createTalkCtrl(this, rIter, "SnowRabbit", Vec3f(0.0f, cNum_init_f3, 0.0f), nullptr);

    initState();
    initSound(4, false);
    MR::onCalcGravity(this);
    MR::declareStarPiece(this, 6);
    initNerve(GET_NERVE(TrickRabbitFreeRun, TrickRabbitSnowNrvWaitSnow));
    MR::tryRegisterNamePosLinkObj(this, rIter);
    MR::useStageSwitchReadA(this, rIter);

    if (MR::useStageSwitchReadB(this, rIter)) {
        MR::listenStageSwitchOnB(this, MR::Functor(this, &TrickRabbitSnow::clearFootPrint));
    }

    makeActorAppeared();
}

void TrickRabbitSnow::initAfterPlacement() {
    MR::trySetMoveLimitCollision(this);
}

void TrickRabbitSnow::initSensor() {
    initHitSensor(2);
    MR::addHitSensorAtJointEnemy(this, "Body", "Spine", 8, ::sBodyRadius, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorAtJointEnemy(this, "Catch", "Spine", 8, ::sCatchRadius, Vec3f(0.0f, 0.0f, 0.0f));
    MR::initStarPointerTargetAtJoint(this, "Spine", ::sBodyRadius, Vec3f(0.0f));
}

void TrickRabbitSnow::initState() {
    mStateCaught = new RabbitStateCaught(this, mMsgCtrl);
    mStateCaught->unusePowerStarModel();
    mStateCaught->init();

    mStateWaitStart = new RabbitStateWaitStart(this, &mFrontVec, mMsgCtrl);
    mStateWaitStart->setTalkActionName("GiveUpTalk");
    mStateWaitStart->mIsKillAfterTalk = false;
    mStateWaitStart->mIsFallByGravity = false;
    mStateWaitStart->init();

    mStateRunaway = new WalkerStateRunaway(this, &mFrontVec, nullptr);
    mStateRunaway->init();

    mStateBlowDamage = new WalkerStateBlowDamage(this, &mFrontVec);
    mStateBlowDamage->init();
}

void TrickRabbitSnow::clearFootPrint() {
    mFootPrint->clearForce();
}

void TrickRabbitSnow::control() {
    if (mNotCaughtableTimer > 0) {
        mNotCaughtableTimer--;
    }

    MR::blendQuatUpFront(&mRotateQuat, -mGravity, mFrontVec, ::sUpVecBlendRate, ::sFrontVecBlendRate);
}

void TrickRabbitSnow::calcAndSetBaseMtx() {
    MR::setBaseTRMtx(this, mRotateQuat);
}

void TrickRabbitSnow::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(TrickRabbitFreeRun, TrickRabbitSnowNrvHideSnow))) {
        return;
    }

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

bool TrickRabbitSnow::receiveMsgPush(HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (isNerve(GET_NERVE(TrickRabbitFreeRun, TrickRabbitSnowNrvHideSnow))) {
        return false;
    }

    if (MR::isSensorPlayer(pSender)) {
        return !isEnableCaught();
    }

    return false;
}

bool TrickRabbitSnow::receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgToEnemyAttackBlow(msg)) {
        return receiveMsgBlowDamage(pSender, pReceiver);
    }

    return false;
}

bool TrickRabbitSnow::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(TrickRabbitFreeRun, TrickRabbitSnowNrvGiveUp))) {
        return mStateWaitStart->receiveMsgPlayerAttack(msg, pSender, pReceiver);
    }

    if (MR::isMsgStarPieceAttack(msg)) {
        MR::limitedStarPieceHitSound();
        MR::startSound(this, "SE_SM_RABBIT_STAR_PIECE_HIT");
        MR::startSound(this, "SE_SV_RABBIT_STAR_PIECE_HIT");

        if (receiveMsgBlowDamage(pSender, pReceiver)) {
            return true;
        }
    }

    if (MR::isMsgPlayerSpinAttack(msg)) {
        return requestCaught();
    }


    return false;
}

bool TrickRabbitSnow::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(TrickRabbitFreeRun, TrickRabbitSnowNrvGiveUp))) {
        return mStateWaitStart->receiveOtherMsg(msg, pSender, pReceiver);
    }

    if (!MR::isValidSwitchA(this)) {
        if (msg == ACTMES_ASK_HIDDEN_BY_SNOW) {
            return isNerve(GET_NERVE(TrickRabbitFreeRun, TrickRabbitSnowNrvHideSnow));
        }

        if (msg == ACTMES_NOTIFY_DISCOVER_SNOW) {
            if (isNerve(GET_NERVE(TrickRabbitFreeRun, TrickRabbitSnowNrvHideSnow))) {
                MR::showModel(this);
                setNerve(GET_NERVE(TrickRabbitFreeRun, TrickRabbitSnowNrvAppearFromSnow));

                return true;
            }
        }
    }

    return false;
}

bool TrickRabbitSnow::receiveMsgBlowDamage(HitSensor* pSender, HitSensor* pReceiver) {
    if (isEnableBlowDamage()) {
        MR::setVelocitySeparateHV(this, pSender, pReceiver, ::sAppearSnowJumpH, ::sAppearSnowJumpV);
        setNerve(GET_NERVE(TrickRabbitFreeRun, TrickRabbitSnowNrvBlowDamage));

        return true;
    }

    return false;
}

bool TrickRabbitSnow::requestCaught() {
    if (isEnableCaught()) {
        if (mListener != nullptr) {
            mListener->noticeCaught(this);
        }

        MR::tryPlayerDropTakingActor();
        MR::invalidateClipping(this);
        setNerve(GET_NERVE(TrickRabbitFreeRun, TrickRabbitSnowNrvCaught));

        return true;
    }

    return false;
}

bool TrickRabbitSnow::requestStartHideSnow() {
    if (isNerve(GET_NERVE(TrickRabbitFreeRun, TrickRabbitSnowNrvWaitSnow))) {
        MR::invalidateClipping(this);
        MR::requestMovementOn(this);
        setNerve(GET_NERVE(TrickRabbitFreeRun, TrickRabbitSnowNrvStartHideSnow));

        return true;
    }

    return false;
}

void TrickRabbitSnow::setFinishPosition() {
    mRotateQuat.set(mFinishQuat);
    mRotateQuat.getZDir(mFrontVec);
    mPosition.set(mFinishPos);
    MR::showModel(this);
    MR::resetPosition(this);
    MR::zeroVelocity(this);
    MR::invalidateClipping(this);
    MR::requestMovementOn(this);
    MR::forwardNode(mMsgCtrl);
    mStateWaitStart->mIsFallByGravity = false;
    setNerve(GET_NERVE(TrickRabbitFreeRun, TrickRabbitSnowNrvGiveUp));
}

void TrickRabbitSnow::setNotCaughtable() {
    mNotCaughtableTimer = 5;
}

void TrickRabbitSnow::exeWaitSnow() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "StartEvent");
    }

    MR::turnDirectionToPlayerDegree(this, &mFrontVec, cNum_exeWaitSnow_turnDirectionToPlayerDegree_1);
    MR::zeroVelocity(this);
}

void TrickRabbitSnow::exeStartHideSnow() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Run");
    }

    MR::escapeFromPlayer(this, &mFrontVec, ::sStartAwayParam);

    if (MR::checkPassBckFrame(this, cNum_exeStartHideSnow_checkPassBckFrame_1)) {
        startJumpSound();
    }

    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(2))) {
        setNerve(GET_NERVE(TrickRabbitFreeRun, TrickRabbitSnowNrvVanish));
    }
}

void TrickRabbitSnow::exeVanish() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Change");
        MR::startSound(this, "SE_SM_RABBIT_HIDE");
        MR::zeroVelocity(this);
    }

    if (!MR::isBckStopped(this)) {
        return;
    }

    MR::startSound(this, "SE_SM_METAMORPHOSE_SMOKE");

    Vec3f trans;
    Vec3f rotate;

    if (MR::tryFindLinkNamePos(this, "隠れ位置", &trans, &rotate)) {
        mPosition.set(trans);
        mRotation.set(rotate);
        MR::makeQuatAndFrontFromRotate(&mRotateQuat, &mFrontVec, this);
        MR::resetPosition(this);
    }

    MR::stopBck(this);
    MR::zeroVelocity(this);
    MR::hideModelAndOnCalcAnim(this);
    setNerve(GET_NERVE(TrickRabbitFreeRun, TrickRabbitSnowNrvHideSnow));
}

void TrickRabbitSnow::exeHideSnow() {
    if (MR::isFirstStep(this)) {
        MR::deleteEffectAll(this);
    }

    MR::zeroVelocity(this);
    MR::turnDirectionFromPlayerDegree(this, &mFrontVec, cNum_exeHideSnow_f1);

    if (MR::isValidSwitchA(this) && MR::isOnSwitchA(this)) {
        MR::showModel(this);
        setNerve(GET_NERVE(TrickRabbitFreeRun, TrickRabbitSnowNrvAppearFromSnow));
    }
}

void TrickRabbitSnow::exeAppearFromSnow() {
    if (MR::isFirstStep(this)) {
        MR::emitEffect(this, "AppearSmoke");
        MR::startAction(this, "Jump");
        MR::setVelocityJump(this, cNum_exeAppearFromSnow_setVelocityJump_1);
        MR::addVelocityMoveToDirection(this, mFrontVec, cNum_exeAppearFromSnow_f1);
        MR::startSound(this, "SE_SM_RABBIT_APPEAR");
        MR::startSystemSE("SE_SM_RUNAWAY_RABBIT_APP_ME");
    }

    MR::addVelocityToGravity(this, cNum_exeAppearFromSnow_addVelocityToGravity_1);
    MR::attenuateVelocity(this, ::sBrakeAccelRate);

    if (MR::isGreaterStep(this, cStep_exeAppearFromSnow_1)) {
        if (MR::isBindedGround(this)) {
            setNerve(GET_NERVE(TrickRabbitFreeRun, TrickRabbitSnowNrvRunaway));
        }
    }
}

void TrickRabbitSnow::exeRunaway() {
    MR::updateActorState(this, mStateRunaway);

    if (mStateRunaway->isRunning() && MR::checkPassBckFrame(this, cNum_exeRunaway_checkPassBckFrame_1)) {
        startJumpSound();
    }

    f32 speed = ::sNormalAccel;

    if (MR::isNearPlayerHorizontal(this, ::sPressureRange)) {
        speed = ::sPressureAccel;
    }

    mStateRunaway->mRunawaySpeed = speed;

    if (MR::isBindedGround(this)) {
        mFootPrint->addPrint(mPosition, mFrontVec, *MR::getGroundNormal(this), false);
    }
}

void TrickRabbitSnow::exeBlowDamage() {
    MR::updateActorStateAndNextNerve(this, mStateBlowDamage, GET_NERVE(TrickRabbitFreeRun, TrickRabbitSnowNrvRunaway));
}

void TrickRabbitSnow::exeCaught() {

    if (MR::updateActorState(this, mStateCaught)) {
        MR::endDemo(this, "捕まり");
        MR::forwardNode(mMsgCtrl);
        mSpotMarkLight->kill();
        mFootPrint->clear();
        MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
        mStateWaitStart->mIsFallByGravity = true;
        setNerve(GET_NERVE(TrickRabbitFreeRun, TrickRabbitSnowNrvGiveUp));

        if (mListener != nullptr) {
            mListener->noticeGiveUp(this);
        }

        if (mIsValidAppearStarPiece) {
            MR::appearStarPiece(this, mPosition, 6, ::sStarPieceHitAccelH, ::sStarPieceHitAccelV, false);
        }
    }
}

void TrickRabbitSnow::exeGiveUp() {
    MR::updateActorState(this, mStateWaitStart);
}

bool TrickRabbitSnow::isGiveUp() const {
    return isNerve(GET_NERVE(TrickRabbitFreeRun, TrickRabbitSnowNrvGiveUp));
}

bool TrickRabbitSnow::isHideSnow() const {
    return isNerve(GET_NERVE(TrickRabbitFreeRun, TrickRabbitSnowNrvHideSnow));
}

bool TrickRabbitSnow::isEnableCaught() const {
    if (mNotCaughtableTimer > 0) {
        return false;
    }

    if (isNerve(GET_NERVE(TrickRabbitFreeRun, TrickRabbitSnowNrvRunaway)) || isNerve(GET_NERVE(TrickRabbitFreeRun, TrickRabbitSnowNrvBlowDamage))) {
        return true;
    }

    return false;
}

bool TrickRabbitSnow::isEnableBlowDamage() const {
    return isNerve(GET_NERVE(TrickRabbitFreeRun, TrickRabbitSnowNrvRunaway));
}

void TrickRabbitSnow::startJumpSound() {
    if (MR::isBindedGroundWater(this)) {
        MR::startSound(this, "SE_SM_RABBIT_JUMP_WATER");
    } else if (MR::isBindedGroundBrake(this)) {
        MR::startSound(this, "SE_SM_RABBIT_JUMP_WATER");
    } else {
        MR::startSound(this, "SE_SM_RABBIT_JUMP");
    }
}
