#pragma once


class JMapInfo;
class TalkMessageInfo;
class TalkNode;

// BMG, in host order as the importer cooks it (crates/nebula-import cook/native_data.rs); see
// MessageEditorMessageTag for the tags.
struct MessageInfoBlock {
    u32 mMagic;
    u32 mBlockSize;
    u16 mItemCount;
    u16 mItemSize;
    u32 _C;
};

struct MessageDataBlock {
    u32 mMagic;
    u32 mBlockSize;
};

struct MessageFlowBlock {
    u32 mMagic;
    u32 mBlockSize;
    u16 mNodeCount;
    u16 _A;
    u32 _C;
};

struct MessageFLI1Block {
    u32 mMagic;
    u32 mBlockSize;
};

class MessageData {
public:
    MessageData(const char*);

    bool getMessageDirect(TalkMessageInfo*, const char*) const;
    bool getMessage(TalkMessageInfo*, u16, u16) const;
    TalkNode* findNode(const char*) const;
    TalkNode* getNode(u32) const;
    TalkNode* getBranchNode(u32) const;
    bool isValidBranchNode(u32) const;
    u8* getMessageInfoTool(int) const;
    s32 findMessageIndex(const char*) const;

    JMapInfo* mIDTable;
    MessageInfoBlock* mInfoBlock;
    MessageDataBlock* mDataBlock;
    u32 _C;
    MessageFlowBlock* mFlowBlock;
    u16* _14;
    u8* _18;
    MessageFLI1Block* mFLI1Block;
};

class MessageHolder {
public:
    MessageHolder();

    void initSceneData();
    void destroySceneData();
    void initSystemData();
    void initGameData();

    MessageData* mSystemMessageData;
    MessageData* mGameMessageData;
    MessageData* mSceneMessageData;
};

class MessageSystem {
public:
    class Node {};

    struct FlowNodeBranch {};

    struct FlowNodeEvent {
        u8 mFlowType;
        u8 mEventType;
        u16 mBranchID;
        u32 mArg;
    };

    static bool getSystemMessageDirect(TalkMessageInfo*, const char*);
    static bool getGameMessageDirect(TalkMessageInfo*, const char*);
    static bool getLayoutMessageDirect(TalkMessageInfo*, const char*);
    static MessageData* getSceneMessageData();
};
