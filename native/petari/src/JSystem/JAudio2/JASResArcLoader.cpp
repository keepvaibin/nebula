#include "JSystem/JAudio2/JASResArcLoader.hpp"
#include "Game/System/GameThreads.hpp"
#include "JSystem/JAudio2/JASLoadThread.hpp"
#include "JSystem/JAudio2/JASTaskThread.hpp"
#include "Game/System/AssetFolder.hpp"
#include <cstring>

u32 JASResArcLoader::getResSize(AssetFolder const* archive, u16 id) {
    AssetFolder::Item item;
    return archive->getItem(archive->getEntry(), id, &item) ? item.mSize : 0;
}

void JASResArcLoader::loadResourceCallback(void* args) {
    TLoadResInfo* castedArgs = (TLoadResInfo*)args;
    u32 readResult = 0;
    AssetFolder::Item item;
    if (castedArgs->mArchive->getItem(castedArgs->mArchive->getEntry(), castedArgs->mResID, &item)) {
        readResult = item.mSize < castedArgs->mBufferSize ? item.mSize : castedArgs->mBufferSize;
        memcpy(castedArgs->mBuffer, item.mData, readResult);
    }
    if (castedArgs->mCallback != nullptr) {
        castedArgs->mCallback(readResult, castedArgs->mCallbackArg);
    }

    if (readResult == 0) {
        if (castedArgs->mMsgQueue != nullptr) {
            castedArgs->mMsgQueue->send((void*)RESARC_MESSAGE_ERROR, true);
        }
    } else if (castedArgs->mMsgQueue != nullptr) {
        castedArgs->mMsgQueue->send((void*)RESARC_MESSAGE_SUCCESS, true);
    }
}

JASResArcLoader::TLoadResInfo::TLoadResInfo(AssetFolder* archive, u16 id, void* buffer, u32 size)
    : mArchive(archive), mResID(id), mBuffer(buffer), mBufferSize(size), mCallback(nullptr), mCallbackArg(0), mMsgQueue(nullptr) {
}

int JASResArcLoader::loadResourceAsync(AssetFolder* archive, u16 id, u8* buffer, u32 size, LoadCallback callback, uintptr_t callbackArg) {
    TLoadResInfo args(archive, id, buffer, size);
    args.mCallback = callback;
    args.mCallbackArg = callbackArg;

    return JASLoad::getThreadPointer()->sendCmdMsg(&loadResourceCallback, (void*)&args, sizeof(TLoadResInfo));
}
