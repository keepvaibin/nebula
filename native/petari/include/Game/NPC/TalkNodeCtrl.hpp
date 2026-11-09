#pragma once

#include "Game/NPC/TalkMessageCtrl.hpp"
#include "Game/Util/Array.hpp"
#include "Game/NPC/TalkMessageInfo.hpp"

class TalkMessageHistory {
public:
    void entry(u32);
    bool search(u32) const;

    u32* mHistory;
    s32 mCount;
    s32 mMax;
};

enum TalkNodeType {
    TalkNodeType_Message = 1,
    TalkNodeType_Branch = 2,
    TalkNodeType_Event = 3,
};

// A branch node's condition (TalkNode::mIndex). The flow data numbers them in this order: the galaxy result comes
// before the time-keep demo and the read-message test.
enum BranchKind {
    // The player chooses; mNextIdx is the select's id (sSelectBranchID).
    BranchKind_Select,
    BranchKind_Func,
    BranchKind_NearPlayer,
    BranchKind_SwitchA,
    BranchKind_SwitchB,
    BranchKind_PlayerNormal,
    BranchKind_PlayerBee,
    BranchKind_PlayerTeresa,
    BranchKind_PowerStarAppeared,
    BranchKind_AlreadyDone,
    BranchKind_PlayerLuigi,
    BranchKind_GalaxyResult,
    BranchKind_TimeKeepDemo,
    BranchKind_MessageRead,
};

// An event node's kind (TalkNode::mGroupID).
enum EventKind {
    EventKind_Func,
    EventKind_FuncStop,
    EventKind_None,
    EventKind_Skip,
    EventKind_Anime,
    EventKind_SwitchA,
    EventKind_SwitchB,
    EventKind_Kill,
};

// A flow node, read in place (NMSG, crates/nebula-import cook/message.rs).
class TalkNode {
public:
    u8 mNodeType;  // TalkNodeType
    u8 mGroupID;   // an event's kind (EventKind)
    u32 mIndex;  // a message's number, a branch's condition, an event's first branch
    union {
        u32 mNextIdx;  // a message's next node (MessageData::cNoNode at the end), a branch's parameter
        u32 mArg;      // an event's value
    };
    u32 mNextGroup;  // a message's group, a branch's first branch
};
static_assert(sizeof(TalkNode) == 16 && __builtin_offsetof(TalkNode, mIndex) == 4, "NMSG node layout");

class RecursiveHelper {
public:
    inline bool hasNode(const TalkNode*) const;
    MR::InlineArray< TalkNode*, 64 > mStack;
    s32 mIndex;
};

class TalkNodeCtrl {
public:
    TalkNodeCtrl();

    void createFlowNodeDirect(TalkMessageCtrl*, const TableIter&, const char*, ActorCameraInfo**);
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
    void createFlowNode(TalkMessageCtrl*, const TableIter&, const char*, ActorCameraInfo**);
    void initNodeRecursive(TalkMessageCtrl*, const TableIter&, ActorCameraInfo*, RecursiveHelper*);

    char* mFlowName;
    s32 mCurrentNodeIdx;
    TalkMessageInfo mMessageInfo;
    TalkMessageHistory mHistory;
    TalkNode* mStartNode;
    TalkNode* mCurrentNode;
    TalkNode* mFlowNode;
    s16 mNodeData;
};
