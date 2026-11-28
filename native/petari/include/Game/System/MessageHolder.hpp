#pragma once


class Table;
class TalkMessageInfo;
class TalkNode;

// A message file as the importer cooks it (crates/nebula-import cook/message.rs, NMSG): counts and offsets from the
// file's start. See MessageEditorMessageTag for the tags in the text (version 1), MessageRunFile for the runs (version 2).
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
    u32 mVersion;  // cMessageFileVersion
    u32 mRunsOffset;
    u32 mReserved;
};

// The file's version: 2, the runs beside the text of version 1.
static const u32 cMessageFileVersion = 2;

// Version 2: each message parsed once by the cook into runs, text runs of code points and typed tag records, with its
// plain text (its text runs one after another) apart. Counts and offsets from the file's start.
struct MessageRunsHeader {
    u32 mListNum;  // one a message
    u32 mListOffset;
    u32 mRunNum;
    u32 mRunOffset;
    u32 mTagNum;
    u32 mTagOffset;
    u32 mCodeNum;  // code points
    u32 mCodeOffset;
    u32 mParamNum;  // a tag's parameter units
    u32 mParamOffset;
};

// A message's runs and its plain text.
struct MessageRunList {
    u32 mFirstRun;
    u32 mRunNum;
    u32 mPlainText;  // the first code point; a 0 follows the last
    u32 mPlainNum;
};

enum MessageRunKind {
    MessageRunKind_Text = 0,  // mFirst: the first code point, mCount of them
    MessageRunKind_Tag = 1,   // mFirst: the tag record, mCount 1
};

struct MessageRun {
    u32 mKind;
    u32 mFirst;
    u32 mCount;
};

// A tag: its group and tag, its parameter units (as the game reads them) and the string it names (a sound's name, the
// number font's text, a ruby's reading), empty for the others.
struct MessageTagRecord {
    u8 mGroup;
    u8 mReserved;
    u16 mTag;
    u32 mParam;
    u32 mParamNum;
    u32 mString;  // the first code point; a 0 follows the last
    u32 mStringNum;
};

static_assert(sizeof(MessageFileHeader) == 48 && sizeof(MessageRunsHeader) == 40 && sizeof(MessageRunList) == 16 &&
                  sizeof(MessageRun) == 12 && sizeof(MessageTagRecord) == 20,
              "NMSG layout");

// One message's runs, as MessageRunFile hands them.
struct MessageRuns {
    const MessageRun* mRuns;
    u32 mRunNum;
    const char32_t* mPlainText;  // ends in a 0
    u32 mPlainNum;
};

// The runs of a message file (version 2), read in place. attach checks every count, offset and index once against
// the file's size and the arrays; the getters then need no check.
class MessageRunFile {
public:
    MessageRunFile() : mHeader(nullptr), mLists(nullptr), mRuns(nullptr), mTags(nullptr), mCodes(nullptr), mParams(nullptr) {
    }

    // False for a file that is not version 2 or whose runs are not whole and inside it (nothing is attached then).
    bool attach(const u8* pFile, u32 size);

    u32 getMessageNum() const {
        return mHeader != nullptr ? mHeader->mListNum : 0;
    }
    // False for an index past the messages.
    bool getRuns(u32 index, MessageRuns* pRuns) const;
    // A text run's code points (rRun.mCount of them).
    const char32_t* getText(const MessageRun& rRun) const {
        return mCodes + rRun.mFirst;
    }
    const MessageTagRecord& getTag(const MessageRun& rRun) const {
        return mTags[rRun.mFirst];
    }
    const u16* getParams(const MessageTagRecord& rTag) const {
        return mParams + rTag.mParam;
    }
    // The tag's string, rTag.mStringNum code points and a 0.
    const char32_t* getString(const MessageTagRecord& rTag) const {
        return mCodes + rTag.mString;
    }
    // The arrays the runs index (TextRuns.hpp makes a message's text in runs of them).
    const char32_t* getCodes() const {
        return mCodes;
    }
    const MessageTagRecord* getTags() const {
        return mTags;
    }
    const u16* getParamUnits() const {
        return mParams;
    }

private:
    const MessageRunsHeader* mHeader;
    const MessageRunList* mLists;
    const MessageRun* mRuns;
    const MessageTagRecord* mTags;
    const char32_t* mCodes;
    const u16* mParams;
};

inline bool MessageRunFile::attach(const u8* pFile, u32 size) {
    *this = MessageRunFile();
    // An array of num items of that size and alignment at offset, inside the file.
    const auto inside = [size](u32 offset, u32 num, u64 itemSize, u32 align) {
        return offset % align == 0 && static_cast< u64 >(offset) + static_cast< u64 >(num) * itemSize <= size;
    };
    if (pFile == nullptr || size < sizeof(MessageFileHeader)) {
        return false;
    }
    const MessageFileHeader* pFileHeader = reinterpret_cast< const MessageFileHeader* >(pFile);
    if (pFileHeader->mVersion != cMessageFileVersion || !inside(pFileHeader->mRunsOffset, 1, sizeof(MessageRunsHeader), 4)) {
        return false;
    }
    const MessageRunsHeader* pHeader = reinterpret_cast< const MessageRunsHeader* >(pFile + pFileHeader->mRunsOffset);
    if (pHeader->mListNum != pFileHeader->mMessageNum || !inside(pHeader->mListOffset, pHeader->mListNum, sizeof(MessageRunList), 4) ||
        !inside(pHeader->mRunOffset, pHeader->mRunNum, sizeof(MessageRun), 4) ||
        !inside(pHeader->mTagOffset, pHeader->mTagNum, sizeof(MessageTagRecord), 4) ||
        !inside(pHeader->mCodeOffset, pHeader->mCodeNum, sizeof(char32_t), 4) ||
        !inside(pHeader->mParamOffset, pHeader->mParamNum, sizeof(u16), 2)) {
        return false;
    }
    const MessageRunList* pLists = reinterpret_cast< const MessageRunList* >(pFile + pHeader->mListOffset);
    const MessageRun* pRuns = reinterpret_cast< const MessageRun* >(pFile + pHeader->mRunOffset);
    const MessageTagRecord* pTags = reinterpret_cast< const MessageTagRecord* >(pFile + pHeader->mTagOffset);
    const char32_t* pCodes = reinterpret_cast< const char32_t* >(pFile + pHeader->mCodeOffset);
    // first and num inside an array of total; ended: a 0 code point follows them.
    const auto span = [](u32 first, u32 num, u32 total) { return static_cast< u64 >(first) + num <= total; };
    const auto ended = [&](u32 first, u32 num) {
        return span(first, num, pHeader->mCodeNum) && first + num < pHeader->mCodeNum && pCodes[first + num] == 0;
    };
    for (u32 i = 0; i < pHeader->mListNum; i++) {
        if (!span(pLists[i].mFirstRun, pLists[i].mRunNum, pHeader->mRunNum) || !ended(pLists[i].mPlainText, pLists[i].mPlainNum)) {
            return false;
        }
    }
    for (u32 i = 0; i < pHeader->mRunNum; i++) {
        const MessageRun& rRun = pRuns[i];
        const bool ok = rRun.mKind == MessageRunKind_Text ? span(rRun.mFirst, rRun.mCount, pHeader->mCodeNum)
                                                          : rRun.mKind == MessageRunKind_Tag && rRun.mFirst < pHeader->mTagNum && rRun.mCount == 1;
        if (!ok) {
            return false;
        }
    }
    for (u32 i = 0; i < pHeader->mTagNum; i++) {
        if (!span(pTags[i].mParam, pTags[i].mParamNum, pHeader->mParamNum) || !ended(pTags[i].mString, pTags[i].mStringNum)) {
            return false;
        }
    }
    mHeader = pHeader;
    mLists = pLists;
    mRuns = pRuns;
    mTags = pTags;
    mCodes = pCodes;
    mParams = reinterpret_cast< const u16* >(pFile + pHeader->mParamOffset);
    return true;
}

inline bool MessageRunFile::getRuns(u32 index, MessageRuns* pRuns) const {
    if (index >= getMessageNum()) {
        return false;
    }
    const MessageRunList& rList = mLists[index];
    pRuns->mRuns = mRuns + rList.mFirstRun;
    pRuns->mRunNum = rList.mRunNum;
    pRuns->mPlainText = mCodes + rList.mPlainText;
    pRuns->mPlainNum = rList.mPlainNum;
    return true;
}

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
    // The runs of the message of that number (version 2); false for an index past the messages.
    bool getMessageRuns(u32 index, MessageRuns* pRuns) const {
        return mRunFile.getRuns(index, pRuns);
    }
    const MessageRunFile& getRunFile() const {
        return mRunFile;
    }

    u32 getMessageNum() const {
        return mFile->mMessageNum;
    }

    Table* mIDTable;
    const MessageFileHeader* mFile;
    const MessageRecord* mMessages;
    const char16_t* mText;
    TalkNode* mNodes;
    const u32* mBranches;
    MessageRunFile mRunFile;
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
