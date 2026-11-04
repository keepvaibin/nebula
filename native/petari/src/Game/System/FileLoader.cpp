#include "Game/System/FileLoader.hpp"
#include "Game/System/AssetFolder.hpp"
#include "Game/System/GameThreads.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/NebulaArena.hpp"
#include <JSystem/JUtility/JUTNativeLog.hpp>

FileLoader::FileLoader() {
    mMutex.init();
    mFileHolder = new FileHolder();
    mArchiveHolder = new ArchiveHolder();
}

void* FileLoader::loadFile(const char* pName, u8* pData, NebulaArena* pHeap) {
    mMutex.lock();
    FileHolderFileEntry* pEntry = mFileHolder->findEntry(pName);
    if (pEntry == nullptr) {
        if (pHeap == nullptr) {
            pHeap = MR::getCurrentHeap();
        }

        pEntry = mFileHolder->add(pName, pHeap, pData);
        void* const pLoaded = GameAssets::load(pName, pData, 0, pHeap, nullptr);
        if (pLoaded == nullptr) {
            NEBULA_PANIC("%s cannot be loaded", pName);
        }

        pEntry->mContext = pLoaded;
    }

    void* const pFile = pEntry->mContext;
    mMutex.unlock();
    return pFile;
}

AssetFolder* FileLoader::mountArchive(const char* pName, NebulaArena* pHeap) {
    mMutex.lock();
    AssetFolder* pFolder = mArchiveHolder->getArchive(pName);
    if (pFolder == nullptr && mFileHolder->findEntry(pName) == nullptr) {
        if (pHeap == nullptr) {
            pHeap = MR::getCurrentHeap();
        }

        void* const pData = loadFile(pName, nullptr, pHeap);
        pFolder = createAndAddArchive(new (pHeap, 0) AssetFolder(GameAssets::find(pName), pData), pHeap, pName);
    }

    mMutex.unlock();
    return pFolder;
}

bool FileLoader::isLoaded(const char* pName) const {
    return mFileHolder->isExist(pName);
}

void* FileLoader::getLoadedFile(const char* pName) {
    return mFileHolder->isExist(pName) ? mFileHolder->getContext(pName) : nullptr;
}

AssetFolder* FileLoader::getMountedArchive(const char* pName) {
    return mArchiveHolder->getArchive(pName);
}

AssetFolder* FileLoader::createAndAddArchive(AssetFolder* pFolder, NebulaArena* pHeap, const char* pName) {
    return mArchiveHolder->createAndAdd(pFolder, pHeap, pName)->mArchive;
}

void FileLoader::getMountedArchiveAndHeap(const char* pName, AssetFolder** pArchive, NebulaArena** pHeap) const {
    mArchiveHolder->getArchiveAndHeap(pName, pArchive, pHeap);
}

void FileLoader::removeHolderIfIsEqualHeap(NebulaArena* pHeap) {
    mMutex.lock();
    mFileHolder->removeIfIsEqualHeap(pHeap);
    mArchiveHolder->removeIfIsEqualHeap(pHeap);
    mMutex.unlock();
}

void FileLoader::removeFile(const char* pName) {
    mMutex.lock();
    mFileHolder->removeFile(pName);
    mMutex.unlock();
}
