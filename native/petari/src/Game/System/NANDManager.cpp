#include "Game/System/NANDManager.hpp"
#include <cstdio>

#include <nebula_game_api.h>

extern "C" const NebulaHostServices* nebula_host(void);

NANDRequestInfo::NANDRequestInfo() {
    init();
}

void NANDRequestInfo::init() {
    mType = Type_Check;
    mName[0] = '\0';
    mBuffer = nullptr;
    mSize = 0;
    mReadLength = nullptr;
    mCheckAnswer = nullptr;
    mStatus = NEBULA_SAVE_DONE;
}

bool NANDRequestInfo::isDone() const {
    return __atomic_load_n(&mStatus, __ATOMIC_ACQUIRE) != NEBULA_SAVE_PENDING;
}

s32 NANDRequestInfo::getResult() const {
    switch (__atomic_load_n(&mStatus, __ATOMIC_ACQUIRE)) {
    case NEBULA_SAVE_DONE:
        return NAND_RESULT_OK;
    case NEBULA_SAVE_PENDING:
        return NAND_RESULT_BUSY;
    case NEBULA_SAVE_NO_FILE:
        return NAND_RESULT_NOEXISTS;
    case NEBULA_SAVE_TOO_LARGE:
        return NAND_RESULT_AUTHENTICATION;
    default:
        return NAND_RESULT_ACCESS;
    }
}

void NANDRequestInfo::start() {
    if (mType == Type_Check) {
        // Room to create the save: a host's disk has it.
        *mCheckAnswer = 0;
        mStatus = NEBULA_SAVE_DONE;
        return;
    }

    static const u32 cOps[] = {0, NEBULA_SAVE_DELETE, NEBULA_SAVE_WRITE, NEBULA_SAVE_READ};
    const NebulaSaveRequest request = {cOps[mType], mName, mBuffer, mSize, mReadLength, &mStatus};
    const NebulaHostServices* const host = nebula_host();
    host->save_request(host->user, &request);
}

const char* NANDRequestInfo::setWriteSeq(const char* pName, const void* pBuf, u32 size, u8 permission, u8 attr) {
    init();
    mType = Type_Write;
    mBuffer = const_cast< void* >(pBuf);
    mSize = size;
    snprintf(mName, sizeof(mName), "%s", pName);
    return mName;
}

const char* NANDRequestInfo::setReadSeq(const char* pName, void* pBuf, u32 capacity, u32* pLength) {
    init();
    mType = Type_Read;
    mBuffer = pBuf;
    mSize = capacity;
    mReadLength = pLength;
    snprintf(mName, sizeof(mName), "%s", pName);
    return mName;
}

NANDRequestInfo* NANDRequestInfo::setCheck(u32 fsBlock, u32 inode, u32* pAnswer) {
    init();
    mType = Type_Check;
    mCheckAnswer = pAnswer;
    return this;
}

NANDRequestInfo* NANDRequestInfo::setDelete(const char* pName) {
    init();
    mType = Type_Delete;
    snprintf(mName, sizeof(mName), "%s", pName);
    return this;
}

s32 NANDResultCode::getCode() const {
    return mCode;
}

bool NANDResultCode::isSuccess() const {
    return getCode() == NAND_RESULT_OK;
}

bool NANDResultCode::isSaveDataCorrupted() const {
    return getCode() == NAND_RESULT_ECC_CRIT || getCode() == NAND_RESULT_AUTHENTICATION;
}

bool NANDResultCode::isNANDCorrupted() const {
    return getCode() == NAND_RESULT_CORRUPT;
}

bool NANDResultCode::isMaxBlocks() const {
    return getCode() == NAND_RESULT_MAXBLOCKS;
}

bool NANDResultCode::isMaxFiles() const {
    return getCode() == NAND_RESULT_MAXFILES;
}

bool NANDResultCode::isNoExistFile() const {
    return getCode() == NAND_RESULT_NOEXISTS;
}

bool NANDResultCode::isBusyOrAllocFailed() const {
    return getCode() == NAND_RESULT_BUSY || getCode() == NAND_RESULT_ALLOC_FAILED;
}

bool NANDResultCode::isUnknown() const {
    return getCode() == NAND_RESULT_UNKNOWN;
}

namespace MR {
    void addRequestToNANDManager(NANDRequestInfo* pRequestInfo) {
        pRequestInfo->start();
    }
};  // namespace MR

