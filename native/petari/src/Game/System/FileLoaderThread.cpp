#include "Game/System/FileLoaderThread.hpp"
#include "Game/System/AssetFolder.hpp"
#include "Game/System/GameThreads.hpp"
#include "Game/Util/FileUtil.hpp"

namespace {
    void* loadFile(RequestFileInfo* pInfo) {
        void* pData = GameAssets::load(pInfo->mFileName, (u8*)pInfo->mFileEntry->mContext, 0, pInfo->mFileEntry->mHeap,
                                       pInfo->mRequestType != 1 ? GameAssets::ALLOC_DIRECTION_BACKWARD
                                                                : GameAssets::ALLOC_DIRECTION_FORWARD,
                                       nullptr);
        if (pData == nullptr) {
            NEBULA_PANIC("%s cannot be loaded", pInfo->mFileName);
        }

        return pData;
    }
};  // namespace

FileLoaderThread::FileLoaderThread(int msgCount, NebulaArena* pHeap) : GameThread(0x8000, msgCount, pHeap) {
}

void* FileLoaderThread::run() {
    GameThreads::leaveWorld();

    while (true) {
        void* msg;
        mQueue.receive(&msg, true);
        RequestFileInfo* info = (RequestFileInfo*)msg;

        switch (info->_0) {
        case 0:
            loadToMainRAM(info);
            break;
        case 1:
            mountArchiveAndStartCreateResource(info);
            break;
        }
    }
}

void FileLoaderThread::loadToMainRAM(RequestFileInfo* pInfo) {
    pInfo->mFileEntry->setContext(::loadFile(pInfo));
}

void FileLoaderThread::mountArchiveAndStartCreateResource(RequestFileInfo* pInfo) {
    void* data = ::loadFile(pInfo);
    NebulaArena* const pHeap = pInfo->mFileEntry->mHeap;
    MR::createAndAddArchive(new (pHeap, 0) AssetFolder(GameAssets::find(pInfo->mFileName), data), pHeap, pInfo->mFileName);
    pInfo->mFileEntry->setContext(data);
}
