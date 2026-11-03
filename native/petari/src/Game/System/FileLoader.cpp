#include "Game/System/FileLoader.hpp"
#include "Game/System/GameThreads.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/NebulaArena.hpp"

FileLoader::FileLoader() {
    mLoaderThread = nullptr;
    mRequestFileInfos = nullptr;
    mRequestedFileCount = 0;
    mFileHolder = nullptr;
    mArchiveHolder = nullptr;
    mLoaderThread = new FileLoaderThread(320, MR::getStationedHeap());
    GameThreads::resume(mLoaderThread->mThread);
    mMutex.init();
    mRequestFileInfos = new RequestFileInfo[0x140];
    mFileHolder = new FileHolder();
    mArchiveHolder = new ArchiveHolder();
    mRequestedFileCount = 0;
}

void FileLoader::requestLoadToMainRAM(const char* pName, u8* pData, NebulaArena* pHeap, GameAssets::AllocDirection direction, bool jam) {
    if (isNeedToLoad(pName)) {
        if (pHeap == nullptr) {
            pHeap = MR::getCurrentHeap();
        }

        RequestFileInfo* info = addRequest(pName, 0, direction, mFileHolder->add(pName, pHeap, pData));

        if (jam) {
            mLoaderThread->mQueue.jam(info, false);
        } else {
            mLoaderThread->mQueue.send(info, false);
        }
    }
}

void FileLoader::requestMountArchive(const char* pName, NebulaArena* pHeap, bool jam) {
    if (isNeedToLoad(pName)) {
        if (pHeap == nullptr) {
            pHeap = MR::getCurrentHeap();
        }

        RequestFileInfo* info = addRequest(pName, 1, 1, mFileHolder->add(pName, pHeap, nullptr));

        if (jam) {
            mLoaderThread->mQueue.jam(info, false);
        } else {
            mLoaderThread->mQueue.send(info, false);
        }
    }
}

bool FileLoader::isLoaded(const char* pName) const {
    return mFileHolder->isExist(pName);
}

void* FileLoader::receiveFile(const char* pName) {
    const RequestFileInfo* info = getRequestFileInfoConst(pName);
    if (info != nullptr && info->mFileEntry != nullptr) {
        info->mFileEntry->waitReadDone();
    }

    return mFileHolder->getContext(pName);
}

AssetFolder* FileLoader::receiveArchive(const char* pName) {
    const RequestFileInfo* info = getRequestFileInfoConst(pName);
    if (info != nullptr && info->mFileEntry != nullptr) {
        info->mFileEntry->waitReadDone();
    }

    return mArchiveHolder->getArchive(pName);
}

void FileLoader::receiveAllRequestedFile() {
    for (u32 i = 0; i < mRequestedFileCount; i++) {
        if (mRequestFileInfos[i].mFileEntry != nullptr) {
            mRequestFileInfos[i].mFileEntry->waitReadDone();
        }
    }
}

AssetFolder* FileLoader::createAndAddArchive(AssetFolder* pFolder, NebulaArena* pHeap, const char* pName) {
    return mArchiveHolder->createAndAdd(pFolder, pHeap, pName)->mArchive;
}

void FileLoader::getMountedArchiveAndHeap(const char* pName, AssetFolder** pArchive, NebulaArena** pHeap) const {
    mArchiveHolder->getArchiveAndHeap(pName, pArchive, pHeap);
}

// The loader reads a request's slot when it comes to it: a slot is free for the next request only once its load is over.
void FileLoader::clearRequestFileInfo(bool) {
    receiveAllRequestedFile();
    mRequestedFileCount = 0;
}

void FileLoader::removeHolderIfIsEqualHeap(NebulaArena* pHeap) {
    for (u32 i = 0; i < mRequestedFileCount; i++) {
        RequestFileInfo* info = &mRequestFileInfos[i];
        if (info->mFileEntry != nullptr && info->mFileEntry->mHeap == pHeap) {
            info->mFileEntry->waitReadDone();
            info->mFileEntry = nullptr;
        }
    }

    mFileHolder->removeIfIsEqualHeap(pHeap);
    mArchiveHolder->removeIfIsEqualHeap(pHeap);
}

void FileLoader::removeFile(const char* pName) {
    RequestFileInfo* info = const_cast< RequestFileInfo* >(getRequestFileInfoConst(pName));
    if (info != nullptr && info->mFileEntry != nullptr) {
        info->mFileEntry->waitReadDone();
        info->mFileEntry = nullptr;
    }

    mFileHolder->removeFile(pName);
}

bool FileLoader::isNeedToLoad(const char* pName) const {
    if (getRequestFileInfoConst(pName) != nullptr) {
        return false;
    }

    return !isLoaded(pName);
}

const RequestFileInfo* FileLoader::getRequestFileInfoConst(const char* pName) const {
    for (u32 i = 0; i < mRequestedFileCount; i++) {
        RequestFileInfo* info = &mRequestFileInfos[i];

        if (MR::isEqualStringCase(info->mFileName, pName)) {
            return info;
        }
    }

    return nullptr;
}

// string load is in the wrong spot for some reason
RequestFileInfo* FileLoader::addRequest(const char* pName, u32 kind, u32 requestType, FileHolderFileEntry* pEntry) {
    GameMutex* mutex = &mMutex;
    mutex->lock();

    // Counted once whole: another thread may wait for every request while this one is added.
    RequestFileInfo* info = &mRequestFileInfos[mRequestedFileCount];
    info->_0 = kind;
    info->mRequestType = requestType;
    info->mFileEntry = pEntry;
    nebula_snprintf(info->mFileName, sizeof(info->mFileName), "%s", pName);
    mRequestedFileCount++;

    mutex->unlock();

    return info;
}
