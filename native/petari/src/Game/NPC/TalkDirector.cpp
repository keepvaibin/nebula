#include "Game/NPC/TalkDirector.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/NPC/TalkBalloon.hpp"
#include "Game/NPC/TalkMessageCtrl.hpp"
#include "Game/NPC/TalkState.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Screen/GameSceneLayoutHolder.hpp"
#include "Game/Screen/YesNoLayout.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/TalkUtil.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    TalkDirector* getTalkDirector() {
        return MR::getSceneObj< TalkDirector >(SceneObj_TalkDirector);
    }

    NEBULA_TUNABLE(f32, sTalkDistanceScale, 1.0f);
    // Range for the host of the long talk already running.
    NEBULA_TUNABLE(f32, sNearScale, 1.2f);
    const char* const cTalkDemoName = "会話";

    // TalkDirector::mDemoType.
    enum TalkDemoType {
        TalkDemo_None,
        // The talk demo with Mario puppetable.
        TalkDemo_Talk,
        // A demo-wanting talk inside a time-keep demo pauses it.
        TalkDemo_PauseTimeKeep,
        // A talk without a demo inside a time-keep demo.
        TalkDemo_InTimeKeep,
    };

    // Flow-data branch argument. The three live ones are asked when read and have no stored result.
    enum BranchResultIndex {
        BranchResult_KinopioExplorerRescued,
        BranchResult_KinopioExplorerOrganize,
        BranchResult_KinopioExplorerTalkGetGrandStar2,
        BranchResult_KinopioExplorerTrickComet,
        BranchResult_KinopioExplorerTalkGetGrandStar3,
        BranchResult_KinopioExplorerStartMessenger,
        BranchResult_KinopioExplorerTalkGetGrandStar4,
        BranchResult_KinopioExplorerTalkGetGrandStar5,
        BranchResult_KinopioExplorerTalkGetGrandStar6,
        BranchResult_KinopioExplorerTalkGoFinalBattle,
        BranchResult_EndLuigiHideAndSeek,
        BranchResult_KinopioExplorerCompleteTrickComet,
        BranchResult_LuigiDisappearFromAstroGalaxy,  // live
        BranchResult_OnLuigiHiding,                  // live
        BranchResult_GreenStarNum1,
        BranchResult_GreenStarNum2,
        BranchResult_GreenStarNum3,
        BranchResult_RedDriver,
        BranchResult_AnyPlayerLeftSupply,  // live
        BranchResult_ActiveLuigiHideAndSeek,
        BranchResult_GalaxyPurpleCometLaunch,
        BranchResult_RosettaTalkTorchLecture,
        BranchResult_RosettaTalkTrickComet,
        BranchResult_RosettaTalkKoopa,
        BranchResult_RosettaTalkCountDownStart,
        BranchResult_RosettaTalkAstroDomeRecover,
        BranchResult_RosettaTalkTorchProgress,
        BranchResult_ViewNormalEnding,
        BranchResult_Num,
    };

    static_assert(BranchResult_Num == TalkDirector::cBranchResultNum, "branch result table");
}  // namespace

namespace NrvTalkDirector {
    NEW_NERVE(TalkDirectorNrvWait, TalkDirector, Wait);
    NEW_NERVE(TalkDirectorNrvPrep, TalkDirector, Prep);
    NEW_NERVE(TalkDirectorNrvTalk, TalkDirector, Talk);
    NEW_NERVE(TalkDirectorNrvSlct, TalkDirector, Slct);
    NEW_NERVE(TalkDirectorNrvNext, TalkDirector, Next);
    NEW_NERVE(TalkDirectorNrvTerm, TalkDirector, Term);
};  // namespace NrvTalkDirector

TalkPeekZ::TalkPeekZ() : mAsker(Frame::newProbeAsker()) {
}

void TalkPeekZ::setDrawSyncToken() {
    Frame::getProjectionv(mProjection);
    Frame::getViewportv(mViewport);

    // The depth at the balloon's point asked last; then the picture drawn so far, at its point now.
    NebulaScreenProbe probe;
    mDepth = 0.0f;
    if (Frame::screenProbeResults(cReadbackChannel, mAsker, &probe, 1) == 1 && probe.x >= 0.0f) {
        mDepth = probe.value;
        TDDraw::invProject(&mWorldPos, Vec3f(probe.user_x, probe.user_y, mDepth), const_cast< MtxPtr >(MR::getCameraViewMtx().mMtx), mProjection,
                           mViewport, true);
    }

    probe.x = -1.0f;
    probe.y = -1.0f;
    probe.user_x = mScreenPos.x;
    probe.user_y = mScreenPos.y;
    if (MR::isInRange(mScreenPos.x, 0.0f, MR::getScreenWidth() - 1) &&
        MR::isInRange(mScreenPos.y, 0.0f, NebulaFrameClock::cScreenLines - 1)) {
        Vec2f pos;
        MR::convertScreenPosToFrame(&pos, mScreenPos);
        probe.x = pos.x;
        probe.y = pos.y;
    }
    Frame::probeScreen(cReadbackChannel, mAsker, &probe, 1);
}

TalkDirector::TalkDirector(const char* pName)
    : LayoutActor(pName, true), mMsgControls(), mPeekZ(), mBalloonHolder(), mStateHolder(), mMsgCtrl(), mBalloonCtrl(), mRequestCtrl(), mLastTalkCtrl(),
      mTalkState(), mIsTalkStart(), mIsTalkEnd(), mIsInvalidTalk(), mIsInvalidClipping(), mIsNext(), mDemoType(TalkDemo_None), mIsSkipTerm(),
      mIsPlayerTalk(), mMessageInfo(), mCameraInfo(), mHostActor(), mBranchResult() {
    lumacore::setClass(this, lumacore::kClassId< TalkDirector >);
}

void TalkDirector::init(const TableIter& /*rIter*/) {
    MR::connectToScene(this, MR::MovementType_TalkDirector, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_None);
    mBalloonHolder = new TalkBalloonHolder();
    mStateHolder = new TalkStateHolder();
    mMsgControls.init(128);
    mPeekZ = new TalkPeekZ();
    initNerve(GET_NERVE(TalkDirector, TalkDirectorNrvWait));
    initBranchResult();
    appear();
}

void TalkDirector::movement() {
    updateMessage();
    LayoutActor::movement();
}

bool TalkDirector::request(TalkMessageCtrl* pCtrl, bool isForce) {
    mIsTalkEnd = false;
    mIsTalkStart = false;

    if (isInvalidTalk()) {
        return false;
    }

    // An automatic root node goes to its first message unless this director is already past it.
    if (pCtrl->mIsOnRootNodeAuto && !isNerve(GET_NERVE(TalkDirector, TalkDirectorNrvNext)) &&
        (TalkFunction::isShortTalk(pCtrl) || !isNerve(GET_NERVE(TalkDirector, TalkDirectorNrvTalk)))) {
        pCtrl->rootNodePre(false);
    }

    if (!isForce && !TalkFunction::isShortTalk(pCtrl) && MR::isPlayerElementModeHopper()) {
        return false;
    }

    // While a long talk runs only its own host may ask.
    bool isTalkingHost = false;

    if (isNerve(GET_NERVE(TalkDirector, TalkDirectorNrvTalk)) && !TalkFunction::isShortTalk(mTalkState->mMsgCtrl)) {
        if (mTalkState->mMsgCtrl != pCtrl) {
            return false;
        }

        isTalkingHost = true;
    }

    if (MR::isTalkNone(pCtrl)) {
        TalkFunction::onTalkStateEntry(pCtrl);
    }

    if (isForce) {
        if (TalkFunction::isShortTalk(pCtrl)) {
            if (pCtrl->isNearPlayer(mRequestCtrl)) {
                mRequestCtrl = pCtrl;
            }
        } else {
            mRequestCtrl = pCtrl;
            mBalloonCtrl = pCtrl;
            mMsgCtrl = pCtrl;
        }
    } else {
        f32 scale = isTalkingHost ? ::sNearScale : ::sTalkDistanceScale;

        if (!pCtrl->isNearPlayer(scale * pCtrl->mTalkDistance)) {
            return false;
        }

        if (pCtrl->isNearPlayer(mRequestCtrl)) {
            mRequestCtrl = pCtrl;
        }
    }

    if (mMsgCtrl != pCtrl) {
        return false;
    }

    if (!isNerve(GET_NERVE(TalkDirector, TalkDirectorNrvWait))) {
        return mTalkState->mMsgCtrl == pCtrl;
    }

    mTalkState = initState(pCtrl);
    setNerve(GET_NERVE(TalkDirector, TalkDirectorNrvPrep));
    return true;
}

bool TalkDirector::test(TalkMessageCtrl* pCtrl, bool isForce, bool isDemo) {
    if (isInvalidTalk() || mMsgCtrl != pCtrl) {
        return false;
    }

    if (isNerve(GET_NERVE(TalkDirector, TalkDirectorNrvTalk))) {
        return true;
    }

    if (!isNerve(GET_NERVE(TalkDirector, TalkDirectorNrvPrep))) {
        return false;
    }

    if (!isForce && !mTalkState->test()) {
        return false;
    }

    return getDemoType(pCtrl, isDemo) != TalkDemo_Talk || MR::canStartDemo();
}

bool TalkDirector::start(TalkMessageCtrl* pCtrl, bool isForce, bool isDemo, bool isPlayerTalk) {
    if (!test(pCtrl, isForce, isDemo)) {
        return false;
    }

    if (isNerve(GET_NERVE(TalkDirector, TalkDirectorNrvTalk))) {
        return true;
    }

    pCtrl->rootNodePre(true);
    prepTalk(pCtrl, isForce, isDemo, isPlayerTalk);

    if (TalkFunction::isEventNode(pCtrl)) {
        setNerve(GET_NERVE(TalkDirector, TalkDirectorNrvNext));
    } else {
        mTalkState->open();
        setNerve(GET_NERVE(TalkDirector, TalkDirectorNrvTalk));
    }

    return true;
}

void TalkDirector::updateMessage() {
    mBalloonHolder->update();
    mStateHolder->update();

    for (TalkMessageCtrl** pIter = mMsgControls.begin(); pIter != mMsgControls.end(); pIter++) {
        if (MR::isTalkEntry(*pIter)) {
            TalkFunction::onTalkStateNone(*pIter);
        }
    }

    // Before a talk starts, the nearest request reaches the message a frame after the balloon.
    if (isNerve(GET_NERVE(TalkDirector, TalkDirectorNrvWait)) || isNerve(GET_NERVE(TalkDirector, TalkDirectorNrvPrep))) {
        mMsgCtrl = mBalloonCtrl;
        mBalloonCtrl = mRequestCtrl;
    } else {
        mMsgCtrl = mRequestCtrl;
        mBalloonCtrl = mRequestCtrl;
    }

    mRequestCtrl = nullptr;

    if (mMsgCtrl != nullptr) {
        mLastTalkCtrl = mMsgCtrl;
    }

    if (mBalloonCtrl != nullptr) {
        mBalloonCtrl->updateBalloonPos();
        mPeekZ->mScreenPos.set(mBalloonCtrl->mBalloonPos.x, mBalloonCtrl->mBalloonPos.y);
    }

    if (MR::isPowerStarGetDemoActive()) {
        mMsgCtrl = nullptr;
        mIsInvalidTalk = true;
    }
}

void TalkDirector::prepTalk(TalkMessageCtrl* pCtrl, bool isSkipTerm, bool isDemo, bool isPlayerTalk) {
    mIsSkipTerm = isSkipTerm;
    mIsTalkStart = true;

    mIsInvalidClipping = MR::isInvalidClipping(pCtrl->mHostActor);
    mDemoType = getDemoType(pCtrl, isDemo);
    mIsPlayerTalk = isPlayerTalk && mDemoType != TalkDemo_None;

    MR::invalidateClipping(pCtrl->mHostActor);

    switch (mDemoType) {
    case TalkDemo_PauseTimeKeep:
        MR::pauseTimeKeepDemo(pCtrl->mHostActor);
        break;
    case TalkDemo_Talk:
        MR::tryStartDemoMarioPuppetable(pCtrl->mHostActor, ::cTalkDemoName);
        pauseOff();
        break;
    }

    if (mIsPlayerTalk) {
        MR::startPlayerTalk(pCtrl->mHostActor);
    }
}

void TalkDirector::termTalk() {
    TalkMessageCtrl* pCtrl = mTalkState->mMsgCtrl;
    mIsTalkEnd = true;

    if (mMessageInfo.isCameraNormal()) {
        MR::endNPCTalkCamera(false, -1);
    } else if (mMessageInfo.isCameraEvent()) {
        MR::endMultiActorCamera(mHostActor, mCameraInfo, ::cTalkDemoName, false);
    }

    if (!mIsInvalidClipping) {
        MR::validateClipping(pCtrl->mHostActor);
    }

    if (mIsPlayerTalk) {
        MR::endPlayerTalk();
    }

    switch (mDemoType) {
    case TalkDemo_PauseTimeKeep:
        MR::resumeTimeKeepDemo(pCtrl->mHostActor);
        break;
    case TalkDemo_Talk:
        MR::endDemo(pCtrl->mHostActor, ::cTalkDemoName);
        break;
    }

    mDemoType = TalkDemo_None;
    mIsPlayerTalk = false;
    mIsInvalidClipping = false;
}

TalkState* TalkDirector::initState(TalkMessageCtrl* pCtrl) {
    TalkState* pState = mStateHolder->getState(pCtrl);
    pState->init(pCtrl, mBalloonHolder->getBalloon(pCtrl));

    const TalkMessageInfo* pInfo = TalkFunction::getMessageInfo(pCtrl);

    if (pInfo->isCameraNormal() || pInfo->isCameraEvent()) {
        mMessageInfo = *pInfo;
        mCameraInfo = pCtrl->mCameraInfo;
        mHostActor = pCtrl->mHostActor;
    }

    return pState;
}

bool TalkDirector::isInvalidTalk() const {
    return mIsInvalidTalk || MR::isStarPointerModeBlueStarReady() || MR::isFirstPersonCamera() || MR::isPlayerDead();
}

void TalkDirector::appearYesNoSelector(const TalkMessageCtrl* pCtrl) const {
    MR::FormattedName yesName;
    MR::FormattedName noName;

    if (const s32 ownNode = pCtrl->getOwnSelectNode(); ownNode >= 0) {
        yesName.format("Select_Nebula%d_Yes", ownNode);
        noName.format("Select_Nebula%d_No", ownNode);
    } else {
        // A select the game does not know names no lines: no choice words, not a null name.
        const char* pBranchID = pCtrl->getBranchID();
        yesName.format("Select_%s_Yes", pBranchID != nullptr ? pBranchID : "");
        noName.format("Select_%s_No", pBranchID != nullptr ? pBranchID : "");
    }

    if (pCtrl->isSelectYesNo()) {
        MR::resetYesNoSelectorSE();
    } else {
        MR::setYesNoSelectorSE("SE_SY_TALK_FOCUS_ITEM", "SE_SY_TALK_SELECT_YES", "SE_SY_TALK_SELECT_YES");
    }

    MR::requestMovementOn(MR::getGameSceneLayoutHolder()->mYesNoLayout);
    MR::appearYesNoSelector(yesName.c_str(), noName.c_str(), nullptr);
}

s32 TalkDirector::getDemoType(const TalkMessageCtrl* pCtrl, bool isDemo) const {
    if (TalkFunction::isShortTalk(pCtrl)) {
        return TalkDemo_None;
    }

    if (MR::isTimeKeepDemoActive()) {
        return isDemo ? TalkDemo_PauseTimeKeep : TalkDemo_InTimeKeep;
    }

    return isDemo ? TalkDemo_Talk : TalkDemo_None;
}

bool TalkDirector::getBranchResult(u16 index) const {
    switch (index) {
    case BranchResult_AnyPlayerLeftSupply:
        return MR::isAnyPlayerLeftSupply();
    case BranchResult_LuigiDisappearFromAstroGalaxy:
        return MR::isLuigiDisappearFromAstroGalaxy();
    case BranchResult_OnLuigiHiding:
        return MR::isOnLuigiHiding();
    }

    return index < BranchResult_Num && mBranchResult[index];
}

void TalkDirector::initBranchResult() {
    mBranchResult[BranchResult_KinopioExplorerRescued] = MR::isKinopioExplorerRescued();
    mBranchResult[BranchResult_KinopioExplorerOrganize] = MR::isKinopioExplorerOrganize();
    mBranchResult[BranchResult_KinopioExplorerTalkGetGrandStar2] = MR::isKinopioExplorerTalkGetGrandStar2();
    mBranchResult[BranchResult_KinopioExplorerTrickComet] = MR::isKinopioExplorerTrickComet();
    mBranchResult[BranchResult_KinopioExplorerTalkGetGrandStar3] = MR::isKinopioExplorerTalkGetGrandStar3();
    mBranchResult[BranchResult_KinopioExplorerStartMessenger] = MR::isKinopioExplorerStartMessenger();
    mBranchResult[BranchResult_KinopioExplorerTalkGetGrandStar4] = MR::isKinopioExplorerTalkGetGrandStar4();
    mBranchResult[BranchResult_KinopioExplorerTalkGetGrandStar5] = MR::isKinopioExplorerTalkGetGrandStar5();
    mBranchResult[BranchResult_KinopioExplorerTalkGetGrandStar6] = MR::isKinopioExplorerTalkGetGrandStar6();
    mBranchResult[BranchResult_KinopioExplorerTalkGoFinalBattle] = MR::isKinopioExplorerTalkGoFinalBattle();
    mBranchResult[BranchResult_EndLuigiHideAndSeek] = MR::isEndLuigiHideAndSeekEvent();
    mBranchResult[BranchResult_KinopioExplorerCompleteTrickComet] = MR::isKinopioExplorerCompleteTrickComet();

    s32 greenStarNum = MR::calcCurrentGreenStarNum();
    mBranchResult[BranchResult_GreenStarNum1] = greenStarNum == 1;
    mBranchResult[BranchResult_GreenStarNum2] = greenStarNum == 2;
    mBranchResult[BranchResult_GreenStarNum3] = greenStarNum == 3;

    mBranchResult[BranchResult_RedDriver] = MR::isOnGameEventFlagRedDriver();
    mBranchResult[BranchResult_ActiveLuigiHideAndSeek] = MR::isActiveLuigiHideAndSeekEvent();
    mBranchResult[BranchResult_GalaxyPurpleCometLaunch] = MR::isGalaxyPurpleCometLaunch();
    mBranchResult[BranchResult_RosettaTalkTorchLecture] = MR::isRosettaTalkTorchLecture();
    mBranchResult[BranchResult_RosettaTalkTrickComet] = MR::isRosettaTalkTrickComet();
    mBranchResult[BranchResult_RosettaTalkKoopa] = MR::isRosettaTalkKoopa();
    mBranchResult[BranchResult_RosettaTalkCountDownStart] = MR::isRosettaTalkCountDownStart();
    mBranchResult[BranchResult_RosettaTalkAstroDomeRecover] = MR::isRosettaTalkAstroDomeRecover();
    mBranchResult[BranchResult_RosettaTalkTorchProgress] = MR::isRosettaTalkTorchProgress();
    mBranchResult[BranchResult_ViewNormalEnding] = MR::isOnGameEventFlagViewNormalEnding();
}

void TalkDirector::pauseOff() {
    MR::requestMovementOn(this);
    mBalloonHolder->pauseOff();
    mStateHolder->pauseOff();
}

void TalkDirector::balloonOff() {
    if (mTalkState != nullptr) {
        mTalkState->balloonOff();
        mBalloonHolder->balloonOff();
    }
}

bool TalkDirector::isSystemTalking() const {
    if (isNerve(GET_NERVE(TalkDirector, TalkDirectorNrvTalk)) || isNerve(GET_NERVE(TalkDirector, TalkDirectorNrvSlct)) ||
        isNerve(GET_NERVE(TalkDirector, TalkDirectorNrvNext))) {
        return !TalkFunction::isShortTalk(mTalkState->mMsgCtrl);
    }

    return false;
}

bool TalkDirector::isNormalTalking() const {
    return isSystemTalking() && mDemoType == TalkDemo_Talk;
}

LiveActor* TalkDirector::getTalkingActor() const {
    return isSystemTalking() ? mTalkState->mMsgCtrl->mHostActor : nullptr;
}

void TalkDirector::exePrep() {
    if (mTalkState->prep(mMsgCtrl)) {
        TalkFunction::onTalkStateEnableStart(mTalkState->mMsgCtrl);
        return;
    }

    TalkFunction::onTalkStateNone(mTalkState->mMsgCtrl);
    mTalkState = nullptr;
    setNerve(GET_NERVE(TalkDirector, TalkDirectorNrvWait));
}

void TalkDirector::exeWait() {
}

void TalkDirector::exeTalk() {
    TalkMessageCtrl* pCtrl = mTalkState->mMsgCtrl;
    // Each line enters this nerve: its first step is the line opening.
    if (MR::isFirstStep(this) && NebulaMods::sWanted[NEBULA_MOD_EVENT_TALK_LINE]) {
        NebulaMods::dispatch(NEBULA_MOD_EVENT_TALK_LINE, 0, pCtrl->mHostActor, TalkFunction::getMessageName(pCtrl));
    }
    TalkFunction::onTalkStateTalking(pCtrl);

    // Without a demo, a talk whose host is no longer the nearest request moves on unread.
    bool isLost = mDemoType == TalkDemo_None && mTalkState->isLostMessage(mMsgCtrl);

    if (!isLost) {
        if (!mTalkState->talk(mMsgCtrl)) {
            return;
        }

        MR::readMessage(pCtrl);
    }

    if (TalkFunction::isSelectTalk(pCtrl)) {
        setNerve(GET_NERVE(TalkDirector, TalkDirectorNrvSlct));
        return;
    }

    mTalkState->clos();
    mIsNext = false;

    if (pCtrl->mIsOnRootNodeAuto && !mTalkState->isSelfInterrupt(mLastTalkCtrl)) {
        pCtrl->rootNodePst();
    }

    setNerve(GET_NERVE(TalkDirector, TalkDirectorNrvNext));
    exeNext();
}

void TalkDirector::exeSlct() {
    TalkMessageCtrl* pCtrl = mTalkState->mMsgCtrl;

    if (MR::isFirstStep(this)) {
        appearYesNoSelector(pCtrl);
        MR::startSystemSE("SE_SM_TALKBLN_OPEN");
    }

    TalkFunction::onTalkStateTalking(pCtrl);

    if (!MR::isYesNoSelected()) {
        return;
    }

    if (NebulaMods::sWanted[NEBULA_MOD_EVENT_TALK_CHOICE]) {
        NebulaMods::dispatch(NEBULA_MOD_EVENT_TALK_CHOICE, MR::isYesNoSelectedYes() ? 1 : 0, pCtrl->mHostActor,
                             TalkFunction::getMessageName(pCtrl));
    }

    MR::resetYesNoSelectorSE();
    mTalkState->clos();
    mIsNext = true;

    if (pCtrl->mIsOnRootNodeAuto) {
        pCtrl->rootNodeSel(MR::isYesNoSelectedYes());
        pCtrl->rootNodePre(true);
    }

    setNerve(GET_NERVE(TalkDirector, TalkDirectorNrvNext));
    exeNext();
}

void TalkDirector::exeNext() {
    TalkMessageCtrl* pCtrl = mTalkState->mMsgCtrl;

    // Read before rootNodeEve moves the node.
    bool isContinue = pCtrl->isCurrentNodeContinue();

    if (!pCtrl->rootNodeEve()) {
        return;
    }

    mIsNext |= isContinue;

    if (!mIsNext) {
        termTalk();
        TalkFunction::onTalkStateEnableEnd(pCtrl);
        setNerve(GET_NERVE(TalkDirector, TalkDirectorNrvTerm));
        return;
    }

    if (TalkFunction::isShortTalk(pCtrl)) {
        termTalk();
    }

    pCtrl->rootNodePre(true);
    mTalkState = initState(pCtrl);
    mTalkState->open();
    setNerve(GET_NERVE(TalkDirector, TalkDirectorNrvTalk));
}

void TalkDirector::exeTerm() {
    if (!mIsSkipTerm && !mTalkState->term(mMsgCtrl)) {
        return;
    }

    TalkFunction::onTalkStateNone(mTalkState->mMsgCtrl);
    mTalkState = nullptr;

    setNerve(GET_NERVE(TalkDirector, TalkDirectorNrvWait));
}

bool TalkFunction::requestTalkSystem(TalkMessageCtrl* pCtrl, bool isForce) {
    return ::getTalkDirector()->request(pCtrl, isForce);
}

bool TalkFunction::startTalkSystem(TalkMessageCtrl* pCtrl, bool isForce, bool isDemo, bool isPlayerTalk) {
    return ::getTalkDirector()->start(pCtrl, isForce, isDemo, isPlayerTalk);
}

bool TalkFunction::endTalkSystem(TalkMessageCtrl* pCtrl) {
    return MR::isTalkEnableEnd(pCtrl);
}

bool TalkFunction::isTalkSystemStart(const TalkMessageCtrl* pCtrl) {
    const TalkDirector* pDirector = ::getTalkDirector();
    return pDirector->mMsgCtrl == pCtrl && pDirector->mIsTalkStart;
}

bool TalkFunction::isTalkSystemEnd(const TalkMessageCtrl* pCtrl) {
    const TalkDirector* pDirector = ::getTalkDirector();
    return pDirector->mMsgCtrl == pCtrl && pDirector->mIsTalkEnd;
}

bool TalkFunction::getBranchAstroGalaxyResult(u16 index) {
    return ::getTalkDirector()->getBranchResult(index);
}

void TalkFunction::registerTalkSystem(TalkMessageCtrl* pCtrl) {
    ::getTalkDirector()->mMsgControls.push_back(pCtrl);
}

void MR::pauseOffTalkDirector() {
    ::getTalkDirector()->pauseOff();
}

void MR::balloonOffTalkDirector() {
    ::getTalkDirector()->balloonOff();
}

void MR::invalidateTalkDirector() {
    ::getTalkDirector()->mIsInvalidTalk = true;
}

void MR::setTalkDirectorDrawSyncToken() {
    TalkDirector* pDirector = ::getTalkDirector();

    if (pDirector != nullptr) {
        pDirector->mPeekZ->setDrawSyncToken();
    }
}

bool MR::isActiveTalkBalloonShort() {
    const TalkDirector* pDirector = ::getTalkDirector();
    return pDirector != nullptr && pDirector->mBalloonHolder->isActiveBalloonShort();
}
