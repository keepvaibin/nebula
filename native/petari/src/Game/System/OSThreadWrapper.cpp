#include "Game/System/OSThreadWrapper.hpp"
#include "Game/System/NebulaKernel.hpp"
#include <JSystem/JKernel/JKRHeap.hpp>

OSThreadWrapper::OSThreadWrapper(u32 stackSize, int messageCount, int priority, JKRHeap* pHeap) {
    if (pHeap == nullptr) {
        pHeap = JKRHeap::sCurrentHeap;
    }

    initHeapSpecified(pHeap, stackSize, priority);
    initMessageQueue(mHeap, messageCount);
}

OSThreadWrapper::~OSThreadWrapper() {
    if (mHeap != nullptr) {
        if (!NebulaKernel::isTerminated(mThread)) {
            NebulaKernel::detach(mThread);
            NebulaKernel::cancel(mThread);
        }

        delete[] mStack;
        delete mThread;
    }

    delete[] mMessageArray;
}

void OSThreadWrapper::initMessageQueue(JKRHeap* pHeap, int messageCount) {
    mMessageCount = messageCount;
    mMessageArray = new (pHeap, 0) void*[mMessageCount];

    mQueue.init(mMessageArray, mMessageCount);
}

void OSThreadWrapper::initHeapSpecified(JKRHeap* pHeap, u32 stackSize, int priority) {
    mHeap = pHeap;
    mStackSize = stackSize & ~31;
    mStack = new (mHeap, 32) u8[mStackSize];
    mThread = new (mHeap, 32) NebulaThread();

    NebulaKernel::create(mThread, &OSThreadWrapper::start, this, mStackSize, priority, true /* detached */);
}

void* OSThreadWrapper::start(void* pArg) {
    return static_cast< OSThreadWrapper* >(pArg)->run();
}

void* OSThreadWrapper::run() {
    return nullptr;
}
