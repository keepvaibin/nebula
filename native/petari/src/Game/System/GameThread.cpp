#include "Game/System/GameThread.hpp"
#include "Game/System/GameThreads.hpp"
#include <JSystem/JKernel/JKRHeap.hpp>

GameThread::GameThread(u32 stackSize, int messageCount, JKRHeap* pHeap) {
    mStartCurrentHeap = JKRHeap::sCurrentHeap;
    if (pHeap == nullptr) {
        pHeap = JKRHeap::sCurrentHeap;
    }

    initHeapSpecified(pHeap, stackSize);
    initMessageQueue(mHeap, messageCount);
}

GameThread::~GameThread() {
    if (mHeap != nullptr) {
        if (!GameThreads::isTerminated(mThread)) {
            GameThreads::cancel(mThread);
        }

        delete mThread;
    }

    delete[] mMessageArray;
}

void GameThread::initMessageQueue(JKRHeap* pHeap, int messageCount) {
    mMessageCount = messageCount;
    mMessageArray = new (pHeap, 0) void*[mMessageCount];

    mQueue.init(mMessageArray, mMessageCount);
}

void GameThread::initHeapSpecified(JKRHeap* pHeap, u32 stackSize) {
    mHeap = pHeap;
    mStackSize = stackSize & ~31;  // the host thread's own stack
    mThread = new (mHeap, 32) GameThreadRecord();

    GameThreads::create(mThread, &GameThread::start, this, mStackSize);
}

void* GameThread::start(void* pArg) {
    GameThread* self = static_cast< GameThread* >(pArg);
    JKRHeap::sCurrentHeap = self->mStartCurrentHeap;
    return self->run();
}

void* GameThread::run() {
    return nullptr;
}
