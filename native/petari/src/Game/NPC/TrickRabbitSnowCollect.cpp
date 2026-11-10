#include "Game/NPC/TrickRabbitSnowCollect.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Screen/BombTimerLayout.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_initTalk_f1, "initTalk.f1", 120.0f);

namespace {
    NEBULA_TUNABLE(f32, sInStartEventRange, 700.0f);
};  // namespace

namespace NrvTrickRabbitFreeRunCollect {
    NEW_NERVE(TrickRabbitSnowCollectNrvTryDemo, TrickRabbitSnowCollect, TryDemo);
    NEW_NERVE(TrickRabbitSnowCollectNrvWait, TrickRabbitSnowCollect, Wait);
    NEW_NERVE(TrickRabbitSnowCollectNrvStartWipeOut, TrickRabbitSnowCollect, StartWipeOut);
    NEW_NERVE(TrickRabbitSnowCollectNrvStartWipeIn, TrickRabbitSnowCollect, StartWipeIn);
    NEW_NERVE(TrickRabbitSnowCollectNrvStartTalk, TrickRabbitSnowCollect, StartTalk);
    NEW_NERVE(TrickRabbitSnowCollectNrvWaitHideSnow, TrickRabbitSnowCollect, WaitHideSnow);
    NEW_NERVE(TrickRabbitSnowCollectNrvStart, TrickRabbitSnowCollect, Start);
    NEW_NERVE(TrickRabbitSnowCollectNrvFailedWipeOut, TrickRabbitSnowCollect, FailedWipeOut);
    NEW_NERVE(TrickRabbitSnowCollectNrvFailedWipeIn, TrickRabbitSnowCollect, FailedWipeIn);
    NEW_NERVE(TrickRabbitSnowCollectNrvFailedTalk, TrickRabbitSnowCollect, FailedTalk);
    NEW_NERVE(TrickRabbitSnowCollectNrvSuccessWipeOut, TrickRabbitSnowCollect, SuccessWipeOut);
    NEW_NERVE(TrickRabbitSnowCollectNrvSuccessWipeIn, TrickRabbitSnowCollect, SuccessWipeIn);
    NEW_NERVE(TrickRabbitSnowCollectNrvSuccessTalk, TrickRabbitSnowCollect, SuccessTalk);
    NEW_NERVE(TrickRabbitSnowCollectNrvTakeOutStar, TrickRabbitSnowCollect, TakeOutStar);
    NEW_NERVE(TrickRabbitSnowCollectNrvAppearPowerStar, TrickRabbitSnowCollect, AppearPowerStar);
    NEW_NERVE(TrickRabbitSnowCollectNrvEnd, TrickRabbitSnowCollect, End);
};  // namespace NrvTrickRabbitFreeRunCollect

TrickRabbitSnowCollect::TrickRabbitSnowCollect(const char* pName)
    : LiveActor(pName), mCameraInfo(), mPowerStarDemoModel(), mMsgCtrl(), mTimerLayout(), mRabbit(), mRabbitNum(), mTimeLimit(180), mIsDemo() {
    mBaseMtx.identity();
}

void TrickRabbitSnowCollect::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::getPlacementMatrixFromRT(rIter, &mBaseMtx);
    MR::connectToSceneNpcMovement(this);
    MR::invalidateClipping(this);
    MR::getPlacementArg0NoInit(rIter, &mTimeLimit);
    initRabbits(rIter);
    initTalk(rIter);

    mTimerLayout = new BombTimerLayout(true);
    mTimerLayout->initWithoutIter();

    MR::initMultiActorCamera(this, rIter, &mCameraInfo, "ウサギと会話");

    mPowerStarDemoModel = MR::createPowerStarDemoModel(this, "パワースターデモモデル", mRabbit[2]->getBaseMtx());
    mPowerStarDemoModel->kill();

    initNerve(GET_NERVE(TrickRabbitFreeRunCollect, TrickRabbitSnowCollectNrvWait));
    initSound(4, false);
    MR::declarePowerStar(this);
    MR::tryRegisterNamePosLinkObj(this, rIter);
    makeActorAppeared();
}

void TrickRabbitSnowCollect::initRabbits(const TableIter& rIter) {
    mRabbitNum = MR::getChildObjNum(rIter);
    mRabbit = new TrickRabbitSnow*[mRabbitNum];

    TrickRabbitCollectListener* const pListener = new TrickRabbitCollectListener(this);

    for (s32 i = 0; i < mRabbitNum; i++) {
        mRabbit[i] = new TrickRabbitSnow("雪ウサギ");
        mRabbit[i]->setListener(pListener);
        MR::initChildObj(mRabbit[i], rIter, i);
    }
}

void TrickRabbitSnowCollect::initTalk(const TableIter& rIter) {
    mMsgCtrl = MR::createTalkCtrl(this, rIter, "SnowRabbitCollect", Vec3f(0.0f, cNum_initTalk_f1, 0.0f), nullptr);
}

MtxPtr TrickRabbitSnowCollect::getBaseMtx() const {
    return const_cast< MtxPtr >(mBaseMtx.mMtx);
}

void TrickRabbitSnowCollect::setStartPosition() {
    for (s32 i = 0; i < mRabbitNum; i++) {
        MR::requestMovementOn(mRabbit[i]);
    }

    MR::setPlayerLinkPosAndWait(this, "マリオ位置");
}

void TrickRabbitSnowCollect::setFinishPosition() {
    for (s32 i = 0; i < mRabbitNum; i++) {
        mRabbit[i]->setFinishPosition();
    }

    MR::setPlayerLinkPosAndWait(this, "マリオ位置");
}

void TrickRabbitSnowCollect::noticeCaught(TrickRabbitSnow* pSubject) {
    for (s32 i = 0; i < mRabbitNum; i++) {
        if (mRabbit[i] != pSubject) {
            mRabbit[i]->setNotCaughtable();
        }
    }
}

void TrickRabbitSnowCollect::noticeGiveUp(TrickRabbitSnow* pSubject) {
    for (s32 i = 0; i < mRabbitNum; i++) {
        if (!mRabbit[i]->isGiveUp()) {
            return;
        }
    }

    pSubject->mIsValidAppearStarPiece = false;

    startSuccessDemo();
}

void TrickRabbitSnowCollect::startSuccessDemo() {
    if (mIsDemo) {
        return;
    }

    mIsDemo = true;

    MR::startSystemSE("SE_SY_TOTAL_COMPLETE");
    MR::requestStartDemoMarioPuppetableWithoutCinemaFrame(this, "雪ウサギ集め成功",
                                                          GET_NERVE(TrickRabbitFreeRunCollect, TrickRabbitSnowCollectNrvSuccessWipeOut),
                                                          GET_NERVE(TrickRabbitFreeRunCollect, TrickRabbitSnowCollectNrvTryDemo));
}

void TrickRabbitSnowCollect::startFailedDemo() {
    if (mIsDemo) {
        return;
    }

    mIsDemo = true;

    MR::requestStartDemoMarioPuppetableWithoutCinemaFrame(this, "雪ウサギ集め失敗",
                                                          GET_NERVE(TrickRabbitFreeRunCollect, TrickRabbitSnowCollectNrvFailedWipeOut),
                                                          GET_NERVE(TrickRabbitFreeRunCollect, TrickRabbitSnowCollectNrvTryDemo));
}

void TrickRabbitSnowCollect::exeTryDemo() {
}

void TrickRabbitSnowCollect::exeWait() {
    if (MR::isNearPlayer(this, ::sInStartEventRange) && MR::isOnGroundPlayer()) {
        setNerve(GET_NERVE(TrickRabbitFreeRunCollect, TrickRabbitSnowCollectNrvStartWipeOut));
    }
}

void TrickRabbitSnowCollect::exeStartWipeOut() {
    if (MR::isFirstStep(this)) {
        MR::closeWipeFade();
    }

    if (MR::isWipeActive()) {
        return;
    }

    MR::requestStartDemoMarioPuppetableWithoutCinemaFrame(this, "雪ウサギ開始",
                                                          GET_NERVE(TrickRabbitFreeRunCollect, TrickRabbitSnowCollectNrvStartWipeIn),
                                                          GET_NERVE(TrickRabbitFreeRunCollect, TrickRabbitSnowCollectNrvTryDemo));
}

void TrickRabbitSnowCollect::exeStartWipeIn() {
    if (MR::isFirstStep(this)) {
        MR::tryPlayerKillTakingActor();
        MR::startBckPlayer("BattleWait");
        MR::startMultiActorCameraTargetPlayer(this, mCameraInfo, "ウサギと会話", 0);
        setStartPosition();
        MR::openWipeFade();
    }

    if (MR::isWipeActive()) {
        return;
    }

    setNerve(GET_NERVE(TrickRabbitFreeRunCollect, TrickRabbitSnowCollectNrvStartTalk));
}

void TrickRabbitSnowCollect::exeStartTalk() {
    if (MR::isFirstStep(this)) {
        setStartPosition();
    }

    if (MR::tryTalkForceWithoutDemoMarioPuppetableAtEnd(mMsgCtrl)) {
        setNerve(GET_NERVE(TrickRabbitFreeRunCollect, TrickRabbitSnowCollectNrvWaitHideSnow));
    }
}

void TrickRabbitSnowCollect::exeWaitHideSnow() {
    if (MR::isFirstStep(this)) {
        for (s32 i = 0; i < mRabbitNum; i++) {
            mRabbit[i]->requestStartHideSnow();
        }
    }

    bool isAllHideSnow = true;
    for (s32 i = 0; i < mRabbitNum && isAllHideSnow; i++) {
        isAllHideSnow = mRabbit[i]->isHideSnow();
    }

    if (isAllHideSnow) {
        MR::endMultiActorCamera(this, mCameraInfo, "ウサギと会話", false);
        MR::endDemo(this, "雪ウサギ開始");
        setNerve(GET_NERVE(TrickRabbitFreeRunCollect, TrickRabbitSnowCollectNrvStart));
    }
}

void TrickRabbitSnowCollect::exeStart() {
    if (MR::isFirstStep(this)) {
        MR::startEventBGM(1);
        mTimerLayout->setTimeLimit(NebulaFrameClock::stepsOfSeconds(mTimeLimit));
        mTimerLayout->mDangerTransFrame = NebulaFrameClock::stepsOfSeconds(20);
        mTimerLayout->appear();
    }

    if (!MR::isPlayerDead() && !MR::isPlayerConfrontDeath()) {
        u32 restTime = mTimerLayout->getRestTime();

        if (restTime == 0) {
            MR::startSystemSE("SE_SY_E3_TIMER_TIME_UP");
        } else if (restTime <= NebulaFrameClock::stepsOfSeconds(2)) {
            if (restTime % NebulaFrameClock::stepsOfSeconds(1) == 0) {
                MR::startSystemSE("SE_SY_E3_TIMER_COUNT_4");
            }
        } else if (restTime <= NebulaFrameClock::stepsOfSeconds(6)) {
            if (restTime % NebulaFrameClock::stepsOfSeconds(1) == 0) {
                MR::startSystemSE("SE_SY_E3_TIMER_COUNT_2");
            }
        } else if (restTime <= NebulaFrameClock::stepsOfSeconds(20)) {
            if (restTime % NebulaFrameClock::stepsOfSeconds(1) == 0) {
                MR::startSystemSE("SE_SY_E3_TIMER_COUNT_1");
            }
        }
    }

    if (mTimerLayout->isReadyToTimeUp()) {
        startFailedDemo();
    }
}

void TrickRabbitSnowCollect::exeFailedWipeOut() {
    if (MR::isFirstStep(this)) {
        MR::stopStageBGM(60);
        MR::closeWipeFade();
    }

    if (MR::isWipeActive()) {
        return;
    }

    MR::curePlayerElementMode();
    MR::tryPlayerKillTakingActor();
    mTimerLayout->kill();
    setNerve(GET_NERVE(TrickRabbitFreeRunCollect, TrickRabbitSnowCollectNrvFailedWipeIn));
}

void TrickRabbitSnowCollect::exeFailedWipeIn() {
    if (MR::isFirstStep(this)) {
        setFinishPosition();
        MR::startBckPlayer("BattleWait");
        MR::startMultiActorCameraTargetPlayer(this, mCameraInfo, "ウサギと会話", 0);
        MR::startLastStageBGM();
        MR::openWipeFade();
    }

    if (MR::isWipeActive()) {
        return;
    }

    MR::forwardNodeNextBranchRight(mMsgCtrl);
    setNerve(GET_NERVE(TrickRabbitFreeRunCollect, TrickRabbitSnowCollectNrvFailedTalk));
}

void TrickRabbitSnowCollect::exeFailedTalk() {
    if (MR::tryTalkForceWithoutDemoMarioPuppetableAtEnd(mMsgCtrl)) {
        MR::endMultiActorCamera(this, mCameraInfo, "ウサギと会話", true);
        MR::endDemo(this, "雪ウサギ集め失敗");
        MR::startSoundPlayer("SE_PM_LAST_DAMAGE");
        MR::forceKillPlayerByGroundRace();
        setNerve(GET_NERVE(TrickRabbitFreeRunCollect, TrickRabbitSnowCollectNrvEnd));
    }
}

void TrickRabbitSnowCollect::exeSuccessWipeOut() {
    if (MR::isFirstStep(this)) {
        MR::stopStageBGM(120);
        MR::closeWipeFade();
    }

    if (MR::isWipeActive()) {
        return;
    }

    MR::curePlayerElementMode();
    MR::tryPlayerKillTakingActor();
    setFinishPosition();
    setNerve(GET_NERVE(TrickRabbitFreeRunCollect, TrickRabbitSnowCollectNrvSuccessWipeIn));
}

void TrickRabbitSnowCollect::exeSuccessWipeIn() {
    if (MR::isFirstStep(this)) {
        mTimerLayout->kill();
        MR::startBckPlayer("BattleWait");
        MR::startMultiActorCameraTargetPlayer(this, mCameraInfo, "ウサギと会話");
        MR::startLastStageBGM();
        MR::openWipeFade();
    }

    if (MR::isWipeActive()) {
        return;
    }

    MR::forwardNodeNextBranchLeft(mMsgCtrl);
    setNerve(GET_NERVE(TrickRabbitFreeRunCollect, TrickRabbitSnowCollectNrvSuccessTalk));
}

void TrickRabbitSnowCollect::exeSuccessTalk() {
    if (MR::tryTalkForceWithoutDemoMarioPuppetableAtEnd(mMsgCtrl)) {
        setNerve(GET_NERVE(TrickRabbitFreeRunCollect, TrickRabbitSnowCollectNrvTakeOutStar));
    }
}

void TrickRabbitSnowCollect::exeTakeOutStar() {
    if (MR::isFirstStep(this)) {
        mPowerStarDemoModel->appear();
        MR::requestMovementOn(mPowerStarDemoModel);
        MR::startAction(mRabbit[2], "TakeOutStarTrickRabbit");
        MR::startAction(mPowerStarDemoModel, "TakeOutStarTrickRabbit");
    }

    if (MR::isActionEnd(mRabbit[2])) {
        MR::startAction(mRabbit[2], "Wait");
        setNerve(GET_NERVE(TrickRabbitFreeRunCollect, TrickRabbitSnowCollectNrvAppearPowerStar));
    }
}

void TrickRabbitSnowCollect::exeAppearPowerStar() {
    if (MR::isFirstStep(this)) {
        mPowerStarDemoModel->kill();

        Vec3f jointPos;
        MR::copyJointPos(mPowerStarDemoModel, "PowerStarC", &jointPos);
        MR::appearPowerStarContinueCurrentDemo(this, jointPos);

        MR::startAfterBossBGM();
    }

    if (MR::isEndPowerStarAppearDemo(this)) {
        MR::endMultiActorCamera(this, mCameraInfo, "ウサギと会話", true);
        MR::endDemo(this, "雪ウサギ集め成功");
        setNerve(GET_NERVE(TrickRabbitFreeRunCollect, TrickRabbitSnowCollectNrvEnd));
    }
}

void TrickRabbitSnowCollect::exeEnd() {
}
