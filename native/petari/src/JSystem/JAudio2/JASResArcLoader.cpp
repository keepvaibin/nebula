#include "JSystem/JAudio2/JASResArcLoader.hpp"
#include "Game/System/GameThreads.hpp"
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
}

JASResArcLoader::TLoadResInfo::TLoadResInfo(AssetFolder* archive, u16 id, void* buffer, u32 size)
    : mArchive(archive), mResID(id), mBuffer(buffer), mBufferSize(size), mCallback(nullptr), mCallbackArg(0) {
}

int JASResArcLoader::loadResourceAsync(AssetFolder* archive, u16 id, u8* buffer, u32 size, LoadCallback callback, uintptr_t callbackArg) {
    TLoadResInfo args(archive, id, buffer, size);
    args.mCallback = callback;
    args.mCallbackArg = callbackArg;

    loadResourceCallback(&args);
    return 1;
}
