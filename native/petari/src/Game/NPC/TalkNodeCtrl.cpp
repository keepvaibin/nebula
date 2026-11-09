#include "Game/NPC/TalkNodeCtrl.hpp"
#include "Game/LiveActor/ActorCameraInfo.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/NPC/TalkMessageInfo.hpp"
#include "Game/Screen/MessageTagSkipTagProcessor.hpp"
#include "Game/System/MessageHolder.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/MessageTag.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/NebulaArena.hpp"

void TalkMessageHistory::entry(u32 msgID) {
    if (mCount == mMax) {
        const s32 max = mMax != 0 ? mMax * 2 : 16;
        u32* const pHistory = new (NebulaArena::findFromRoot(this), 0) u32[max];
        for (s32 i = 0; i < mCount; i++) {
            pHistory[i] = mHistory[i];
        }
        delete[] mHistory;
        mHistory = pHistory;
        mMax = max;
    }

    mHistory[mCount++] = msgID;
}

bool TalkMessageHistory::search(u32 msgID) const {
    for (s32 i = 0; i < mCount; i++) {
        if (mHistory[i] == msgID) {
            return true;
        }
    }

    return false;
}

TalkNodeCtrl::TalkNodeCtrl() : mFlowName(), mCurrentNodeIdx(-1), mMessageInfo() {
    mHistory.mHistory = nullptr;
    mHistory.mCount = 0;
    mHistory.mMax = 0;
    mStartNode = nullptr;
    mCurrentNode = nullptr;
    mNodeData = -1;
}

void TalkNodeCtrl::createFlowNodeDirect(TalkMessageCtrl* pMsgCtrl, const TableIter& rIter, const char* pName, ActorCameraInfo** pCameraInf) {
    // A null flow name is the empty name, which no node has.
    if (pName == nullptr) {
        pName = "";
    }
    TalkNode* node = MessageSystem::getSceneMessageData()->findNode(pName);
    mStartNode = node;
    mCurrentNode = node;
    mFlowNode = node;
    mFlowName = new char[strlen(pName) + 1];
    MR::copyString(mFlowName, pName, strlen(pName) + 1);

    if (mCurrentNode == nullptr) {
        char* name = mFlowName;
        MessageData* msgData = MessageSystem::getSceneMessageData();
        msgData->getMessageDirect(&mMessageInfo, name);
    } else {
        updateMessage();
    }

    if (mMessageInfo.isFlowTalk()) {
        forwardFlowNode();
        mStartNode = mCurrentNode;
        mFlowNode = mCurrentNode;
    }

    RecursiveHelper helper;
    helper.mIndex = 0;

    if (!MR::isValidInfo(rIter)) {
        *pCameraInf = new ActorCameraInfo(0, 0);
    } else {
        *pCameraInf = new ActorCameraInfo(rIter);
        initNodeRecursive(pMsgCtrl, rIter, *pCameraInf, &helper);
    }

    resetFlowNode();
}

void TalkNodeCtrl::resetFlowNode() {
    if (mCurrentNode != mStartNode) {
        mCurrentNode = mStartNode;
        mFlowNode = mStartNode;
        updateMessage();
    }
}

void TalkNodeCtrl::resetTempFlowNode() {
    if (mCurrentNode != mFlowNode) {
        mCurrentNode = mFlowNode;
        updateMessage();
    }
}

void TalkNodeCtrl::recordTempFlowNode() {
    mFlowNode = mCurrentNode;
}

void TalkNodeCtrl::forwardFlowNode() {
    TalkNode* node = mCurrentNode;

    if (node == nullptr) {
        return;
    }

    if (node->mNodeType == TalkNodeType_Message) {
        u32 idx = node->mNextIdx;
        MessageData* msg = MessageSystem::getSceneMessageData();
        mCurrentNode = msg->getNode(idx);
    } else if (node->mNodeType == TalkNodeType_Event) {
        u32 idx = node->mIndex;
        MessageData* msg = MessageSystem::getSceneMessageData();
        mCurrentNode = msg->getBranchNode(idx);
    }

    updateMessage();
}

bool TalkNodeCtrl::isExistNextNode() const {
    return getNextNode() != nullptr;
}

bool TalkNodeCtrl::isNextNodeMessage() const {
    TalkNode* next = getNextNode();

    if (next == nullptr || next->mNodeType != TalkNodeType_Message) {
        return false;
    }

    return true;
}

bool TalkNodeCtrl::isCurrentNodeEvent() const {
    return getCurrentNodeEvent() != nullptr;
}

TalkNode* TalkNodeCtrl::getNextNode() const {
    TalkNode* node = mCurrentNode;
    u32 idx;

    if (node == nullptr) {
        return nullptr;
    }

    if (node->mNodeType == TalkNodeType_Message) {
        idx = node->mNextIdx;
        if (idx != MessageData::cNoNode) {
            return MessageSystem::getSceneMessageData()->getNode(idx);
        }
    } else if (node->mNodeType == TalkNodeType_Event) {
        idx = node->mIndex;
        MessageData* msg = MessageSystem::getSceneMessageData();

        if (msg->isValidBranchNode(idx)) {
            idx = node->mIndex;
            MessageData* msg = MessageSystem::getSceneMessageData();
            return msg->getBranchNode(idx);
        }
    }

    return nullptr;
}

TalkNode* TalkNodeCtrl::getNextNodeBranch() const {
    TalkNode* node = getNextNode();

    if (node == nullptr) {
        return nullptr;
    }

    return node->mNodeType != TalkNodeType_Branch ? nullptr : node;
}

TalkNode* TalkNodeCtrl::getCurrentNodeBranch() const {
    if (mCurrentNode != nullptr && mCurrentNode->mNodeType == TalkNodeType_Branch) {
        return mCurrentNode;
    }

    return nullptr;
}

TalkNode* TalkNodeCtrl::getCurrentNodeMessage() const {
    if (mCurrentNode != nullptr && mCurrentNode->mNodeType == TalkNodeType_Message) {
        return mCurrentNode;
    }

    return nullptr;
}

TalkNode* TalkNodeCtrl::getCurrentNodeEvent() const {
    if (mCurrentNode != nullptr && mCurrentNode->mNodeType == TalkNodeType_Event) {
        return mCurrentNode;
    }

    return nullptr;
}

TalkNode* TalkNodeCtrl::getNextNodeEvent() const {
    TalkNode* node = getNextNode();

    if (node == nullptr) {
        return nullptr;
    }

    return node->mNodeType != TalkNodeType_Event ? nullptr : node;
}

void TalkNodeCtrl::updateMessage() {
    TalkNode* cur = mCurrentNode;

    if (cur != nullptr) {
        if (cur->mNodeType != TalkNodeType_Message) {
            mMessageInfo.mMessage = nullptr;
        } else {
            MessageSystem::getSceneMessageData()->getMessage(&mMessageInfo, cur->mIndex);
            mCurrentNodeIdx = cur->mIndex;

            TalkNode* nextBranchNode = getNextNodeBranch();

            if (nextBranchNode != nullptr) {
                mNodeData = nextBranchNode->mIndex;
            } else {
                mNodeData = -1;
            }
        }
    }

    if (mMessageInfo.isEventTalk()) {
        if (mHistory.search(mCurrentNodeIdx)) {
            mMessageInfo.mTalkType = 0;
        }

        s8 msgId = mMessageInfo.mAlreadyReadId;

        if (msgId != -1) {
            if (MR::isOnMessageAlreadyRead(msgId)) {
                mMessageInfo.mTalkType = 0;
            }
        }
    }
}

void TalkNodeCtrl::readMessage() {
    if (mMessageInfo.isEventTalk()) {
        if (!mHistory.search(mCurrentNodeIdx)) {
            mHistory.entry(mCurrentNodeIdx);
        }

        mMessageInfo.mTalkType = 0;
    }

    if (mMessageInfo.mAlreadyReadId != -1) {
        MR::onMessageAlreadyRead(mMessageInfo.mAlreadyReadId);
    }
}

const char16_t* TalkNodeCtrl::getSubMessage() const {
    const char16_t* pMessage = mMessageInfo.mMessage;

    if (*pMessage != cMessageTagEscape) {
        return nullptr;
    }

    const MessageEditorMessageTag messageTag(pMessage + 1);

    if (!messageTag.isGroupTagId(MessageTagGroup_SubMessage, 0)) {
        return nullptr;
    }

    TalkMessageInfo subInfo;

    if (MessageSystem::getSceneMessageData()->getMessage(&subInfo, messageTag.getParam32(0))) {
        return subInfo.mMessage;
    }

    return nullptr;
}

void TalkNodeCtrl::forwardCurrentBranchNode(bool storeCurrent) {
    TalkNode* cur = mCurrentNode;

    if (storeCurrent) {
        u32 group = cur->mNextGroup;
        MessageData* msg = MessageSystem::getSceneMessageData();
        mCurrentNode = msg->getBranchNode(group);
    } else {
        u32 group = cur->mNextGroup;
        MessageData* msg = MessageSystem::getSceneMessageData();
        mCurrentNode = msg->getBranchNode(group + 1);
    }

    updateMessage();
}

void TalkNodeCtrl::createFlowNode(TalkMessageCtrl* pMsgCtrl, const TableIter& rIter, const char* pName, ActorCameraInfo** pCameraInf) {
    MR::FormattedName buf;
    s32 msgID = MR::getMessageID(rIter);
    const char* zoneName = MR::getCurrentPlacementZoneName();
    buf.format("%s_%s%03d", zoneName, pName, msgID);
    createFlowNodeDirect(pMsgCtrl, rIter, buf.c_str(), pCameraInf);
}

bool RecursiveHelper::hasNode(const TalkNode* pNode) const {
    for (int i = 0; i < mIndex; i++) {
        if (mStack[i] == pNode) {
            return true;
        }
    }

    return false;
}

void TalkNodeCtrl::initNodeRecursive(TalkMessageCtrl* pMsgCtrl, const TableIter& rIter, ActorCameraInfo* pCameraInf, RecursiveHelper* pHelper) {
    TalkNode* currentNode = mCurrentNode;

    if (pHelper->hasNode(currentNode)) {
        return;
    }

    if (pHelper->mIndex == pHelper->mStack.size()) {
        pHelper->mStack.grow();
    }

    pHelper->mStack[pHelper->mIndex++] = currentNode;

    if (mMessageInfo.isCameraEvent()) {
        pCameraInf->mCameraSetID = mMessageInfo.mCameraSetID;
        MR::initMultiActorCamera(pMsgCtrl->mHostActor, rIter, &pCameraInf, "会話");
    }

    TalkNode* nodeEvent = getCurrentNodeEvent();
    if (nodeEvent != nullptr) {
        if (nodeEvent->mGroupID == EventKind_SwitchA) {
            MR::needStageSwitchWriteA(pMsgCtrl->mHostActor, rIter);
        } else if (nodeEvent->mGroupID == EventKind_SwitchB) {
            MR::needStageSwitchWriteB(pMsgCtrl->mHostActor, rIter);
        }
    }

    TalkNode* nodeBranch = getCurrentNodeBranch();
    if (nodeBranch != nullptr) {
        if (nodeBranch->mIndex == BranchKind_SwitchA) {
            MR::needStageSwitchReadA(pMsgCtrl->mHostActor, rIter);
        } else if (nodeBranch->mIndex == BranchKind_SwitchB) {
            MR::needStageSwitchReadB(pMsgCtrl->mHostActor, rIter);
        }
    }

    TalkNode* nextNodeBranch = getNextNodeBranch();
    if (nextNodeBranch != nullptr || getCurrentNodeBranch() != nullptr) {
        if (getNextNodeBranch() != nullptr) {
            forwardFlowNode();
        }
        TalkNode* rememberNode = mCurrentNode;
        forwardCurrentBranchNode(true);
        initNodeRecursive(pMsgCtrl, rIter, pCameraInf, pHelper);
        mCurrentNode = rememberNode;
        forwardCurrentBranchNode(false);
        initNodeRecursive(pMsgCtrl, rIter, pCameraInf, pHelper);
        return;
    }

    TalkNode* nextNodeEvent = getNextNodeEvent();
    if (nextNodeEvent == nullptr && !isNextNodeMessage()) {
        return;
    }

    forwardFlowNode();
    initNodeRecursive(pMsgCtrl, rIter, pCameraInf, pHelper);
}
