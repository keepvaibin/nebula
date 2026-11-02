#include "Game/System/FileLoaderThread.hpp"
#include "Game/System/GameThreads.hpp"
#include "Game/Util/FileUtil.hpp"
#include "JSystem/JUtility/JUTException.hpp"

namespace {
    void* loadFile(RequestFileInfo* pInfo) {
        void* pData = GameAssets::load(pInfo->mFileName, (u8*)pInfo->mFileEntry->mContext, 0, pInfo->mFileEntry->mHeap,
                                       pInfo->mRequestType != 1 ? GameAssets::ALLOC_DIRECTION_BACKWARD
                                                                : GameAssets::ALLOC_DIRECTION_FORWARD,
                                       nullptr);
        if (pData == nullptr) {
            JUTException::panic_f(__FILE__, __LINE__, "%s cannot be loaded", pInfo->mFileName);
        }

        return pData;
    }
};  // namespace

FileLoaderThread::FileLoaderThread(int msgCount, JKRHeap* pHeap) : GameThread(0x8000, msgCount, pHeap) {
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
    pInfo->_88 = 1;
    void* data = ::loadFile(pInfo);
    pInfo->mFileEntry->setContext(data, pInfo->mFileEntry->mHeap);
    pInfo->_88 = 2;
}

void FileLoaderThread::mountArchiveAndStartCreateResource(RequestFileInfo* pInfo) {
    pInfo->_88 = 1;
    void* data = ::loadFile(pInfo);
    MR::createAndAddArchive(data, pInfo->mFileEntry->mHeap, pInfo->mFileName);
    pInfo->mFileEntry->setContext(data, pInfo->mFileEntry->mHeap);
    pInfo->_88 = 2;
}
