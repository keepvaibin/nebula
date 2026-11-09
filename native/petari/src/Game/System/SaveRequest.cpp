#include "Game/System/SaveRequest.hpp"

#include <nebula_game_api.h>

extern "C" const NebulaHostServices* nebula_host(void);

SaveRequest::SaveRequest() {
    init();
}

void SaveRequest::init() {
    mType = Type_None;
    mName = "";
    mBuffer = nullptr;
    mSize = 0;
    mReadLength = nullptr;
    mStatus = NEBULA_SAVE_DONE;
}

bool SaveRequest::isDone() const {
    return mStatus != NEBULA_SAVE_PENDING;
}

SaveRequest::Result SaveRequest::getResult() const {
    switch (static_cast< s32 >(mStatus)) {
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
    static const u32 cOps[] = {0, NEBULA_SAVE_WRITE, NEBULA_SAVE_READ};
    if (mType == Type_None) {
        return;
    }
    // NebulaAtomic< s32 > is standard-layout with its value as the only member: the host stores into that value.
    static_assert(sizeof(NebulaAtomic< s32 >) == sizeof(int32_t), "the host's status is the atomic's value");
    const NebulaSaveRequest request = {cOps[mType], mName, mBuffer, mSize, mReadLength, reinterpret_cast< int32_t* >(&mStatus)};
    const NebulaHostServices* const host = nebula_host();
    host->save_request(host->user, &request);
}

const char* SaveRequest::setWriteSeq(const char* pName, const void* pBuf, u32 size) {
    init();
    mType = Type_Write;
    mBuffer = const_cast< void* >(pBuf);
    mSize = size;
    mName = pName;
    return mName;
}

const char* SaveRequest::setReadSeq(const char* pName, void* pBuf, u32 capacity, u32* pLength) {
    init();
    mType = Type_Read;
    mBuffer = pBuf;
    mSize = capacity;
    mReadLength = pLength;
    mName = pName;
    return mName;
}
