#include "Game/Util/TalkUtil.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/NPC/TalkMessageCtrl.hpp"
#include "Game/NPC/TalkMessageFunc.hpp"
#include "Game/NPC/TalkNodeCtrl.hpp"
#include "Game/Screen/GameSceneLayoutHolder.hpp"
#include "Game/Screen/YesNoController.hpp"
#include "Game/Screen/YesNoLayout.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"

namespace {
    const Vec3f sNoOffset(0.0f, 0.0f, 0.0f);
}  // namespace

namespace MR {
    void registerBranchFunc(TalkMessageCtrl* pCtrl, const TalkMessageFuncBase& rFunc) {
        if (pCtrl == nullptr) {
            return;
        }
        pCtrl->registerBranchFunc(rFunc);
    }

    void registerEventFunc(TalkMessageCtrl* pCtrl, const TalkMessageFuncBase& rFunc) {
        if (pCtrl == nullptr) {
            return;
        }
        pCtrl->registerEventFunc(rFunc);
    }

    void registerAnimeFunc(TalkMessageCtrl* pCtrl, const TalkMessageFuncBase& rFunc) {
        if (pCtrl == nullptr) {
            return;
        }
        pCtrl->registerAnimeFunc(rFunc);
    }

    void registerKillFunc(TalkMessageCtrl* pCtrl, const TalkMessageFuncBase& rFunc) {
        if (pCtrl == nullptr) {
            return;
        }
        pCtrl->registerKillFunc(rFunc);
    }

    void setMessageArg(TalkMessageCtrl* pCtrl, int value) {
        if (pCtrl == nullptr) {
            return;
        }
        CustomTagArg arg(value, CustomTagArg::Type_Int);
        pCtrl->setMessageArg(arg);
    }

    void setMessageArg(TalkMessageCtrl* pCtrl, const char16_t* pArg) {
        if (pCtrl == nullptr) {
            return;
        }
        CustomTagArg arg(pArg, CustomTagArg::Type_Char);
        pCtrl->setMessageArg(arg);
    }

    // A mod's object made like a game's talks with its own lines: "<zone>_<its name><number>", as its model is its own.
    TalkMessageCtrl* createTalkCtrl(LiveActor* pActor, const TableIter& rIter, const char* pMessageID, const Vec3f& rOffset, MtxPtr pMtx) {
        TalkMessageCtrl* pCtrl = new TalkMessageCtrl(pActor, rOffset, pMtx);
        pCtrl->createMessage(rIter, NebulaMods::modelFor(pActor != nullptr ? pActor->mName : nullptr, pMessageID));
        return pCtrl;
    }

    TalkMessageCtrl* createTalkCtrlDirect(LiveActor* pActor, const TableIter& rIter, const char* pMessageID, const Vec3f& rOffset, MtxPtr pMtx) {
        TalkMessageCtrl* pCtrl = new TalkMessageCtrl(pActor, rOffset, pMtx);
        pCtrl->createMessageDirect(rIter, NebulaMods::modelFor(pActor != nullptr ? pActor->mName : nullptr, pMessageID));
        return pCtrl;
    }

    TalkMessageCtrl* createTalkCtrlDirectOnRootNodeAutomatic(LiveActor* pActor, const TableIter& rIter, const char* pMessageID,
                                                             const Vec3f& rOffset, MtxPtr pMtx) {
        TalkMessageCtrl* pCtrl = createTalkCtrlDirect(pActor, rIter, pMessageID, rOffset, pMtx);
        onRootNodeAutomatic(pCtrl);
        return pCtrl;
    }

    bool tryTalkNearPlayer(TalkMessageCtrl* pCtrl) {
        if (pCtrl == nullptr) {
            return false;
        }
        if (MR::isTimeKeepDemoActive()) {
            return false;
        }

        return pCtrl->requestTalk() && pCtrl->startTalk();
    }

    bool tryTalkNearPlayerAtEnd(TalkMessageCtrl* pCtrl) {
        if (pCtrl == nullptr) {
            return false;
        }
        if (MR::isTimeKeepDemoActive()) {
            return false;
        }

        if (pCtrl->endTalk()) {
            pCtrl->requestTalk();
            return true;
        } else {
            if (pCtrl->requestTalk()) {
                pCtrl->startTalk();
            }

            return false;
        }
    }

    bool tryTalkForce(TalkMessageCtrl* pCtrl) {
        if (pCtrl == nullptr) {
            return false;
        }
        if (MR::isTimeKeepDemoActive()) {
            return false;
        }

        return pCtrl->requestTalkForce() && pCtrl->startTalkForce();
    }

    bool tryTalkForceAtEnd(TalkMessageCtrl* pCtrl) {
        if (pCtrl == nullptr) {
            return false;
        }
        if (MR::isTimeKeepDemoActive()) {
            return false;
        }

        if (pCtrl->endTalk()) {
            return true;
        }

        if (pCtrl->requestTalkForce()) {
            pCtrl->startTalkForce();
        }

        return false;
    }

    bool tryTalkForceWithoutDemo(TalkMessageCtrl* pCtrl) {
        if (pCtrl == nullptr) {
            return false;
        }
        if (MR::isTimeKeepDemoActive()) {
            return false;
        }

        return pCtrl->requestTalkForce() && pCtrl->startTalkForceWithoutDemo();
    }

    bool tryTalkForceWithoutDemoMarioPuppetable(TalkMessageCtrl* pCtrl) {
        if (pCtrl == nullptr) {
            return false;
        }
        if (MR::isTimeKeepDemoActive()) {
            return false;
        }

        return pCtrl->requestTalkForce() && pCtrl->startTalkForceWithoutDemoPuppetable();
    }

    bool tryTalkForceWithoutDemoAtEnd(TalkMessageCtrl* pCtrl) {
        if (pCtrl == nullptr) {
            return false;
        }
        if (MR::isTimeKeepDemoActive()) {
            return false;
        }

        if (pCtrl->endTalk()) {
            return true;
        }

        if (pCtrl->requestTalkForce()) {
            pCtrl->startTalkForceWithoutDemo();
        }

        return false;
    }

    bool tryTalkForceWithoutDemoMarioPuppetableAtEnd(TalkMessageCtrl* pCtrl) {
        if (pCtrl == nullptr) {
            return false;
        }
        if (MR::isTimeKeepDemoActive()) {
            return false;
        }

        if (pCtrl->endTalk()) {
            return true;
        }

        if (pCtrl->requestTalkForce()) {
            pCtrl->startTalkForceWithoutDemoPuppetable();
        }

        return false;
    }

    bool tryTalkTimeKeepDemo(TalkMessageCtrl* pCtrl) {
        if (pCtrl == nullptr) {
            return false;
        }
        return pCtrl->requestTalkForce() && pCtrl->startTalkForce();
    }

    bool tryTalkTimeKeepDemoMarioPuppetable(TalkMessageCtrl* pCtrl) {
        if (pCtrl == nullptr) {
            return false;
        }
        return pCtrl->requestTalkForce() && pCtrl->startTalkForcePuppetable();
    }

    bool tryTalkTimeKeepDemoWithoutPauseMarioPuppetable(TalkMessageCtrl* pCtrl) {
        if (pCtrl == nullptr) {
            return false;
        }
        return pCtrl->requestTalkForce() && pCtrl->startTalkForceWithoutDemoPuppetable();
    }

    bool tryTalkRequest(TalkMessageCtrl* pCtrl) {
        if (pCtrl == nullptr) {
            return false;
        }
        return pCtrl->requestTalk();
    }

    bool tryTalkSelectLeft(TalkMessageCtrl* pCtrl) {
        if (pCtrl == nullptr) {
            return false;
        }
        return MR::isYesNoSelected() && MR::isYesNoSelectedYes();
    }

    bool tryTalkSelectRight(TalkMessageCtrl* pCtrl) {
        if (pCtrl == nullptr) {
            return false;
        }
        return MR::isYesNoSelected() && !MR::isYesNoSelectedYes();
    }

    MtxPtr getMessageBalloonFollowMatrix(const TalkMessageCtrl* pCtrl) {
        if (pCtrl == nullptr) {
            return nullptr;
        }
        return pCtrl->mMtx;
    }

    const Vec3f& getMessageBalloonFollowOffset(const TalkMessageCtrl* pCtrl) {
        if (pCtrl == nullptr) {
            return sNoOffset;
        }
        return pCtrl->mMsgBalloonFollowOffs;
    }

    void setMessageBalloonFollowOffset(TalkMessageCtrl* pCtrl, const Vec3f& rOffs) {
        if (pCtrl == nullptr) {
            return;
        }
        pCtrl->setMessageBallonFollowOffs(rOffs);
    }

    bool isNearPlayer(const TalkMessageCtrl* pCtrl, f32 dist) {
        if (pCtrl == nullptr) {
            return false;
        }
        return pCtrl->isNearPlayer(dist);
    }

    bool inMessageArea(const TalkMessageCtrl* pCtrl) {
        if (pCtrl == nullptr) {
            return false;
        }
        return pCtrl->inMessageArea();
    }

    bool isTalkNone(const TalkMessageCtrl* pCtrl) {
        return pCtrl == nullptr || pCtrl->mTalkStateKind == TalkState_None;
    }

    bool isTalkEntry(const TalkMessageCtrl* pCtrl) {
        return pCtrl != nullptr && pCtrl->mTalkStateKind == TalkState_Entry;
    }

    bool isTalkTalking(const TalkMessageCtrl* pCtrl) {
        return pCtrl != nullptr && pCtrl->mTalkStateKind == TalkState_Talking;
    }

    bool isTalkEnableEnd(const TalkMessageCtrl* pCtrl) {
        return pCtrl != nullptr && pCtrl->mTalkStateKind == TalkState_EnableEnd;
    }

    void clearTalkState(TalkMessageCtrl* pCtrl) {
        if (pCtrl == nullptr) {
            return;
        }
        TalkFunction::onTalkStateNone(pCtrl);
    }

    void resetNode(TalkMessageCtrl* pCtrl) {
        if (pCtrl == nullptr) {
            return;
        }
        TalkFunction::onTalkStateNone(pCtrl);
        pCtrl->mNodeCtrl->resetFlowNode();
    }

    void readMessage(TalkMessageCtrl* pCtrl) {
        if (pCtrl == nullptr) {
            return;
        }
        pCtrl->readMessage();
    }

    void forwardNode(TalkMessageCtrl* pCtrl) {
        if (pCtrl == nullptr) {
            return;
        }
        TalkFunction::onTalkStateNone(pCtrl);
        pCtrl->mNodeCtrl->forwardFlowNode();
        pCtrl->mNodeCtrl->recordTempFlowNode();
    }

    void resetAndForwardNode(TalkMessageCtrl* pCtrl, s32 numNode) {
        if (pCtrl == nullptr) {
            return;
        }
        TalkFunction::onTalkStateNone(pCtrl);
        pCtrl->mNodeCtrl->resetFlowNode();

        for (s32 i = 0; i < numNode; i++) {
            TalkFunction::onTalkStateNone(pCtrl);
            pCtrl->mNodeCtrl->forwardFlowNode();
            pCtrl->mNodeCtrl->recordTempFlowNode();
        }
    }

    void forwardNodeNextBranchLeft(TalkMessageCtrl* pCtrl) {
        if (pCtrl == nullptr) {
            return;
        }
        TalkFunction::onTalkStateNone(pCtrl);
        pCtrl->mNodeCtrl->forwardFlowNode();
        pCtrl->mNodeCtrl->forwardCurrentBranchNode(true);
        pCtrl->mNodeCtrl->recordTempFlowNode();
    }

    void forwardNodeNextBranchRight(TalkMessageCtrl* pCtrl) {
        if (pCtrl == nullptr) {
            return;
        }
        TalkFunction::onTalkStateNone(pCtrl);
        pCtrl->mNodeCtrl->forwardFlowNode();
        pCtrl->mNodeCtrl->forwardCurrentBranchNode(false);
        pCtrl->mNodeCtrl->recordTempFlowNode();
    }

    void forwardNodeCurrentBranchLeft(TalkMessageCtrl* pCtrl) {
        if (pCtrl == nullptr) {
            return;
        }
        TalkFunction::onTalkStateNone(pCtrl);
        pCtrl->mNodeCtrl->forwardCurrentBranchNode(true);
        pCtrl->mNodeCtrl->recordTempFlowNode();
    }

    void forwardNodeCurrentBranchRight(TalkMessageCtrl* pCtrl) {
        if (pCtrl == nullptr) {
            return;
        }
        TalkFunction::onTalkStateNone(pCtrl);
        pCtrl->mNodeCtrl->forwardCurrentBranchNode(false);
        pCtrl->mNodeCtrl->recordTempFlowNode();
    }

    bool tryForwardNode(TalkMessageCtrl* pCtrl) {
        if (pCtrl == nullptr) {
            return false;
        }
        if (pCtrl->mNodeCtrl->isExistNextNode()) {
            TalkFunction::onTalkStateNone(pCtrl);
            TalkFunction::onTalkStateNone(pCtrl);
            pCtrl->mNodeCtrl->forwardFlowNode();
            pCtrl->mNodeCtrl->recordTempFlowNode();
            return true;
        }

        return false;
    }

    bool isExistNextNode(const TalkMessageCtrl* pCtrl) {
        if (pCtrl == nullptr) {
            return false;
        }
        return pCtrl->mNodeCtrl->isExistNextNode();
    }

    bool isShortTalk(const TalkMessageCtrl* pCtrl) {
        if (pCtrl == nullptr) {
            return false;
        }
        return TalkFunction::isShortTalk(pCtrl);
    }

    void setDistanceToTalk(TalkMessageCtrl* pCtrl, f32 dist) {
        if (pCtrl == nullptr) {
            return;
        }
        pCtrl->mTalkDistance = dist;
    }

    void onRootNodeAutomatic(TalkMessageCtrl* pCtrl) {
        if (pCtrl == nullptr) {
            return;
        }
        pCtrl->mIsOnRootNodeAuto = true;
    }

    void offRootNodeAutomatic(TalkMessageCtrl* pCtrl) {
        if (pCtrl == nullptr) {
            return;
        }
        pCtrl->mIsOnRootNodeAuto = false;
    }

    void onReadNodeAutomatic(TalkMessageCtrl* pCtrl) {
        if (pCtrl == nullptr) {
            return;
        }
        pCtrl->mIsOnReadNodeAuto = true;
    }

    void offReadNodeAutomatic(TalkMessageCtrl* pCtrl) {
        if (pCtrl == nullptr) {
            return;
        }
        pCtrl->mIsOnReadNodeAuto = false;
    }

    void onStartOnlyFront(TalkMessageCtrl* pCtrl) {
        if (pCtrl == nullptr) {
            return;
        }
        pCtrl->mIsStartOnlyFront = true;
    }

    bool isTalkStart(const TalkMessageCtrl* pCtrl) {
        if (pCtrl == nullptr) {
            return false;
        }
        return TalkFunction::isTalkSystemStart(pCtrl);
    }

    bool isTalkEnd(const TalkMessageCtrl* pCtrl) {
        if (pCtrl == nullptr) {
            return false;
        }
        return TalkFunction::isTalkSystemEnd(pCtrl);
    }
};  // namespace MR
