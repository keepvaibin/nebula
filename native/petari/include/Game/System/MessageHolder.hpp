#pragma once


class Table;
class TalkMessageInfo;
class TalkNode;

// A message file as the importer cooks it (crates/nebula-import cook/message.rs, NMSG): counts and offsets from the
// file's start. See MessageEditorMessageTag for the tags in the text.
struct MessageFileHeader {
    char mMagic[4];  // "NMSG"
    u32 mMessageNum;
    u32 mMessageOffset;
    u32 mTextNum;  // UTF-16 units
    u32 mTextOffset;
    u32 mNodeNum;
    u32 mNodeOffset;
    u32 mBranchNum;
    u32 mBranchOffset;
    u32 mReserved[3];
};

struct MessageRecord {
    u32 mText;  // the first unit
    u16 mCameraSetID;
    s8 mSoundId;
    u8 mCameraType;
    u8 mTalkType;
    u8 mBalloonType;
    s8 mAreaId;
    s8 mAlreadyReadId;
};

class MessageData {
public:
    // A flow node's next (a message's) or a branch with none.
    static const u32 cNoNode = 0xFFFFFFFFu;

    MessageData(const char*);

    bool getMessageDirect(TalkMessageInfo*, const char*) const;
    // False for an index past the messages.
    bool getMessage(TalkMessageInfo*, u32 index) const;
    TalkNode* findNode(const char*) const;
    TalkNode* getNode(u32) const;
    TalkNode* getBranchNode(u32) const;
    bool isValidBranchNode(u32) const;
    s32 findMessageIndex(const char*) const;
    // The id of the message of that number, or null: a walk of the id table, for a rare lookup.
    const char* findMessageName(u32) const;

    u32 getMessageNum() const {
        return mFile->mMessageNum;
    }

    Table* mIDTable;
    const MessageFileHeader* mFile;
    const MessageRecord* mMessages;
    const char16_t* mText;
    TalkNode* mNodes;
    const u32* mBranches;
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
    static bool getSystemMessageDirect(TalkMessageInfo*, const char*);
    static bool getGameMessageDirect(TalkMessageInfo*, const char*);
    static bool getLayoutMessageDirect(TalkMessageInfo*, const char*);
    static MessageData* getSceneMessageData();
};
