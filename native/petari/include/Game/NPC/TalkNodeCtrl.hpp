#pragma once

#include "Game/NPC/TalkMessageCtrl.hpp"
#include "Game/Util/Array.hpp"
#include "Game/NPC/TalkMessageInfo.hpp"

class TalkMessageHistory {
public:
    void entry(u16);
    bool search(u16) const;

    u16* mHistory;
    s32 mCount;
    s32 mMax;
};

class TalkNode {
public:
    // A BMG FLW1 node, read in place (host order, as the importer cooks it).
    u8 mNodeType;
    u8 mGroupID;
    u16 mIndex;
    union {
        u32 mUnknown;
        struct {
            u16 mNextIdx;
            u16 mNextGroup;
        };
    };
};

class RecursiveHelper {
public:
    inline bool hasNode(const TalkNode*) const;
    MR::FixedArray< TalkNode*, 64 > mStack;
    s32 mIndex;
};

class MessageNode {
public:
    const char* mMessage;
};

class TalkNodeCtrl {
public:
    TalkNodeCtrl();

    void createFlowNodeDirect(TalkMessageCtrl*, const JMapInfoIter&, const char*, ActorCameraInfo**);
    void resetFlowNode();
    void resetTempFlowNode();
    void recordTempFlowNode();
    void forwardFlowNode();
    bool isExistNextNode() const;
    bool isNextNodeMessage() const;
    bool isCurrentNodeEvent() const;
    TalkNode* getNextNode() const;
    TalkNode* getNextNodeBranch() const;
    TalkNode* getCurrentNodeBranch() const;
    TalkNode* getCurrentNodeMessage() const;
    TalkNode* getCurrentNodeEvent() const;
    TalkNode* getNextNodeEvent() const;
    void updateMessage();
    void readMessage();
    const char16_t* getSubMessage() const;
    void forwardCurrentBranchNode(bool);
    void createFlowNode(TalkMessageCtrl*, const JMapInfoIter&, const char*, ActorCameraInfo**);
    void initNodeRecursive(TalkMessageCtrl*, const JMapInfoIter&, ActorCameraInfo*, RecursiveHelper*);

    char* _0;
    s32 mCurrentNodeIdx;
    TalkMessageInfo mMessageInfo;
    TalkMessageHistory mHistory;
    TalkNode* _38;
    TalkNode* mCurrentNode;
    TalkNode* mFlowNode;
    s16 mNodeData;
};
