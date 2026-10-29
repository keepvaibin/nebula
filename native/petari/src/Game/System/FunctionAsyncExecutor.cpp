#include "Game/System/FunctionAsyncExecutor.hpp"
#include <JSystem/JUtility/JUTNativeLog.hpp>
#include "Game/Util/Functor.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/MutexHolder.hpp"
#include <JSystem/JKernel/JKRExpHeap.hpp>
#include <JSystem/JKernel/JKRUnitHeap.hpp>

FunctionAsyncExecInfo::FunctionAsyncExecInfo(MR::FunctorBase* pFunc, int priority, const char* pName)
    : mFunc(pFunc), mPriority(priority), mName(pName), mIsEnd(false) {
    OSInitMessageQueue(&mQueue, &mMessage, 1);
}

FunctionAsyncExecInfo::~FunctionAsyncExecInfo() {
    delete mFunc;
}

void FunctionAsyncExecInfo::execute() {
    OSThread* pThread = OSGetCurrentThread();
    OSPriority priorityPrev = OSGetThreadPriority(pThread);

    OSSetThreadPriority(pThread, mPriority);
    (*mFunc)();
    OSSetThreadPriority(pThread, priorityPrev);
}

FunctionAsyncExecutorThread::FunctionAsyncExecutorThread(JKRHeap* pHeap)
    : OSThreadWrapper(0x10000, 4, 1, pHeap), mIsSuspended(false), mName(nullptr) {
}

void* FunctionAsyncExecutorThread::run() {

    while (true) {
        mIsSuspended = false;
        mName = nullptr;

        OSMessage msg;
        OSReceiveMessage(&mQueue, &msg, OS_MESSAGE_BLOCK);

        FunctionAsyncExecInfo* info = reinterpret_cast< FunctionAsyncExecInfo* >(msg);
        mName = info->mName;
        mIsSuspended = true;
        info->execute();
        OSSendMessage(&info->mQueue, nullptr, OS_MESSAGE_NOBLOCK);
        info->mIsEnd = true;
    }
}

FunctionAsyncExecutorOnMainThread::FunctionAsyncExecutorOnMainThread(OSThread* pThread) : mThread(pThread), _0() {
    OSInitMessageQueue(&mQueue, mMsgArray, ARRAY_SIZE(mMsgArray));
}

void FunctionAsyncExecutorOnMainThread::update() {
    OSMessage msg;
    FunctionAsyncExecInfo* info;

    if (OSReceiveMessage(&mQueue, &msg, OS_MESSAGE_NOBLOCK)) {
        _0 = true;

        info = static_cast< FunctionAsyncExecInfo* >(msg);
        info->mPriority = OSGetThreadPriority(OSGetCurrentThread());
        info->execute();

        OSSendMessage(&info->mQueue, nullptr, OS_MESSAGE_NOBLOCK);

        info->mIsEnd = true;
    }
}

FunctionAsyncExecutor::FunctionAsyncExecutor() : mMainThreadExec(nullptr), mHolders(), _410(nullptr), _414(nullptr) {
    for (s32 i = 0; i < ARRAY_SIZE(mThreads); i++) {
        FunctionAsyncExecutorThread* thread = new FunctionAsyncExecutorThread(MR::getStationedHeapNapa());
        mThreads[i] = thread;
        OSResumeThread(thread->mThread);
    }

    mMainThreadExec = new FunctionAsyncExecutorOnMainThread(OSGetCurrentThread());
#ifdef TARGET_PC
    // Units of the native record (0x34 bytes on the console), as many as the console's heap held.
    _410 = JKRUnitHeap::create(sizeof(FunctionAsyncExecInfo), 0x34A8 / 0x34 * ALIGN_NEXT(sizeof(FunctionAsyncExecInfo), 4), 4,
                               MR::getCurrentHeap(), false);
#else
    _410 = JKRUnitHeap::create(0x34, 0x34A8, 4, MR::getCurrentHeap(), false);
#endif
    _414 = JKRExpHeap::create(0x2800, MR::getCurrentHeap(), false);
}

void FunctionAsyncExecutor::update() {
    mMainThreadExec->update();
}

void FunctionAsyncExecutor::start(const MR::FunctorBase& rFunc, int priority, const char* pName) {
    FunctionAsyncExecInfo* info = createAndAddExecInfo(rFunc, priority, pName);
    FunctionAsyncExecutorThread* thread = getSuspendThread();
    OSSendMessage(&thread->mQueue, info, OS_MESSAGE_NOBLOCK);
}

bool FunctionAsyncExecutor::startOnMainThread(const MR::FunctorBase& rFunc, const char* pName) {
    FunctionAsyncExecutorOnMainThread* thread = mMainThreadExec;
    bool isSameThread = OSGetCurrentThread() == thread->mThread;

    if (isSameThread) {
        rFunc();
        return false;
    } else {
        FunctionAsyncExecInfo* info = createAndAddExecInfo(rFunc, 0, pName);
        OSSendMessage(&mMainThreadExec->mQueue, info, OS_MESSAGE_NOBLOCK);
        return true;
    }
}

void FunctionAsyncExecutor::waitForEnd(const char* pName) {
    OSLockMutex(&MR::MutexHolder< 2 >::sMutex);
    FunctionAsyncExecInfo** cur = mHolders.begin();
    FunctionAsyncExecInfo* const* lst = mHolders.end();

    while (cur != lst && !(*cur)->isSame(pName)) {
        cur++;
    }

#ifdef TARGET_PC
    // A function startOnMainThread ran at once (its caller was the main thread) has no entry: it has ended. The console
    // reads past the list's end here and waits on whatever entry lies there; natively that can be a stale entry that
    // never ends (ResourceHolderManager::createAndAddInner on the main thread would hang).
    if (cur == lst) {
        OSUnlockMutex(&MR::MutexHolder< 2 >::sMutex);
        return;
    }
#endif
    FunctionAsyncExecInfo* info = *cur;
    OSUnlockMutex(&MR::MutexHolder< 2 >::sMutex);
    OSReceiveMessage(&info->mQueue, &info->mMessage, OS_MESSAGE_BLOCK);
    OSLockMutex(&MR::MutexHolder< 2 >::sMutex);
    cur = mHolders.begin();
    lst = mHolders.end();

    while (cur != lst && *cur != info) {
        cur++;
    }

    // TODO: Inline of `MR::Vector<T>::erase`.
    if (mHolders.end() - cur - 1 > 0) {
        for (FunctionAsyncExecInfo** p = cur; p + 1 != mHolders.end(); p++) {
            *p = *(p + 1);
        }
    }

    mHolders.mCount--;
    OSUnlockMutex(&MR::MutexHolder< 2 >::sMutex);
    delete info;
}

/* this matches but on a different compiler version */
bool FunctionAsyncExecutor::isEnd(const char* pName) const {
    OSLockMutex(&MR::MutexHolder< 2 >::sMutex);
    FunctionAsyncExecInfo* const* cur = mHolders.begin();
    FunctionAsyncExecInfo* const* lst = mHolders.end();

    while ((cur != lst) && !(*cur)->isSame(pName)) {
        cur++;
    }

#ifdef TARGET_PC
    // The console read past the list's end here; natively a missing name is reported.
    if (cur == lst) {
        for (FunctionAsyncExecInfo* const* p = mHolders.begin(); p != lst; p++) {
            JUTNativeLog("  async function %p \"%s\" end %d\n", (*p)->mName, (*p)->mName, (*p)->mIsEnd);
        }
        OSPanic(__FILE__, __LINE__, "isEnd: no async function %p \"%s\" among %d", pName, pName, (int)mHolders.size());
    }
#endif
    FunctionAsyncExecInfo* info = *cur;
    OSUnlockMutex(&MR::MutexHolder< 2 >::sMutex);
    return info->mIsEnd;
}

OSThread* FunctionAsyncExecutor::getOSThread(const char* pName) {
    for (s32 i = 0; i < ARRAY_SIZE(mThreads); i++) {
        FunctionAsyncExecutorThread* pThread = mThreads[i];

        if (pThread->mIsSuspended && MR::isEqualString(pThread->mName, pName)) {
            return pThread->mThread;
        }
    }

    return nullptr;
}

FunctionAsyncExecInfo* FunctionAsyncExecutor::createAndAddExecInfo(const MR::FunctorBase& rFunc, int priority, const char* pName) {
    MR::FunctorBase* func = rFunc.clone(_414);
    FunctionAsyncExecInfo* info = new (_410, 0) FunctionAsyncExecInfo(func, priority, pName);

    OSLockMutex(&MR::MutexHolder< 2 >::sMutex);
    mHolders.push_back(info);
    OSUnlockMutex(&MR::MutexHolder< 2 >::sMutex);
    return info;
}

FunctionAsyncExecutorThread* FunctionAsyncExecutor::getSuspendThread() {
    for (s32 i = 0; i < ARRAY_SIZE(mThreads); i++) {
        if (!mThreads[i]->mIsSuspended) {
            return mThreads[i];
        }
    }

    return nullptr;
}
