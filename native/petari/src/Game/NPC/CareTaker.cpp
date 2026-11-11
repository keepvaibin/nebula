#include "Game/NPC/CareTaker.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Effect/EffectSystem.hpp"
#include "Game/Effect/EffectSystemUtil.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/NPC/NPCActorItem.hpp"
#include "Game/NPC/TalkMessageFunc.hpp"
#include "Game/Screen/BombTimerLayout.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 0.05f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f6, "init.f6", 350.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setAnim_f1, "setAnim.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f1, "exeWait.f1", 1.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePstWipeOut_f1, "exePstWipeOut.f1", 1.3f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_setRailCoordSpeed_1, "exeWait.setRailCoordSpeed.1", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePreWipeOut_setRailCoordSpeed_1, "exePreWipeOut.setRailCoordSpeed.1", 0.0f);

namespace {
    static const s32 sTidyTimeLimit = NebulaFrameClock::stepsOfSeconds(30);
    NEBULA_TUNABLE(s32, sBgmStartStep, 166);
    NEBULA_TUNABLE(s32, sPreRaceWait, 100);
    NEBULA_TUNABLE(s32, sPstRaceWait, 50);
};  // namespace

namespace NrvCaretaker {
    NEW_NERVE(CaretakerNrvTakeOutStar, Caretaker, TakeOutStar);
    NEW_NERVE(CaretakerNrvReaction, Caretaker, Reaction);
    NEW_NERVE(CaretakerNrvTalk, Caretaker, Talk);
    NEW_NERVE(CaretakerNrvWait, Caretaker, Wait);
    NEW_NERVE(CaretakerNrvPreTalk, Caretaker, PreTalk);
    NEW_NERVE(CaretakerNrvPreWipeOut, Caretaker, PreWipeOut);
    NEW_NERVE(CaretakerNrvPreWipeIn, Caretaker, PreWipeIn);
    NEW_NERVE(CaretakerNrvPreWait, Caretaker, PreWait);
    NEW_NERVE(CaretakerNrvCountDown, Caretaker, CountDown);
    NEW_NERVE(CaretakerNrvTidy, Caretaker, Tidy);
    NEW_NERVE(CaretakerNrvRunaway, Caretaker, Runaway);
    NEW_NERVE(CaretakerNrvPstWipeOut, Caretaker, PstWipeOut);
    NEW_NERVE(CaretakerNrvPstWipeIn, Caretaker, PstWipeIn);
    NEW_NERVE(CaretakerNrvPstWait, Caretaker, PstWait);
    NEW_NERVE(CaretakerNrvPstTalk, Caretaker, PstTalk);
    NEW_NERVE(CaretakerNrvAppearedStar, Caretaker, AppearedStar);
};  // namespace NrvCaretaker

Caretaker::Caretaker(const char* pName) : NPCActor(pName), mBombTimerLayout(), mTidyTimeLimit(::sTidyTimeLimit) {
    lumacore::setClass(this, lumacore::kClassId< Caretaker >);
}

void Caretaker::init(const TableIter& rIter) {
    NPCActorCaps caps = NPCActorCaps("Caretaker");
    NPCActorItem item = NPCActorItem("Caretaker");
    caps.setDefault();
    caps.mSoundSize = 8;
    caps.mRailRider = true;
    caps.mUseShadow = 1;
    caps.mWaitNerve = GET_NERVE(Caretaker, CaretakerNrvTalk);
    caps.mSensorSize = cNum_init_f1;
    initialize(rIter, caps);
    s32 bodyColorFrame = 0;
    MR::getPlacementArg3NoInit(rIter, &bodyColorFrame);

    s32 talkAnim = 0;
    MR::getPlacementArg4NoInit(rIter, &talkAnim);

    MR::startBrk(this, "BodyColor");
    MR::setBrkFrameAndStop(this, bodyColorFrame);
    MR::startBtk(this, "Dirt");
    MR::setBtkFrameAndStop(this, 0.0f);

    setDefaults3("BTalkHelp", "BSpinHit", "BSpinHit", "BTrampled");

    mParam.setMoveAction("BWaitStand", "BWaitStand");
    setTalkAction("BWaitRun", "BRunTalk");
    mParam.mTurnWhenNear = 0;
    mParam.mTurnOnTalk = 1;
    mParam.mTurnSpeed = cNum_init_f2;
    mRailSpeed = cNum_init_f3;
    mRailAccel = cNum_init_f4;
    mRailPoseRate = cNum_init_f5;
    mIsRailOnGround = 1;

    setAnim(talkAnim);

    // Obj_arg0 -1 or 1: a plain helper; anything else runs the tidy-up star game.
    s32 starGame = -1;
    MR::getPlacementArg0NoInit(rIter, &starGame);

    MR::startBckNoInterpole(this, "Wait");
    MR::calcAnimDirect(this);

    if (mMsgCtrl != nullptr) {
        MR::registerBranchFunc(getMsgCtrl(), TalkMessageFunc(this, &Caretaker::branchFuncComet));
        MR::registerEventFunc(getMsgCtrl(), TalkMessageFunc(this, &Caretaker::eventFuncComet));
    }

    if (starGame != -1 && starGame != 1) {
        setDefaults4("TalkAngry", "SpinHit", "Trampled", "SpinHit");
        mParam.setMoveAction("Wait", "Wait");
        mParam.setTalkAction("TalkNormal", "TalkNormal");
        setTalkAction("WaitRun");

        MR::getNPCItemData(&item, 0);
        equipment(item, false);

        if (MR::isExistRail(this)) {
            MR::moveCoordAndFollowTrans(this);
            mInitPos.set(mPosition);
        }

        setNerve(GET_NERVE(Caretaker, CaretakerNrvWait));
        MR::needStageSwitchReadA(this, rIter);
        MR::needStageSwitchWriteB(this, rIter);
        MR::declarePowerStar(this);

        mTakeOutStar = new TakeOutStar(this, "TakeOutStarCaretaker", "TakeOutStarCaretaker", GET_NERVE(Caretaker, CaretakerNrvTakeOutStar));

        if (MR::isPlayerLuigi()) {
            MR::getPlacementArg2NoInit(rIter, &mTidyTimeLimit);
        } else {
            MR::getPlacementArg1NoInit(rIter, &mTidyTimeLimit);
        }

        mBombTimerLayout = new BombTimerLayout(true);
        mBombTimerLayout->initWithoutIter();
        mBombTimerLayout->setTimeLimit(NebulaFrameClock::stepsOfSeconds(mTidyTimeLimit));
        mBombTimerLayout->kill();

        MR::joinToGroupArray(this, rIter, "ゴミ管理", 32);

        if (mMsgCtrl != nullptr) {
            MR::registerBranchFunc(getMsgCtrl(), TalkMessageFunc(this, &Caretaker::branchFuncStar));
            MR::registerAnimeFunc(getMsgCtrl(), TalkMessageFunc(this, &Caretaker::animeFunc));
            MR::setMessageArg(mMsgCtrl, mTidyTimeLimit);
            MR::setDistanceToTalk(mMsgCtrl, cNum_init_f6);
        }

        mCameraInfo = MR::createActorCameraInfo(rIter);

        MR::initActorCamera(this, rIter, &mCameraInfo);
    }
}

void Caretaker::initAfterPlacement() {
    if (MR::isExistRail(this)) {
        MR::followRailPoseOnGround(this, 1.0f);
    }
}

void Caretaker::kill() {
    NPCActor::kill();

    if (mBombTimerLayout != nullptr) {
        mBombTimerLayout->kill();
    }
}

void Caretaker::control() {
    MR::startLevelSound(this, "SE_SM_LV_CARETAKER_WAIT");
    NPCActor::control();
}

bool Caretaker::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgPlayerSpinAttack(msg)) {
        mIsSpinAttacked = 1;

        return true;
    }

    return NPCActor::receiveMsgPlayerAttack(msg, pSender, pReceiver);
}

bool Caretaker::branchFuncStar(u32 msg) {
    switch (msg) {
    case 0:
        return MR::isOnSwitchA(this);
    default:
        return false;
    }
}

bool Caretaker::branchFuncComet(u32 msg) {
    s32 purpleCoinNum = MR::getPurpleCoinNum();

    switch (msg) {
    case 0:
        return purpleCoinNum == 0;
    case 1:
        return purpleCoinNum >= 1 && purpleCoinNum < 50;
    case 2:
        return purpleCoinNum >= 50 && purpleCoinNum < 80;
    case 3:
        return purpleCoinNum >= 80 && purpleCoinNum < 90;
    case 4:
        return purpleCoinNum >= 90 && purpleCoinNum < 95;
    case 5:
        return purpleCoinNum >= 95 && purpleCoinNum < 100;
    case 6:
        return purpleCoinNum == 100;
    default:
        return false;
    }
}

bool Caretaker::eventFuncComet(u32 msg) {
    if (msg == 0) {
        return mTakeOutStar->takeOut();
    }

    if (msg == 1) {
        MR::forceKillPlayerByGroundRace();

        return true;
    }

    if (msg == 2) {
        TalkMessageCtrl* msgCtrl = mMsgCtrl;

        MR::setMessageArg(msgCtrl, MR::getPurpleCoinNum());
    }

    return true;
}

bool Caretaker::animeFunc(u32 msg) {
    switch (msg) {
    case 0:
        MR::startAction(this, "TalkNormal");
        break;
    case 1:
        MR::startAction(this, "TalkAngry");
        break;
    case 2:
        MR::startAction(this, "TalkCry");
        break;
    }

    return true;
}

void Caretaker::setAnim(s32 anim) {
    switch (anim) {
    case 0:
        mParam.setTalkAction("BTalkNormal", "BTalkNormal");
        break;
    case 1:
        mParam.setTalkAction("BTalkCry", "BTalkCry");
        break;
    case 2:
        mParam.setTalkAction("BTalkSpin", "BTalkSpin");
        break;
    case 3:
        mParam.setTalkAction("BTalkSurprise", "BTalkSurprise");
        break;
    case 4:
        mParam.setTalkAction("BTalkSpring", "BTalkSpring");
        break;
    case 5:
        mParam.setSingleAction("BTalkHelp");
        break;
    case 6:
        MR::invalidateLodCtrl(this);
        MR::setBtkFrameAndStop(this, cNum_setAnim_f1);
        mParam.setMoveTalkNoTurnAction("BTalkSurvive", "BTalkSurvive");
        break;
    }
}

void Caretaker::updateCounterSE() {
    if (MR::isPlayerDead()) {
        return;
    }

    if (MR::isPlayerConfrontDeath()) {
        return;
    }

    u32 restTime = mBombTimerLayout->getRestTime();

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
    } else if (restTime <= mBombTimerLayout->mDangerTransFrame) {
        if (restTime % NebulaFrameClock::stepsOfSeconds(1) == 0) {
            MR::startSystemSE("SE_SY_E3_TIMER_COUNT_1");
        }
    }
}

void Caretaker::exeTakeOutStar() {
}

void Caretaker::exeReaction() {
    if (mIsTrampledStart) {
        MR::startSound(this, "SE_SM_NPC_TRAMPLED_METAL");
        MR::startSound(this, "SE_SV_CARE_TAKER_TRAMPLE");
    }

    if (mIsSpinAttackedStart) {
        MR::startSound(this, "SE_SM_CARETAKER_SPIN_HIT");
        MR::startSound(this, "SE_SV_CARE_TAKER_SPIN_HIT");
    }

    if (isPointingSe()) {
        MR::startSound(this, "SE_SV_CARE_TAKER_POINTED2P");
        MR::startPointerHitSound();
    }

    if (mIsReactionStart) {
        MR::startSound(this, "SE_SM_CARETAKER_SPIN_HIT");
        MR::startSound(this, "SE_SV_CARE_TAKER_STAR_PIECE");
    }

    if (MR::tryStartReactionAndPopNerve(this)) {
        return;
    }
}

void Caretaker::exeTalk() {
    MR::tryTalkNearPlayerAndStartMoveTalkAction(this);
    MR::tryStartReactionAndPushNerve(this, GET_NERVE(Caretaker, CaretakerNrvReaction));
}

void Caretaker::exeWait() {
    if (MR::tryTalkNearPlayerAndStartMoveTalkAction(this)) {
        MR::startNPCTalkCamera(getMsgCtrl(), getBaseMtx(), cNum_exeWait_f1, -1);
        MR::setRailCoordSpeed(this, cNum_exeWait_setRailCoordSpeed_1);
        setNerve(GET_NERVE(Caretaker, CaretakerNrvPreTalk));
    } else if (MR::tryStartReactionAndPushNerve(this, GET_NERVE(Caretaker, CaretakerNrvReaction))) {
        return;
    }
}

void Caretaker::exePreTalk() {
    turnToPlayer(mParam.mTurnSpeed);

    if (!MR::tryTalkNearPlayerAtEnd(mMsgCtrl)) {
        return;
    }

    if (MR::tryTalkSelectLeft(mMsgCtrl)) {
        MR::tryStartDemoMarioPuppetableWithoutCinemaFrame(this, "ゴミ掃除タイムアタック");
        MR::startAction(this, "Wait");
        MR::startBckPlayer("Watch");
        setNerve(GET_NERVE(Caretaker, CaretakerNrvPreWipeOut));
    } else {
        MR::endNPCTalkCamera(false);
        setNerve(GET_NERVE(Caretaker, CaretakerNrvWait));
    }
}

void Caretaker::exePreWipeOut() {
    if (MR::isFirstStep(this)) {
        MR::stopStageBGM(90);
        MR::invalidateClipping(this);
        MR::closeWipeFade();
    }

    if (MR::isWipeActive()) {
        return;
    }

    MR::setRailCoordSpeed(this, cNum_exePreWipeOut_setRailCoordSpeed_1);
    MR::setRailCoord(this, 0.0f);
    MR::setDefaultPose(this);
    MR::setPlayerPosOnGroundAndWait("バトルシップ・タイムアタック前位置");
    MR::endNPCTalkCamera(false, 0);
    MR::callInvalidateClippingAllGroupMember(this);
    mBombTimerLayout->appear();
    mBombTimerLayout->suspend();
    mBombTimerLayout->setTimeLimit(NebulaFrameClock::stepsOfSeconds(mTidyTimeLimit));
    setNerve(GET_NERVE(Caretaker, CaretakerNrvPreWipeIn));
}

void Caretaker::exePreWipeIn() {
    if (MR::isFirstStep(this)) {
        MR::openWipeFade();
        MR::startSubBGM("BGM_MINIGAME_START", false);
    }

    if (MR::isWipeActive()) {
        return;
    }

    setNerve(GET_NERVE(Caretaker, CaretakerNrvPreWait));
}

void Caretaker::exePreWait() {
    if (MR::isFirstStep(this)) {
        MR::endDemo(this, "ゴミ掃除タイムアタック");
        MR::offPlayerControl();
        MR::onSwitchB(this);
    }

    if (MR::isGreaterStep(this, ::sPreRaceWait)) {
        setNerve(GET_NERVE(Caretaker, CaretakerNrvCountDown));
    }
}

void Caretaker::exeCountDown() {
    if (getNerveStep() % NebulaFrameClock::stepsOfSeconds(1) == 0) {
        MR::startSystemSE("SE_SY_RACE_COUNT_DOWN");
    }

    if (MR::isStep(this, ::sBgmStartStep)) {
        MR::startStageBGM("MBGM_RACE_01", false);
    }

    if (MR::isGreaterEqualStep(this, NebulaFrameClock::stepsOfSeconds(3))) {
        MR::onPlayerControl(true);
        setNerve(GET_NERVE(Caretaker, CaretakerNrvTidy));
    }
}

void Caretaker::exeTidy() {
    if (MR::isFirstStep(this)) {
        mBombTimerLayout->resume();
        MR::startSystemSE("SE_SY_RACE_START");
    }

    if (MR::isPlayerInBind()) {
        mBombTimerLayout->kill();
        MR::stopStageBGM(60);
        setNerve(GET_NERVE(Caretaker, CaretakerNrvRunaway));
    } else {
        updateCounterSE();

        if (MR::canStartDemo() && (mBombTimerLayout->isReadyToTimeUp() || MR::isOnSwitchA(this))) {
            MR::tryStartDemoMarioPuppetableWithoutCinemaFrame(this, "ゴミ掃除タイムアタック");
            setNerve(GET_NERVE(Caretaker, CaretakerNrvPstWipeOut));
        }
    }
}

void Caretaker::exeRunaway() {
    if (MR::isPlayerInBind()) {
        return;
    }

    MR::offSwitchB(this);
    MR::validateClipping(this);
    MR::callAppearAllGroupMember(this);
    MR::callValidateClippingAllGroupMember(this);
    MR::resetNode(mMsgCtrl);
    setNerve(GET_NERVE(Caretaker, CaretakerNrvWait));
}

void Caretaker::exePstWipeOut() {
    if (MR::isFirstStep(this)) {
        if (MR::isOnSwitchA(this)) {
            MR::startSystemSE("SE_SY_TOTAL_COMPLETE");
            MR::stopStageBGM(10);
        } else {
            MR::stopStageBGM(60);
        }

        MR::closeWipeFade();
    }

    if (MR::isWipeActive()) {
        return;
    }

    MR::offSwitchB(this);

    if (MR::isOnSwitchA(this)) {
        MR::callKillAllGroupMember(this);
    } else {
        MR::callAppearAllGroupMember(this);
    }

    MR::callValidateClippingAllGroupMember(this);
    MR::Effect::forceDeleteAllEmitters(MR::getEffectSystem());
    MR::tryPlayerKillTakingActor();
    MR::setPlayerPosOnGroundAndWait("バトルシップ・タイムアタック後位置");
    MR::startBckPlayer("Watch");
    MR::startNPCTalkCamera(getMsgCtrl(), getBaseMtx(), cNum_exePstWipeOut_f1, 1);
    setNerve(GET_NERVE(Caretaker, CaretakerNrvPstWipeIn));
}

void Caretaker::exePstWipeIn() {
    if (MR::isFirstStep(this)) {
        mBombTimerLayout->kill();
        MR::openWipeFade();
        MR::startLastStageBGM();
    }

    if (MR::isWipeActive()) {
        return;
    }

    MR::startAction(this, "TalkNormal");
    MR::validateClipping(this);
    setNerve(GET_NERVE(Caretaker, CaretakerNrvPstWait));
}

void Caretaker::exePstWait() {
    if (MR::isGreaterEqualStep(this, ::sPstRaceWait)) {
        setNerve(GET_NERVE(Caretaker, CaretakerNrvPstTalk));
    }
}

void Caretaker::exePstTalk() {
    if (!MR::tryTalkForceWithoutDemoMarioPuppetableAtEnd(mMsgCtrl)) {
        return;
    }

    MR::endDemo(this, "ゴミ掃除タイムアタック");
    MR::endNPCTalkCamera(false);

    if (MR::isOnSwitchA(this)) {
        setNerve(GET_NERVE(Caretaker, CaretakerNrvAppearedStar));
    } else {
        setNerve(GET_NERVE(Caretaker, CaretakerNrvWait));
    }
}

void Caretaker::exeAppearedStar() {
    MR::tryTalkNearPlayerAndStartTalkAction(this);
    MR::tryStartReactionAndPushNerve(this, GET_NERVE(Caretaker, CaretakerNrvReaction));
}
