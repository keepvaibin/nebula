#include "Game/NPC/TalkDirector.hpp"
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
#include <cstdio>
#include "Game/Util/StringUtil.hpp"

namespace {
    TalkDirector* getTalkDirector() {
        return MR::getSceneObj< TalkDirector >(SceneObj_TalkDirector);
    }

    static const f32 sTalkDistanceScale = 1.0f;
    static const f32 sNearScale = 1.2f;
};  // namespace

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
    : LayoutActor(pName, true), mMsgCtrl(), mBalloonCtrl(), mRequestCtrl(), mLastTalkCtrl(), mTalkState(), mIsTalkStart(), mIsTalkEnd(), mIsInvalidTalk(), mIsInvalidClipping(), mDemoType(), mIsSkipTerm(), mIsPlayerTalk() {
}

TalkDirector::~TalkDirector() {
}

void TalkDirector::init(const TableIter& rIter) {
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

bool TalkDirector::request(TalkMessageCtrl* pCtrl, bool force) {
    mIsTalkEnd = false;
    bool var31 = false;
    mIsTalkStart = false;
    if (isInvalidTalk()) {
        return false;
    }

    if (pCtrl->mIsOnRootNodeAuto) {
        if (TalkFunction::isShortTalk(pCtrl)) {
            if (!isNerve(GET_NERVE(TalkDirector, TalkDirectorNrvNext))) {
                pCtrl->rootNodePre(false);
            }
        } else if (!isNerve(GET_NERVE(TalkDirector, TalkDirectorNrvTalk)) && !isNerve(GET_NERVE(TalkDirector, TalkDirectorNrvNext))) {
            pCtrl->rootNodePre(false);
        }
    }

    if (!force && !TalkFunction::isShortTalk(pCtrl) && MR::isPlayerElementModeHopper()) {
        return false;
    }

    if (isNerve(GET_NERVE(TalkDirector, TalkDirectorNrvTalk)) && !TalkFunction::isShortTalk(mTalkState->mMsgCtrl)) {
        if (mTalkState->mMsgCtrl == pCtrl) {
            var31 = true;
        } else {
            return false;
        }
    }

    if (MR::isTalkNone(pCtrl)) {
        TalkFunction::onTalkStateEntry(pCtrl);
    }

    if (force) {
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
        f32 scale = var31 ? ::sNearScale : ::sTalkDistanceScale;
        f32 talkDistance = pCtrl->mTalkDistance;

        if (!pCtrl->isNearPlayer(scale * talkDistance)) {
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

bool TalkDirector::test(TalkMessageCtrl* pCtrl, bool arg2, bool arg3) {
    if (isInvalidTalk()) {
        return false;
    }

    if (mMsgCtrl != pCtrl) {
        return false;
    }

    if (isNerve(GET_NERVE(TalkDirector, TalkDirectorNrvTalk))) {
        return true;
    }

    if (!isNerve(GET_NERVE(TalkDirector, TalkDirectorNrvPrep))) {
        return false;
    }

    if (!arg2 && !mTalkState->test()) {
        return false;
    }

    if (getDemoType(pCtrl, arg3) == 1 && !MR::canStartDemo()) {
        return false;
    }

    return true;
}

bool TalkDirector::start(TalkMessageCtrl* pCtrl, bool arg2, bool arg3, bool arg4) {
    if (test(pCtrl, arg2, arg3)) {
        if (isNerve(GET_NERVE(TalkDirector, TalkDirectorNrvTalk))) {
            return true;
        }
    } else {
        return false;
    }

    pCtrl->rootNodePre(true);
    prepTalk(pCtrl, arg2, arg3, arg4);

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

    if (isNerve(GET_NERVE(TalkDirector, TalkDirectorNrvWait)) || isNerve(GET_NERVE(TalkDirector, TalkDirectorNrvPrep))) {
        mMsgCtrl = mBalloonCtrl;
        mBalloonCtrl = mRequestCtrl;
        mRequestCtrl = nullptr;
    } else {
        mMsgCtrl = mRequestCtrl;
        mBalloonCtrl = mMsgCtrl;
        mRequestCtrl = nullptr;
    }

    if (mMsgCtrl != nullptr) {
        mLastTalkCtrl = mMsgCtrl;
    }

    if (mBalloonCtrl != nullptr) {
        mBalloonCtrl->updateBalloonPos();

        TalkPeekZ* peek = mPeekZ;
        Vec2f v1(mBalloonCtrl->mBalloonPos.x, mBalloonCtrl->mBalloonPos.y);
        peek->mScreenPos.set(v1.x, v1.y);
    }

    if (MR::isPowerStarGetDemoActive()) {
        mMsgCtrl = nullptr;
        mIsInvalidTalk = true;
    }
}

void TalkDirector::prepTalk(TalkMessageCtrl* pCtrl, bool arg2, bool arg3, bool arg4) {
    mIsSkipTerm = arg2;
    mIsTalkStart = true;
    mIsPlayerTalk = arg4;

    mIsInvalidClipping = MR::isInvalidClipping(pCtrl->mHostActor);
    mDemoType = getDemoType(pCtrl, arg3);

    if (mDemoType == 0) {
        mIsPlayerTalk = false;
    }

    MR::invalidateClipping(pCtrl->mHostActor);

    switch (mDemoType) {
    case 2:
        MR::pauseTimeKeepDemo(pCtrl->mHostActor);
        break;
    case 1:
        // "Discussion"
        MR::tryStartDemoMarioPuppetable(pCtrl->mHostActor, "会話");
        MR::requestMovementOn(this);
        mBalloonHolder->pauseOff();
        mStateHolder->pauseOff();
        break;
    }

    if (mIsPlayerTalk) {
        MR::startPlayerTalk(pCtrl->mHostActor);
    }
}

void TalkDirector::termTalk() {
    TalkMessageCtrl* control = mTalkState->mMsgCtrl;
    mIsTalkEnd = true;

    if (mMessageInfo.isCameraNormal()) {
        MR::endNPCTalkCamera(false, -1);
    } else if (mMessageInfo.isCameraEvent()) {
        MR::endMultiActorCamera(mHostActor, mCameraInfo, "会話", false);
    }

    if (!mIsInvalidClipping) {
        MR::validateClipping(control->mHostActor);
    }

    if (mIsPlayerTalk) {
        MR::endPlayerTalk();
    }

    switch (mDemoType) {
    case 2:
        MR::resumeTimeKeepDemo(control->mHostActor);
        break;
    case 1:
        MR::endDemo(control->mHostActor, "会話");
        break;
    }

    mDemoType = 0;
    mIsPlayerTalk = false;
    mIsInvalidClipping = false;
}

TalkState* TalkDirector::initState(TalkMessageCtrl* pCtrl) {
    TalkState* state = mStateHolder->getState(pCtrl);
    TalkBalloon* balloon = mBalloonHolder->getBalloon(pCtrl);
    state->init(pCtrl, balloon);

    TalkMessageInfo* info = TalkFunction::getMessageInfo(pCtrl);

    if (info->isCameraNormal() || info->isCameraEvent()) {
        info = TalkFunction::getMessageInfo(pCtrl);
        mMessageInfo = *info;
        mCameraInfo = pCtrl->mCameraInfo;
        mHostActor = pCtrl->mHostActor;
    }

    return state;
}

bool TalkDirector::isInvalidTalk() const {
    if (mIsInvalidTalk) {
        return true;
    }

    if (MR::isStarPointerModeBlueStarReady()) {
        return true;
    }

    if (MR::isFirstPersonCamera()) {
        return true;
    }

    return MR::isPlayerDead();
}

void TalkDirector::appearYesNoSelector(const TalkMessageCtrl* pCtrl) const {
    const char* branchID = pCtrl->getBranchID();

    MR::FormattedName buffYes("Select_%s_Yes", branchID);

    MR::FormattedName buffNo("Select_%s_No", branchID);

    if (pCtrl->isSelectYesNo()) {
        MR::resetYesNoSelectorSE();
    } else {
        MR::setYesNoSelectorSE("SE_SY_TALK_FOCUS_ITEM", "SE_SY_TALK_SELECT_YES", "SE_SY_TALK_SELECT_YES");
    }

    MR::requestMovementOn(MR::getGameSceneLayoutHolder()->mYesNoLayout);
    MR::appearYesNoSelector(buffYes.c_str(), buffNo.c_str(), nullptr);
}

s32 TalkDirector::getDemoType(const TalkMessageCtrl* pCtrl, bool arg2) const {
    s32 demoType;

    if (TalkFunction::isShortTalk(pCtrl)) {
        demoType = 0;
    } else if (MR::isTimeKeepDemoActive()) {
        if (arg2) {
            demoType = 2;
        } else {
            demoType = 3;
        }
    } else {
        demoType = 1;
    }

    if (!arg2 && demoType != 3) {
        return 0;
    }

    return demoType;
}

// Gets the arg-th bool of TalkDirector's booleans, which start at 0x70
bool TalkDirector::getBranchResult(u16 arg) {
    if (arg == 18) {
        return MR::isAnyPlayerLeftSupply();
    }

    if (arg == 12) {
        return MR::isLuigiDisappearFromAstroGalaxy();
    }

    if (arg == 13) {
        return MR::isOnLuigiHiding();
    }

    return (reinterpret_cast< bool* >(this) + arg)[0x70];
}

void TalkDirector::initBranchResult() {
    mIsKinopioExplorerRescued = MR::isKinopioExplorerRescued();
    mIsKinopioExplorerOrganize = MR::isKinopioExplorerOrganize();
    mIsKinopioExplorerTalkGetGrandStar2 = MR::isKinopioExplorerTalkGetGrandStar2();
    mIsKinopioExplorerTrickComet = MR::isKinopioExplorerTrickComet();
    mIsKinopioExplorerTalkGetGrandStar3 = MR::isKinopioExplorerTalkGetGrandStar3();
    mIsKinopioExplorerStartMessenger = MR::isKinopioExplorerStartMessenger();
    mIsKinopioExplorerTalkGetGrandStar4 = MR::isKinopioExplorerTalkGetGrandStar4();
    mIsKinopioExplorerTalkGetGrandStar5 = MR::isKinopioExplorerTalkGetGrandStar5();
    mIsKinopioExplorerTalkGetGrandStar6 = MR::isKinopioExplorerTalkGetGrandStar6();
    mIsKinopioExplorerTalkGoFinalBattle = MR::isKinopioExplorerTalkGoFinalBattle();
    mIsEndLuigiHideAndSeek = MR::isEndLuigiHideAndSeekEvent();
    mIsKinopioExplorerCompleteTrickComet = MR::isKinopioExplorerCompleteTrickComet();
    mHasOneGreenStar = MR::calcCurrentGreenStarNum() == 1;
    mHasTwoGreenStars = MR::calcCurrentGreenStarNum() == 2;
    mHasThreeGreenStars = MR::calcCurrentGreenStarNum() == 3;
    mIsUnlockedRedDriver = MR::isOnGameEventFlagRedDriver();
    mIsActiveLuigiHideAndSeek = MR::isActiveLuigiHideAndSeekEvent();
    mIsGalaxyPurpleCometLaunch = MR::isGalaxyPurpleCometLaunch();
    misRosettaTalkTorchLecture = MR::isRosettaTalkTorchLecture();
    mIsRosettaTalkTrickComet = MR::isRosettaTalkTrickComet();
    mIsRosettaTalkKoopa = MR::isRosettaTalkKoopa();
    mIsRosettaTalkCountDownStart = MR::isRosettaTalkCountDownStart();
    mIsRosettaTalkAstroDemoRecover = MR::isRosettaTalkAstroDomeRecover();
    mIsRosettaTalkTorchProgress = MR::isRosettaTalkTorchProgress();
    mIsOnGameEventFlagViewNormalEnding = MR::isOnGameEventFlagViewNormalEnding();
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
    return isSystemTalking() && mDemoType == 1;
}

LiveActor* TalkDirector::getTalkingActor() const {
    if (isSystemTalking()) {
        return mTalkState->mMsgCtrl->mHostActor;
    }

    return nullptr;
}

void TalkDirector::exePrep() {
    if (MR::isLessStep(this, 4)) {
    }

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
    TalkMessageCtrl* control = mTalkState->mMsgCtrl;
    TalkFunction::onTalkStateTalking(control);

    bool cond = false;

    if (mDemoType == 0) {
        cond = mTalkState->isLostMessage(mMsgCtrl);
    }

    if (!cond) {
        if (!mTalkState->talk(mMsgCtrl)) {
            return;
        }

        MR::readMessage(control);
    }

    if (TalkFunction::isSelectTalk(control)) {
        setNerve(GET_NERVE(TalkDirector, TalkDirectorNrvSlct));
        return;
    }

    mTalkState->clos();
    mIsNext = false;

    if (control->mIsOnRootNodeAuto && !mTalkState->isSelfInterrupt(mLastTalkCtrl)) {
        control->rootNodePst();
    }

    setNerve(GET_NERVE(TalkDirector, TalkDirectorNrvNext));
    exeNext();
}

void TalkDirector::exeSlct() {
    TalkMessageCtrl* control = mTalkState->mMsgCtrl;

    if (MR::isFirstStep(this)) {
        appearYesNoSelector(control);
        MR::startSystemSE("SE_SM_TALKBLN_OPEN");
    }

    TalkFunction::onTalkStateTalking(control);

    if (MR::isYesNoSelected()) {
        MR::resetYesNoSelectorSE();
        mTalkState->clos();
        mIsNext = true;

        if (control->mIsOnRootNodeAuto) {
            control->rootNodeSel(MR::isYesNoSelectedYes());
            control->rootNodePre(true);
        }

        setNerve(GET_NERVE(TalkDirector, TalkDirectorNrvNext));
        exeNext();
    }
}

void TalkDirector::exeNext() {
    TalkMessageCtrl* control = mTalkState->mMsgCtrl;

    bool cond = control->isCurrentNodeContinue();

    if (control->rootNodeEve()) {
        mIsNext |= cond;

        if (mIsNext) {
            if (TalkFunction::isShortTalk(control)) {
                termTalk();
            }
            control->rootNodePre(true);
            mTalkState = initState(control);
            mTalkState->open();
            setNerve(GET_NERVE(TalkDirector, TalkDirectorNrvTalk));

            return;
        }
        termTalk();
        TalkFunction::onTalkStateEnableEnd(control);
        setNerve(GET_NERVE(TalkDirector, TalkDirectorNrvTerm));
    }
}

void TalkDirector::exeTerm() {
    if (!mIsSkipTerm) {
        if (!mTalkState->term(mMsgCtrl)) {
            return;
        }
    }

    TalkFunction::onTalkStateNone(mTalkState->mMsgCtrl);
    mTalkState = nullptr;

    setNerve(GET_NERVE(TalkDirector, TalkDirectorNrvWait));
}

bool TalkFunction::requestTalkSystem(TalkMessageCtrl* pCtrl, bool force) {
    return ::getTalkDirector()->request(pCtrl, force);
}

bool TalkFunction::startTalkSystem(TalkMessageCtrl* pCtrl, bool force, bool demo, bool notPuppetable) {
    return ::getTalkDirector()->start(pCtrl, force, demo, notPuppetable);
}

bool TalkFunction::endTalkSystem(TalkMessageCtrl* pCtrl) {
    ::getTalkDirector();
    return MR::isTalkEnableEnd(pCtrl);
}

bool TalkFunction::isTalkSystemStart(const TalkMessageCtrl* pCtrl) {
    TalkDirector* pDirector = ::getTalkDirector();

    return pDirector->mMsgCtrl == pCtrl && pDirector->mIsTalkStart;
}

bool TalkFunction::isTalkSystemEnd(const TalkMessageCtrl* pCtrl) {
    TalkDirector* pDirector = ::getTalkDirector();

    return pDirector->mMsgCtrl == pCtrl && pDirector->mIsTalkEnd;
}

bool TalkFunction::getBranchAstroGalaxyResult(u16 arg) {
    return ::getTalkDirector()->getBranchResult(arg);
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
    if (::getTalkDirector() != nullptr) {
        ::getTalkDirector()->mPeekZ->setDrawSyncToken();
    }
}

bool MR::isActiveTalkBalloonShort() {
    if (::getTalkDirector() != nullptr) {
        return ::getTalkDirector()->mBalloonHolder->isActiveBalloonShort();
    }

    return false;
}
