#include "Game/System/SaveRequest.hpp"
#include <cstdio>

#include <nebula_game_api.h>

extern "C" const NebulaHostServices* nebula_host(void);

SaveRequest::SaveRequest() {
    init();
}

void SaveRequest::init() {
    mType = Type_None;
    mName[0] = '\0';
    mBuffer = nullptr;
    mSize = 0;
    mReadLength = nullptr;
    mStatus = NEBULA_SAVE_DONE;
}

bool SaveRequest::isDone() const {
    return __atomic_load_n(&mStatus, __ATOMIC_ACQUIRE) != NEBULA_SAVE_PENDING;
}

SaveRequest::Result SaveRequest::getResult() const {
    switch (__atomic_load_n(&mStatus, __ATOMIC_ACQUIRE)) {
    case NEBULA_SAVE_DONE:
        return Result_Done;
    case NEBULA_SAVE_PENDING:
        return Result_Pending;
    case NEBULA_SAVE_NO_FILE:
        return Result_NoFile;
    case NEBULA_SAVE_TOO_LARGE:
        return Result_Corrupted;
    default:
        return Result_Failed;
    }
}

void SaveRequest::start() {
    static const u32 cOps[] = {0, NEBULA_SAVE_DELETE, NEBULA_SAVE_WRITE, NEBULA_SAVE_READ};
    if (mType == Type_None) {
        return;
    }
    const NebulaSaveRequest request = {cOps[mType], mName, mBuffer, mSize, mReadLength, &mStatus};
    const NebulaHostServices* const host = nebula_host();
    host->save_request(host->user, &request);
}

const char* SaveRequest::setWriteSeq(const char* pName, const void* pBuf, u32 size) {
    init();
    mType = Type_Write;
    mBuffer = const_cast< void* >(pBuf);
    mSize = size;
    snprintf(mName, sizeof(mName), "%s", pName);
    return mName;
}

const char* SaveRequest::setReadSeq(const char* pName, void* pBuf, u32 capacity, u32* pLength) {
    init();
    mType = Type_Read;
    mBuffer = pBuf;
    mSize = capacity;
    mReadLength = pLength;
    snprintf(mName, sizeof(mName), "%s", pName);
    return mName;
}

SaveRequest* SaveRequest::setDelete(const char* pName) {
    init();
    mType = Type_Delete;
    snprintf(mName, sizeof(mName), "%s", pName);
    return this;
}
