#pragma once

#include "Game/System/GameAssets.hpp"

#include "Game/System/ArchiveHolder.hpp"
#include "Game/System/GameThreads.hpp"
#include "Game/System/FileHolder.hpp"
#include "Game/System/FileLoaderThread.hpp"

#include "Game/System/NebulaArena.hpp"
#include <cstdio>
#include <NebulaGpu.h>
#include <nebula_math.h>

class AssetFolder;

class FileLoader {
public:
    FileLoader();

    void requestLoadToMainRAM(const char*, u8*, NebulaArena*, GameAssets::AllocDirection, bool);
    void requestMountArchive(const char*, NebulaArena*, bool);
    bool isLoaded(const char*) const;
    void* receiveFile(const char*);
    AssetFolder* receiveArchive(const char*);
    void receiveAllRequestedFile();
    AssetFolder* createAndAddArchive(AssetFolder*, NebulaArena*, const char*);
    void getMountedArchiveAndHeap(const char*, AssetFolder**, NebulaArena**) const;
    void clearRequestFileInfo(bool);
    void removeHolderIfIsEqualHeap(NebulaArena*);
    void removeFile(const char*);
    bool isNeedToLoad(const char*) const;
    const RequestFileInfo* getRequestFileInfoConst(const char*) const;
    RequestFileInfo* addRequest(const char*, u32, u32, FileHolderFileEntry*);

    FileLoaderThread* mLoaderThread;
    GameMutex mMutex;
    RequestFileInfo* mRequestFileInfos;
    s32 mRequestedFileCount;
    FileHolder* mFileHolder;
    ArchiveHolder* mArchiveHolder;
};
