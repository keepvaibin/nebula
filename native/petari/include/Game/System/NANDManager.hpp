#pragma once

#include <revolution.h>

class NANDRequestInfo;

// One request on the game's save files: set up by one of the set* calls, started by MR::addRequestToNANDManager, done
// when the host has finished it (the game polls isDone). On the console a save thread ran each as NAND calls; the
// host's save service is asynchronous itself (save_request).
class NANDRequestInfo {
public:
    NANDRequestInfo();

    void init();
    bool isDone() const;
    // The result the save sequences interpret (NAND_RESULT_*), once done.
    s32 getResult() const;
    void start();
    const char* setWriteSeq(const char*, const void*, u32, u8, u8);
    const char* setReadSeq(const char*, void*, u32, u32*);
    NANDRequestInfo* setCheck(u32, u32, u32*);
    NANDRequestInfo* setDelete(const char*);

private:
    enum Type {
        Type_Check,
        Type_Delete,
        Type_Write,
        Type_Read,
    };

    char mName[64];
    Type mType;
    void* mBuffer;
    u32 mSize;           // a write's length, a read's capacity
    u32* mReadLength;    // a read: the file's length
    u32* mCheckAnswer;   // a check: 0, room to create the save
    volatile s32 mStatus;  // NEBULA_SAVE_*: the host writes it from its own thread
};

class NANDResultCode {
public:
    NANDResultCode(s32 code) : mCode(code) {
    }

    s32 getCode() const;
    bool isSuccess() const;
    bool isSaveDataCorrupted() const;
    bool isNANDCorrupted() const;
    bool isMaxBlocks() const;
    bool isMaxFiles() const;
    bool isNoExistFile() const;
    bool isBusyOrAllocFailed() const;
    bool isUnknown() const;

private:
    /* 0x0 */ s32 mCode;
};

namespace MR {
    void addRequestToNANDManager(NANDRequestInfo*);
};  // namespace MR
