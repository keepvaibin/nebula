#include "Game/NPC/TalkMessageCtrl.hpp"
#include "Game/AreaObj/MessageArea.hpp"
#include "Game/LiveActor/ActorCameraInfo.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/NPC/TalkDirector.hpp"
#include "Game/NPC/TalkMessageFunc.hpp"
#include "Game/NPC/TalkMessageInfo.hpp"
#include "Game/NPC/TalkNodeCtrl.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/MessageHolder.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/NPCUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/TalkUtil.hpp"
#include <nebula_geometry.hpp>

#include <nebula_math.h>
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    // Indexed by a select branch's mNextIdx.
    const char* const sSelectBranchID[] = {
        "PenguinRace",       "SwimmingSchool",    "PenguinRace",       "BombTimeAttackLv1",         "PhantomTeresaRacer",
        "BombTimeAttackLv2", "TrialSurfingCoach", "TrialSurfingHowTo", "DeathPromenadeTeresaRacer", "RosettaFinalBattle",
        "CometTico",         "TransformTico",     "ChallengeSurfingCoach", "TicoShopExchange",      "TicoShopWhich",
        "KinopioPurple",     "CometTicoTell",     "TrialTamakoroHowTo", "KnockOnTheDoor",
    };

    // Selects that are not a yes/no question.
    NEBULA_TUNABLE(u32, sSelectTicoShopWhich, 14);
    NEBULA_TUNABLE(u32, sSelectCometTicoTell, 16);

    NEBULA_TUNABLE(f32, sDefaultTalkDistance, 240.0f);
    NEBULA_TUNABLE(f32, sComposeTalkDistanceScale, 2.0f);

    // Events that go on to the next node by themselves.
    bool isNodeEventContinue(const TalkNode* pEvent) {
        return pEvent->mGroupID <= EventKind_Anime && (1 << pEvent->mGroupID & (1 << EventKind_Func | 1 << EventKind_None | 1 << EventKind_Anime));
    }

    TalkNode* getSelectBranch(const TalkNodeCtrl* pNodeCtrl) {
        TalkNode* pNode = pNodeCtrl->getNextNodeBranch();

        if (pNode == nullptr || pNode->mIndex != BranchKind_Select) {
            return nullptr;
        }

        return pNode;
    }
};  // namespace

TalkMessageCtrl::TalkMessageCtrl(LiveActor* pHost, const Vec3f& rBalloonOffset, MtxPtr pBalloonMtx)
    : NameObj("会話制御"), mHostActor(pHost), mNodeCtrl(), mZoneID(-1), mTalkStateKind(TalkState_None), mBalloonPos(0.0f, 0.0f, 0.0f), mMtx(pBalloonMtx),
      mMsgBalloonFollowOffs(rBalloonOffset), mTalkDistance(::sDefaultTalkDistance), mAlreadyDoneFlag(), mAlreadyDoneFlags(), mIsOnRootNodeAuto(),
      mIsOnReadNodeAuto(true), mIsStartOnlyFront(), mCameraInfo(), mBranchFunc(), mEventFunc(), mAnimeFunc(), mKillFunc(),
      mTagArg(nullptr, CustomTagArg::Type_Uninitialized) {
    lumacore::setClass(this, lumacore::kClassId< TalkMessageCtrl >);
    MR::createSceneObj(SceneObj_TalkDirector);
    TalkFunction::registerTalkSystem(this);
    mZoneID = MR::getCurrentPlacementZoneId();
}

bool TalkMessageCtrl::inMessageArea() const {
    s32 areaId = mNodeCtrl->mMessageInfo.mAreaId;

    if (areaId < 0) {
        return false;
    }

    MessageArea* pArea = static_cast< MessageArea* >(MR::getAreaObj("MessageArea", *MR::getPlayerCenterPos()));
    return pArea != nullptr && areaId == pArea->mObjArg0 && mZoneID == pArea->mZoneID;
}

void TalkMessageCtrl::startCamera(s32 frames) {
    TalkMessageInfo* pInfo = &mNodeCtrl->mMessageInfo;

    if (pInfo->isCameraNormal()) {
        MR::startNPCTalkCamera(this, mHostActor->getBaseMtx(), 1.0f, frames);
    } else if (pInfo->isCameraEvent() && pInfo->mCameraSetID) {
        mCameraInfo->mCameraSetID = pInfo->mCameraSetID;
        MR::startMultiActorCameraNoTarget(mHostActor, mCameraInfo, "会話", frames);
    }
}

void TalkMessageCtrl::createMessage(const TableIter& rIter, const char* pName) {
    mNodeCtrl = new TalkNodeCtrl();
    mNodeCtrl->createFlowNode(this, rIter, pName, &mCameraInfo);

    if (mNodeCtrl->mCurrentNode != nullptr) {
        mAlreadyDoneFlags = MR::setupAlreadyDoneFlag(mNodeCtrl->mFlowName, rIter, &mAlreadyDoneFlag);
    } else {
        mAlreadyDoneFlag = 1;
    }
}

void TalkMessageCtrl::createMessageDirect(const TableIter& rIter, const char* pName) {
    mNodeCtrl = new TalkNodeCtrl();
    mNodeCtrl->createFlowNodeDirect(this, rIter, pName, &mCameraInfo);
    mAlreadyDoneFlag = 1;
}

bool TalkMessageCtrl::rootNodeEve() {
    TalkNodeCtrl* pNodeCtrl = mNodeCtrl;
    TalkNode* pEvent = pNodeCtrl->getCurrentNodeEvent();

    if (pEvent == nullptr) {
        return true;
    }

    // A function that returns false holds the flow on this event.
    switch (pEvent->mGroupID) {
    case EventKind_Func:
    case EventKind_FuncStop:
        if (mEventFunc == nullptr) {
            return true;
        }

        if (!(*mEventFunc)(pEvent->mArg)) {
            return false;
        }

        break;

    case EventKind_Anime:
        if (mAnimeFunc == nullptr) {
            return true;
        }

        if (!(*mAnimeFunc)(pEvent->mArg)) {
            return false;
        }

        break;

    case EventKind_Skip:
        rootNodePst();
        return true;

    case EventKind_SwitchA:
        MR::onSwitchA(mHostActor);
        break;

    case EventKind_SwitchB:
        MR::onSwitchB(mHostActor);
        break;

    case EventKind_Kill:
        if (mKillFunc == nullptr) {
            return true;
        }

        if (!(*mKillFunc)(pEvent->mArg)) {
            return false;
        }

        break;
    }

    // Another event right after this one runs next frame.
    bool isExistNext = pNodeCtrl->isExistNextNode();
    rootNodePst();

    return !(isExistNext && pNodeCtrl->getCurrentNodeEvent());
}

void TalkMessageCtrl::rootNodePre(bool isStopAtEvent) {
    TalkNodeCtrl* pNodeCtrl = mNodeCtrl;
    pNodeCtrl->resetTempFlowNode();

    while (true) {
        TalkNode* pBranch = pNodeCtrl->getCurrentNodeBranch();

        if (pBranch == nullptr) {
            if (isStopAtEvent || !isCurrentNodeContinue()) {
                return;
            }

            pNodeCtrl->forwardFlowNode();
            continue;
        }

        bool isTrue = false;

        switch (pBranch->mIndex) {
        case BranchKind_Select:
            return;
        case BranchKind_Func:
            isTrue = mBranchFunc == nullptr || (*mBranchFunc)(pBranch->mNextIdx);
            break;
        case BranchKind_NearPlayer:
            isTrue = MR::isNearPlayerAnyTime(mHostActor, mTalkDistance);
            break;
        case BranchKind_SwitchA:
            isTrue = MR::isOnSwitchA(mHostActor);
            break;
        case BranchKind_SwitchB:
            isTrue = MR::isOnSwitchB(mHostActor);
            break;
        case BranchKind_PlayerNormal:
            isTrue = MR::isPlayerElementModeNormal();
            break;
        case BranchKind_PlayerBee:
            isTrue = MR::isPlayerElementModeBee();
            break;
        case BranchKind_PlayerTeresa:
            isTrue = MR::isPlayerElementModeTeresa();
            break;
        case BranchKind_PowerStarAppeared:
            isTrue = MR::isStageStatePowerStarAppeared();
            break;
        case BranchKind_AlreadyDone:
            isTrue = mAlreadyDoneFlag != 0;
            break;
        case BranchKind_PlayerLuigi:
            isTrue = MR::isPlayerLuigi();
            break;
        case BranchKind_TimeKeepDemo:
            isTrue = MR::isTimeKeepDemoActive();
            break;
        case BranchKind_MessageRead:
            isTrue = MR::isOnMessageAlreadyRead(pBranch->mNextIdx);
            break;
        case BranchKind_GalaxyResult:
            isTrue = TalkFunction::getBranchAstroGalaxyResult(pBranch->mNextIdx);
            break;
        }

        pNodeCtrl->forwardCurrentBranchNode(isTrue);
    }
}

void TalkMessageCtrl::rootNodePst() {
    TalkNodeCtrl* pNodeCtrl = mNodeCtrl;

    if (!pNodeCtrl->isExistNextNode()) {
        pNodeCtrl->resetFlowNode();
    } else {
        pNodeCtrl->forwardFlowNode();
    }

    pNodeCtrl->recordTempFlowNode();
}

bool TalkMessageCtrl::isCurrentNodeContinue() const {
    TalkNode* pEvent = mNodeCtrl->getCurrentNodeEvent();
    return pEvent != nullptr && ::isNodeEventContinue(pEvent);
}

void TalkMessageCtrl::rootNodeSel(bool isYes) {
    TalkNodeCtrl* pNodeCtrl = mNodeCtrl;
    pNodeCtrl->forwardFlowNode();
    pNodeCtrl->forwardCurrentBranchNode(isYes);
    pNodeCtrl->recordTempFlowNode();
}

void TalkMessageCtrl::registerBranchFunc(const TalkMessageFuncBase& rFunc) {
    mBranchFunc = rFunc.clone();
}

void TalkMessageCtrl::registerEventFunc(const TalkMessageFuncBase& rFunc) {
    mEventFunc = rFunc.clone();
}

void TalkMessageCtrl::registerAnimeFunc(const TalkMessageFuncBase& rFunc) {
    mAnimeFunc = rFunc.clone();
}

void TalkMessageCtrl::registerKillFunc(const TalkMessageFuncBase& rFunc) {
    mKillFunc = rFunc.clone();
}

void TalkMessageCtrl::readMessage() {
    if (mIsOnReadNodeAuto) {
        mNodeCtrl->readMessage();
    }

    if (!mAlreadyDoneFlag) {
        MR::updateAlreadyDoneFlag(mAlreadyDoneFlags, 1);
    }
}

bool TalkMessageCtrl::isSelectYesNo() const {
    TalkNode* pNode = ::getSelectBranch(mNodeCtrl);
    return pNode != nullptr && pNode->mNextIdx != ::sSelectCometTicoTell && pNode->mNextIdx != ::sSelectTicoShopWhich;
}

u32 TalkMessageCtrl::getMessageID() const {
    TalkNode* pMessage = mNodeCtrl->getCurrentNodeMessage();

    if (pMessage != nullptr) {
        return pMessage->mIndex;
    }

    return mNodeCtrl->mCurrentNodeIdx;
}

bool TalkMessageCtrl::requestTalk() {
    return TalkFunction::requestTalkSystem(this, false);
}

bool TalkMessageCtrl::requestTalkForce() {
    return TalkFunction::requestTalkSystem(this, true);
}

bool TalkMessageCtrl::startTalk() {
    return TalkFunction::startTalkSystem(this, false, true, true);
}

bool TalkMessageCtrl::startTalkForce() {
    return TalkFunction::startTalkSystem(this, true, true, true);
}

bool TalkMessageCtrl::startTalkForcePuppetable() {
    return TalkFunction::startTalkSystem(this, true, true, false);
}

bool TalkMessageCtrl::startTalkForceWithoutDemo() {
    return TalkFunction::startTalkSystem(this, true, false, true);
}

bool TalkMessageCtrl::startTalkForceWithoutDemoPuppetable() {
    return TalkFunction::startTalkSystem(this, true, false, false);
}

bool TalkMessageCtrl::endTalk() {
    return TalkFunction::endTalkSystem(this);
}

bool TalkMessageCtrl::isNearPlayer(const TalkMessageCtrl* pOther) {
    if (pOther == nullptr) {
        return true;
    }

    const Vec3f& rPlayerPos = *MR::getPlayerPos();
    return mHostActor->mPosition.squared(rPlayerPos) < pOther->mHostActor->mPosition.squared(rPlayerPos);
}

const char* TalkMessageCtrl::getBranchID() const {
    TalkNode* pNode = ::getSelectBranch(mNodeCtrl);

    if (pNode == nullptr || pNode->mNextIdx >= ARRAY_SIZEU(::sSelectBranchID)) {
        return nullptr;
    }

    return ::sSelectBranchID[pNode->mNextIdx];
}

s32 TalkMessageCtrl::getOwnSelectNode() const {
    const TalkNode* pNode = ::getSelectBranch(mNodeCtrl);

    if (pNode == nullptr || pNode->mNextIdx != cSelectOwnChoices) {
        return -1;
    }

    return static_cast< s32 >(pNode - MessageSystem::getSceneMessageData()->getNode(0));
}

bool TalkFunction::isShortTalk(const TalkMessageCtrl* pCtrl) {
    return pCtrl->mNodeCtrl->mMessageInfo.isShortTalk();
}

bool TalkFunction::isComposeTalk(const TalkMessageCtrl* pCtrl) {
    return pCtrl->mNodeCtrl->mMessageInfo.isComposeTalk();
}

bool TalkFunction::isSelectTalk(const TalkMessageCtrl* pCtrl) {
    return pCtrl->mNodeCtrl->mNodeData == 0;
}

bool TalkFunction::isEventNode(const TalkMessageCtrl* pCtrl) {
    return pCtrl->mNodeCtrl->isCurrentNodeEvent();
}

void TalkFunction::onTalkStateNone(TalkMessageCtrl* pCtrl) {
    pCtrl->mTalkStateKind = TalkState_None;
}

void TalkFunction::onTalkStateEntry(TalkMessageCtrl* pCtrl) {
    pCtrl->mTalkStateKind = TalkState_Entry;
}

void TalkFunction::onTalkStateEnableStart(TalkMessageCtrl* pCtrl) {
    pCtrl->mTalkStateKind = TalkState_EnableStart;
}

void TalkFunction::onTalkStateTalking(TalkMessageCtrl* pCtrl) {
    pCtrl->mTalkStateKind = TalkState_Talking;
}

void TalkFunction::onTalkStateEnableEnd(TalkMessageCtrl* pCtrl) {
    pCtrl->mTalkStateKind = TalkState_EnableEnd;
}

TalkMessageInfo* TalkFunction::getMessageInfo(const TalkMessageCtrl* pCtrl) {
    return &pCtrl->mNodeCtrl->mMessageInfo;
}

const char16_t* TalkFunction::getMessage(const TalkMessageCtrl* pCtrl) {
    return pCtrl->mNodeCtrl->mMessageInfo.mMessage;
}

const char16_t* TalkFunction::getSubMessage(const TalkMessageCtrl* pCtrl) {
    return pCtrl->mNodeCtrl->getSubMessage();
}

bool TalkMessageCtrl::isNearPlayer(f32 distance) const {
    if (mNodeCtrl->mMessageInfo.isNullTalk()) {
        return false;
    }

    // A negative talk distance talks from anywhere.
    if (mTalkDistance < 0.0f) {
        return true;
    }

    if (distance <= 0.0f) {
        distance = mTalkDistance;

        if (mNodeCtrl->mMessageInfo.isComposeTalk()) {
            distance = distance * ::sComposeTalkDistanceScale;
        }
    }

    Vec3f talkPos(mHostActor->mPosition);

    if (mMtx != nullptr) {
        talkPos.set< f32 >(mMtx[0][3], mMtx[1][3], mMtx[2][3]);
    }

    // Within distance along gravity and across it.
    Vec3f offsetH;
    f32 height = MR::vecKillElement(talkPos - *MR::getPlayerCenterPos(), mHostActor->mGravity, &offsetH);

    if (!(MR::abs(height) > distance) && offsetH.squared() < distance * distance) {
        if (!mIsStartOnlyFront && !mNodeCtrl->mMessageInfo.isBalloonSign()) {
            return true;
        }

        // Front-only talks and signs need the player in front of the host.
        Pos3f hostMtx;
        hostMtx.set(mHostActor->getBaseMtx());

        Vec3f front;
        hostMtx.getZDir(front);

        Vec3f toPlayer;
        hostMtx.getTrans(toPlayer);
        toPlayer = *MR::getPlayerPos() - toPlayer;

        if (MR::vecKillElement(toPlayer, front, &toPlayer) > 0.0f) {
            return true;
        }
    }

    return inMessageArea();
}

void TalkMessageCtrl::updateBalloonPos() {
    Mtx34f hostMtx;
    hostMtx.set(mHostActor->getBaseMtx());

    if (mMtx != nullptr) {
        hostMtx.set(mMtx);
    }

    // The follow offset is in camera space.
    Pos3f cameraMtx = MR::getCameraInvViewMtx();
    MR::setMtxTrans(cameraMtx, 0.0f, 0.0f, 0.0f);
    MR::addTransMtxLocal(cameraMtx, mMsgBalloonFollowOffs);

    Vec3f balloonPos;
    balloonPos.set< f32 >(hostMtx[0][3], hostMtx[1][3], hostMtx[2][3]);
    balloonPos.add(Vec3f(cameraMtx[0][3], cameraMtx[1][3], cameraMtx[2][3]));
    MR::calcScreenPosition(&mBalloonPos, balloonPos);
}

const char* TalkFunction::getMessageName(const TalkMessageCtrl* pCtrl) {
    const TalkNodeCtrl* pNodes = pCtrl->mNodeCtrl;
    if (pNodes == nullptr) {
        return nullptr;
    }

    // A message placed by its own name has no flow node: the name is the message's.
    if (pNodes->mCurrentNode == nullptr) {
        return pNodes->mFlowName;
    }

    return MessageSystem::getSceneMessageData()->findMessageName(pCtrl->getMessageID());
}
