#include "JSystem/JKernel/JKRThread.hpp"
#include "Game/System/GameThreads.hpp"
#include "JSystem/JKernel/JKRHeap.hpp"
#include "JSystem/JUtility/TColor.hpp"

JKRThread::JKRThread(u32 stack_size, int message_count) : mThreadListLink(this) {
    JKRHeap* heap = JKRHeap::findFromRoot(this);
    if (heap == nullptr) {
        heap = JKRGetSystemHeap();
    }

    setCommon_heapSpecified(heap, stack_size);
    setCommon_mesgQueue(mHeap, message_count);
}

JKRThread::JKRThread(JKRHeap* pHeap, u32 stack_size, int message_count) : mThreadListLink(this) {
    if (pHeap == nullptr) {
        pHeap = JKRGetCurrentHeap();
    }

    setCommon_heapSpecified(pHeap, stack_size);
    setCommon_mesgQueue(mHeap, message_count);
}

JKRThread::~JKRThread() {
    sThreadList.remove(&mThreadListLink);

    if (mHeap != nullptr) {
        if (!GameThreads::isTerminated(mThread)) {
            GameThreads::cancel(mThread);
        }

        JKRHeap::free(mStackMemory, mHeap);
        JKRHeap::free(mThread, mHeap);
    }

    JKRHeap::free(mMesgBuffer, 0);
}

void JKRThread::setCommon_mesgQueue(JKRHeap* pHeap, int msgCount) {
    mMessageCount = msgCount;
    // A message is a pointer: 8 bytes natively.
    mMesgBuffer = reinterpret_cast< void** >(JKRHeap::alloc(mMessageCount * sizeof(void*), 0, pHeap));
    mMessageQueue.init(mMesgBuffer, mMessageCount);
    sThreadList.append(&mThreadListLink);
    mCurrentHeap = nullptr;
    mCurrentHeapError = 0;
}

void JKRThread::setCommon_heapSpecified(JKRHeap* pHeap, u32 stack_size) {
    mStartCurrentHeap = JKRHeap::sCurrentHeap;
    mHeap = pHeap;
    mStackSize = stack_size & 0xffffffe0;
    mStackMemory = JKRAllocFromHeap(mHeap, mStackSize, 0x20);
    mThread = reinterpret_cast< GameThreadRecord* >(JKRAllocFromHeap(mHeap, sizeof(GameThreadRecord), 0x20));
    GameThreads::create(mThread, start, this, mStackSize);
}

void* JKRThread::start(void* pThread) {
    JKRThread* self = static_cast< JKRThread* >(pThread);
    JKRHeap::sCurrentHeap = self->mStartCurrentHeap;
    return self->run();
}

JKRThread* JKRThread::searchThread(GameThreadRecord* pThread) {
    for (JSUPtrLink* i = sThreadList.mHead; i != nullptr; i = i->mNext) {
        JKRThread* thread = reinterpret_cast< JKRThread* >(i->mData);

        if (thread->mThread == pThread) {
            return thread;
        }
    }

    return nullptr;
}

JKRThread* JKRThreadSwitch::enter(JKRThread* pThread, int a2) {
    if (pThread == nullptr) {
        return nullptr;
    }

    JKRThread* foundThread = JKRThread::searchThread(pThread->mThread);

    if (foundThread != nullptr) {
        pThread = foundThread;
    }

    JKRThread::TLoad* inf = &pThread->mLoadInfo;

    inf->clear();
    inf->_10 = a2;
    inf->_0 = 1;

    return pThread;
}

JUtility::TColor& JUtility::TColor::operator=(const TColor& rColor) {
    static_cast< NebulaColor8& >(*this) = rColor;
    return *this;
}

void setThreadColor(JUtility::TColor& rDestination, u8 r, u8 g, u8 b, u8 a) {
    rDestination = JUtility::TColor(r, g, b, a);
}

JSUList< JKRThread > JKRThread::sThreadList = JSUList< JKRThread >(false);
u64 JKRThreadSwitch::sTotalStart;
JSUList< JKRTask > JKRTask::sTaskList = JSUList< JKRTask >();
u8 JKRTask::sEndMesgQueue[32];
JKRThreadSwitch* JKRThreadSwitch::sManager;
u32 JKRThreadSwitch::sTotalCount;

