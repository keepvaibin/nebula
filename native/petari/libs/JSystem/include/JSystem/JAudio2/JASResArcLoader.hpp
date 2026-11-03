#pragma once

#include "Game/System/GameThreads.hpp"

class AssetFolder;

namespace JASResArcLoader {

    typedef void (*LoadCallback)(u32, uintptr_t);

    struct TLoadResInfo {
        TLoadResInfo(AssetFolder* archive, u16 id, void* buffer, u32 size);

        AssetFolder* mArchive;
        u16 mResID;
        void* mBuffer;
        u32 mBufferSize;
        LoadCallback mCallback;
        uintptr_t mCallbackArg;
        GameMessageQueue* mMsgQueue;
    };

    u32 getResSize(AssetFolder const*, u16);
    void loadResourceCallback(void*);
    int loadResourceAsync(AssetFolder*, u16, u8*, u32, void (*)(u32, uintptr_t), uintptr_t);
}  // namespace JASResArcLoader

enum ResArcMessage {
    RESARC_MESSAGE_ERROR = -1,
    RESARC_MESSAGE_SUCCESS = 0,
};
