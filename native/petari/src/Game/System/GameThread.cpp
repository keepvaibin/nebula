#include "Game/System/GameThread.hpp"
#include "Game/System/GameThreads.hpp"
#include "Game/System/NebulaArena.hpp"

GameThread::GameThread(u32 stackSize, int messageCount, NebulaArena* pHeap) {
    mStartCurrentHeap = NebulaArena::currentSlot();
    if (pHeap == nullptr) {
        pHeap = NebulaArena::currentSlot();
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

void GameThread::initMessageQueue(NebulaArena* pHeap, int messageCount) {
    mMessageCount = messageCount;
    mMessageArray = new (pHeap, 0) void*[mMessageCount];

    mQueue.init(mMessageArray, mMessageCount);
}

void GameThread::initHeapSpecified(NebulaArena* pHeap, u32 stackSize) {
    mHeap = pHeap;
    mStackSize = stackSize & ~31;  // the host thread's own stack
    mThread = new (mHeap, 32) GameThreadRecord();

    GameThreads::create(mThread, &GameThread::start, this, mStackSize);
}

void* GameThread::start(void* pArg) {
    GameThread* self = static_cast< GameThread* >(pArg);
    NebulaArena::setCurrentSlot(self->mStartCurrentHeap);
    return self->run();
}

void* GameThread::run() {
    return nullptr;
}
