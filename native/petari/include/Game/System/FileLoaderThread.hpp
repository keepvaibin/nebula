#pragma once

#include "Game/System/NebulaAtomic.hpp"

#include "Game/System/FileHolder.hpp"
#include "Game/System/GameThread.hpp"

struct RequestFileInfo {
    /* 0x00 */ u32 _0;
    /* 0x04 */ s32 mRequestType;
    /* 0x08 */ char mFileName[0x80];
    /* 0x88 */ NebulaAtomic< u32 > _88;
    /* 0x8C */ FileHolderFileEntry* mFileEntry;
};

class FileLoaderThread : public GameThread {
public:
    FileLoaderThread(int, JKRHeap*);

    virtual void* run();

    void loadToMainRAM(RequestFileInfo*);
    void mountArchiveAndStartCreateResource(RequestFileInfo*);
};
