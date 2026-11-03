#pragma once


#include "Game/System/FileHolder.hpp"
#include "Game/System/GameThread.hpp"

struct RequestFileInfo {
    u32 _0;
    s32 mRequestType;
    char mFileName[0x80];
    FileHolderFileEntry* mFileEntry;
};

class FileLoaderThread : public GameThread {
public:
    FileLoaderThread(int, NebulaArena*);

    virtual void* run();

    void loadToMainRAM(RequestFileInfo*);
    void mountArchiveAndStartCreateResource(RequestFileInfo*);
};
