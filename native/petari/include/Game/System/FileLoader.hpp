#pragma once

#include "Game/System/ArchiveHolder.hpp"
#include "Game/System/FileHolder.hpp"
#include "Game/System/GameAssets.hpp"
#include "Game/System/GameThreads.hpp"

#include "Game/System/NebulaArena.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>

class AssetFolder;

// The store's files the game has taken, by path. Taking one is done when the call returns: the store is mapped, so
// nothing waits for a drive here; a page the game touches later may.
class FileLoader {
public:
    FileLoader();

    void* loadFile(const char*, u8*, NebulaArena*);
    AssetFolder* mountArchive(const char*, NebulaArena*);
    bool isLoaded(const char*) const;
    void* getLoadedFile(const char*);
    AssetFolder* getMountedArchive(const char*);
    AssetFolder* createAndAddArchive(AssetFolder*, NebulaArena*, const char*);
    void getMountedArchiveAndHeap(const char*, AssetFolder**, NebulaArena**) const;
    void removeHolderIfIsEqualHeap(NebulaArena*);
    void removeFile(const char*);

    GameMutex mMutex;
    FileHolder* mFileHolder;
    ArchiveHolder* mArchiveHolder;
};
