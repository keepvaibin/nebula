#include "Game/NPC/RunawayTico.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/LiveActor/LodCtrl.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_exeGuide0_f1, "exeGuide0.f1", 100.0f);

namespace {
    NEBULA_TUNABLE(s32, sDemoCameraZoomOutStartFrame, 630);
    NEBULA_TUNABLE(s32, sDemoCameraZoomOutEndFrame, 730);
    NEBULA_TUNABLE(s32, sDemoCameraZoomOutBgmStartFrame, 617);
    NEBULA_TUNABLE(s32, sDemoFadeOut, 90);
    NEBULA_TUNABLE(s32, sDemoFadeIn, 75);
    static const s32 sDemoFadeWait = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(f32, sAwayYOffset, -49.7357f);
    static const s32 sWaitSeStartFrame = NebulaFrameClock::stepsOfMilliseconds(3500);
    NEBULA_TUNABLE(s32, sFloatSeStartFrame, 560);
    NEBULA_TUNABLE(s32, sAppearSeStep, 23);
};  // namespace

namespace NrvRunawayTico {
    NEW_NERVE(RunawayTicoNrvGuide0, RunawayTico, Guide0);
    NEW_NERVE(RunawayTicoNrvGuide1, RunawayTico, Guide1);
    NEW_NERVE(RunawayTicoNrvWhiteIn, RunawayTico, WhiteIn);
    NEW_NERVE(RunawayTicoNrvWhiteOut, RunawayTico, WhiteOut);
    NEW_NERVE(RunawayTicoNrvWait, RunawayTico, Wait);
    NEW_NERVE(RunawayTicoNrvAppear, RunawayTico, Appear);
    NEW_NERVE(RunawayTicoNrvTalk, RunawayTico, Talk);
};  // namespace NrvRunawayTico

RunawayTico::RunawayTico(const char* pName) : Tico(pName), mCameraInfo(), mRole(), mDemoCastID(), mIsStartRunaway(), mIsAllCaught() {
    lumacore::setClass(this, lumacore::kClassId< RunawayTico >);
}

void RunawayTico::init(const TableIter& rIter) {
    s32 colorID = 0;
    MR::getPlacementArg0NoInit(rIter, &colorID);
    s32 role = 0;
    MR::getPlacementArg1NoInit(rIter, &role);
    mRole = role;

    if (MR::tryRegisterDemoCast(this, rIter)) {
        mDemoCastID = MR::getDemoCastID(rIter);

        if (mDemoCastID > 0) {
            colorID = 0;
            mRole = Role_AllCaught;
        } else {
            colorID = -1;
            mRole = Role_Meet;
        }
    }

    initBase(rIter, colorID);
    initMessage(rIter, "RunawayTico");

    if (mRole == Role_Meet) {
        mCameraInfo = MR::createActorCameraInfo(rIter);
        MR::initAnimCamera(this, mCameraInfo, "DemoMeetTico");

        MR::registerDemoActionFunctor(this, MR::Functor(this, &RunawayTico::setDemoTrans), "チコとの出会い[開始]");
        MR::registerDemoActionNerve(this, GET_NERVE(RunawayTico, RunawayTicoNrvGuide1), "チコとの出会い[チコ変身]");
        MR::registerDemoActionFunctor(this, MR::Functor(this, &RunawayTico::startRunaway), "ウサギ追いかけ[開始]");
        setNerve(GET_NERVE(RunawayTico, RunawayTicoNrvGuide0));
    } else if (mRole == Role_AllCaught) {
        MR::registerDemoActionFunctor(this, MR::Functor(this, &RunawayTico::setPosAllCaught), "高楼出現[フェードイン]");
        makeActorDead();
    }

    MR::offRootNodeAutomatic(mMsgCtrl);
    MR::useStageSwitchWriteA(this, rIter);
    MR::invalidateClipping(this);
    MR::onCalcGravity(this);
}

void RunawayTico::initAfterPlacement() {
    if (mRole == Role_Meet && MR::isOnGameEventFlagEndTicoGuideDemo()) {
        makeActorDead();
    }
}

void RunawayTico::appearBushComment(const Vec3f& rPos) {
    MR::forwardNodeNextBranchLeft(mMsgCtrl);
    appear();
    setPosAfterCaught(rPos);
    MR::requestStartDemoMarioPuppetable(this, "ぼやき", GET_NERVE(RunawayTico, RunawayTicoNrvAppear), GET_NERVE(RunawayTico, RunawayTicoNrvWait));
}

void RunawayTico::appearHoleComment(const Vec3f& rPos) {
    MR::forwardNodeNextBranchRight(mMsgCtrl);
    MR::forwardNodeCurrentBranchLeft(mMsgCtrl);
    appear();
    setPosAfterCaught(rPos);
    MR::requestStartDemoMarioPuppetable(this, "ぼやき", GET_NERVE(RunawayTico, RunawayTicoNrvAppear), GET_NERVE(RunawayTico, RunawayTicoNrvWait));
}

void RunawayTico::appearPipeComment(const Vec3f& rPos) {
    MR::forwardNodeNextBranchRight(mMsgCtrl);
    MR::forwardNodeCurrentBranchRight(mMsgCtrl);
    appear();
    setPosAfterCaught(rPos);
    MR::requestStartDemoMarioPuppetable(this, "ぼやき", GET_NERVE(RunawayTico, RunawayTicoNrvAppear), GET_NERVE(RunawayTico, RunawayTicoNrvWait));
}

void RunawayTico::appearMamaComment(const Vec3f& rPos) {
    mIsAllCaught = true;

    MR::forwardNode(mMsgCtrl);
    appear();
    setPosAfterCaught(rPos);
    MR::requestStartDemoMarioPuppetable(this, "ぼやき", GET_NERVE(RunawayTico, RunawayTicoNrvAppear), GET_NERVE(RunawayTico, RunawayTicoNrvWait));
}

void RunawayTico::setPosAfterCaught(const Vec3f& rPos) {
    Vec3f front;
    MR::getPlayerFrontVec(&front);

    Pos3f mtx;
    MR::makeMtxUpFrontPos(&mtx, -mGravity, -front, rPos);
    setBaseMtx(mtx);
    setInitPose();
}

void RunawayTico::setPosAllCaught() {
    MR::forwardNode(mMsgCtrl);
    MR::onRootNodeAutomatic(mMsgCtrl);

    MR::setNPCActorPos(this, MR::FormattedName("TicoDemoPos%d", mDemoCastID + 1).c_str());
    setInitPose();

    if (MR::isDead(this)) {
        makeActorAppeared();
    }

    setNerveWait();
}

bool RunawayTico::isStartRunaway() const {
    return mIsStartRunaway;
}

void RunawayTico::startRunaway() {
    mIsStartRunaway = true;

    if (MR::isValidSwitchA(this)) {
        MR::onSwitchA(this);
    }
}

void RunawayTico::setDemoTrans() {
    MR::startAction(this, "DemoMeetTico");
    setBaseMtx(MR::getPlayerBaseMtx());
}

void RunawayTico::exeGuide0() {
    if (MR::isFirstStep(this)) {
        MR::startTimeKeepDemoMarioPuppetable(this, "チコガイドデモ", nullptr);
        MR::onGameEventFlagEndTicoGuideDemo();
        MR::endStartPosCamera();
        MR::startAnimCameraTargetPlayer(this, mCameraInfo, "DemoMeetTico", 0, 1.0f);
        MR::forceToFrameCinemaFrame();
    }

    if (MR::isGreaterEqualStep(this, ::sWaitSeStartFrame)) {
        MR::startLevelSound(this, "SE_SM_LV_TICO_WAIT");
    }

    if (MR::isGreaterEqualStep(this, ::sFloatSeStartFrame)) {
        MR::startLevelSound(this, "SE_SM_LV_TICO_FLOAT_DEMO", sFloatSeMinVolume * cNum_exeGuide0_f1);
    }

    if (MR::isStep(this, ::sDemoCameraZoomOutBgmStartFrame)) {
        MR::startSubBGM("BGM_MEET_TICO_ZOOM_OUT", false);
    }

    if (MR::isGreaterEqualStep(this, ::sDemoCameraZoomOutStartFrame) && MR::isLessStep(this, ::sDemoCameraZoomOutEndFrame)) {
        MR::startSystemLevelSE("SE_DM_LV_MEET_TICO_ZOOM_OUT");
    }
}

void RunawayTico::exeGuide1() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Metamorphosis");

        Pos3f mtx;
        mtx.set(MR::getJointMtx(this, "Body"));
        MR::addTransMtxLocalY(mtx.toMtxPtr(), ::sAwayYOffset);
        setBaseMtx(mtx);

        mLodCtrl->invalidate();
    }

    if (MR::isBckStopped(this)) {
        MR::hideModel(this);
    }

    if (MR::isDemoPartLastStep("チコとの出会い[ウサギ逃走]")) {
        MR::startStartPosCamera(true);
        MR::endAnimCamera(this, mCameraInfo, "DemoMeetTico", -1, true);
        MR::startStageBGM("MBGM_GALAXY_24", false);
        MR::showModel(this);
        mLodCtrl->validate();
        kill();
    }
}

void RunawayTico::exeWhiteOut() {
    if (MR::isFirstStep(this)) {
        MR::closeWipeWhiteFade(::sDemoFadeOut);
        MR::stopStageBGM(135);
    }

    MR::limitedSound("SE_SM_LV_TICO_WAIT", 1);

    if (MR::isLessEqualStep(this, ::sDemoFadeWait)) {
        MR::startLevelSound(this, "SE_SM_LV_TICO_OP_WAIT");
    }

    if (MR::isWipeActive()) {
        return;
    }

    setNerve(GET_NERVE(RunawayTico, RunawayTicoNrvWhiteIn));
}

void RunawayTico::exeWhiteIn() {
    if (MR::isLessStep(this, ::sDemoFadeIn)) {
        MR::limitedSound("SE_SM_LV_TICO_WAIT", 1);
    }

    if (MR::isStep(this, ::sDemoFadeIn)) {
        MR::openWipeWhiteFade(::sDemoFadeOut);
        MR::endDemo(this, "ぼやき");
        MR::startTimeKeepDemoMarioPuppetable(this, "チコガイドデモ", "高楼出現[デモ]");
        kill();
    }
}

void RunawayTico::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Wait");
    }
}

void RunawayTico::exeAppear() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayer("WatchupMore");
        MR::startAction(this, "Appear");
        MR::startSound(this, "SE_SM_RUNAWAY_RABBIT_APPEAR");
    }

    if (MR::isStep(this, ::sAppearSeStep)) {
        MR::startSound(this, "SE_SM_METAMORPHOSE_SMOKE");
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(RunawayTico, RunawayTicoNrvTalk));
    }
}

void RunawayTico::exeTalk() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Talk");
    }

    turnToPlayer();

    if (MR::tryTalkForceWithoutDemoAtEnd(mMsgCtrl)) {
        if (mIsAllCaught) {
            MR::startNPCTalkCamera(getMsgCtrl(), getBaseMtx(), 1.0f, 0);
            setNerve(GET_NERVE(RunawayTico, RunawayTicoNrvWhiteOut));
        } else {
            MR::endDemo(this, "ぼやき");
            setNerveWait();
        }
    }
}
