#include "Game/System/FunctionAsyncExecutor.hpp"
#include "Game/System/GameThreads.hpp"
#include <JSystem/JUtility/JUTNativeLog.hpp>
#include "Game/Util/Functor.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/MutexHolder.hpp"
#include "Game/System/NebulaArena.hpp"

FunctionAsyncExecInfo::FunctionAsyncExecInfo(MR::FunctorBase* pFunc, const char* pName)
    : mFunc(pFunc), mStarterHeap(NebulaArena::currentSlot()), mName(pName), mIsEnd(false) {
    mQueue.init(&mMessage, 1);
}

FunctionAsyncExecInfo::~FunctionAsyncExecInfo() {
    delete mFunc;
}

void FunctionAsyncExecInfo::execute() {
    NebulaArena* const previous = mStarterHeap->becomeCurrentHeap();
    (*mFunc)();
    NebulaArena::setCurrentSlot(previous);
}

FunctionAsyncExecutorThread::FunctionAsyncExecutorThread(NebulaArena* pHeap)
    : GameThread(0x10000, 4, pHeap), mIsSuspended(false), mName(nullptr) {
}

void* FunctionAsyncExecutorThread::run() {

    while (true) {
        mIsSuspended = false;
        mName = nullptr;

        void* msg;
        mQueue.receive(&msg, true);

        FunctionAsyncExecInfo* info = reinterpret_cast< FunctionAsyncExecInfo* >(msg);
        mName = info->mName;
        mIsSuspended = true;
        info->execute();
        info->mIsEnd = true;
        info->mQueue.send(nullptr, false);
    }
}

FunctionAsyncExecutorOnMainThread::FunctionAsyncExecutorOnMainThread(GameThreadRecord* pThread) : mThread(pThread), _0() {
    mQueue.init(mMsgArray, ARRAY_SIZE(mMsgArray));
}

void FunctionAsyncExecutorOnMainThread::update() {
    void* msg;
    FunctionAsyncExecInfo* info;

    if (mQueue.receive(&msg, false)) {
        _0 = true;

        info = static_cast< FunctionAsyncExecInfo* >(msg);
        info->execute();
        info->mIsEnd = true;
        info->mQueue.send(nullptr, false);
    }
}

FunctionAsyncExecutor::FunctionAsyncExecutor() : mMainThreadExec(nullptr), mHolders(), _410(nullptr), _414(nullptr) {
    mThreads.init(2);
    for (s32 i = 0; i < 2; i++) {
        addThread();
    }

    mMainThreadExec = new FunctionAsyncExecutorOnMainThread(GameThreads::current());
    _410 = NebulaArena::create(MR::getCurrentHeap());
    _414 = NebulaArena::create(MR::getCurrentHeap());
}

void FunctionAsyncExecutor::update() {
    mMainThreadExec->update();
}

void FunctionAsyncExecutor::start(const MR::FunctorBase& rFunc, const char* pName) {
    FunctionAsyncExecInfo* info = createAndAddExecInfo(rFunc, pName);
    FunctionAsyncExecutorThread* thread = getSuspendThread();
    thread->mQueue.send(info, true);
}

bool FunctionAsyncExecutor::startOnMainThread(const MR::FunctorBase& rFunc, const char* pName) {
    FunctionAsyncExecutorOnMainThread* thread = mMainThreadExec;
    bool isSameThread = GameThreads::current() == thread->mThread;

    if (isSameThread) {
        rFunc();
        return false;
    } else {
        FunctionAsyncExecInfo* info = createAndAddExecInfo(rFunc, pName);
        mMainThreadExec->mQueue.send(info, true);
        return true;
    }
}

void FunctionAsyncExecutor::waitForEnd(const char* pName) {
    (MR::MutexHolder< 2 >::sMutex).lock();
    FunctionAsyncExecInfo** cur = mHolders.begin();
    FunctionAsyncExecInfo* const* lst = mHolders.end();

    while (cur != lst && !(*cur)->isSame(pName)) {
        cur++;
    }

    // A function startOnMainThread ran at once (its caller was the main thread) has no entry: it has ended. Reading
    // past the list's end could wait on a stale entry that never ends (ResourceHolderManager::createAndAddInner on the
    // main thread would hang).
    if (cur == lst) {
        (MR::MutexHolder< 2 >::sMutex).unlock();
        return;
    }
    FunctionAsyncExecInfo* info = *cur;
    (MR::MutexHolder< 2 >::sMutex).unlock();
    info->mQueue.receive(&info->mMessage, true);
    (MR::MutexHolder< 2 >::sMutex).lock();
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
    (MR::MutexHolder< 2 >::sMutex).unlock();
    delete info;
}

/* this matches but on a different compiler version */
bool FunctionAsyncExecutor::isEnd(const char* pName) const {
    (MR::MutexHolder< 2 >::sMutex).lock();
    FunctionAsyncExecInfo* const* cur = mHolders.begin();
    FunctionAsyncExecInfo* const* lst = mHolders.end();

    while ((cur != lst) && !(*cur)->isSame(pName)) {
        cur++;
    }

    // A missing name is reported.
    if (cur == lst) {
        for (FunctionAsyncExecInfo* const* p = mHolders.begin(); p != lst; p++) {
            JUTNativeLog("  async function %p \"%s\" end %d\n", (*p)->mName, (*p)->mName, (*p)->mIsEnd);
        }
        NEBULA_PANIC("isEnd: no async function %p \"%s\" among %d", pName, pName, (int)mHolders.size());
    }
    FunctionAsyncExecInfo* info = *cur;
    (MR::MutexHolder< 2 >::sMutex).unlock();
    return info->mIsEnd;
}

FunctionAsyncExecInfo* FunctionAsyncExecutor::createAndAddExecInfo(const MR::FunctorBase& rFunc, const char* pName) {
    MR::FunctorBase* func = rFunc.clone(_414);
    FunctionAsyncExecInfo* info = new (_410, 0) FunctionAsyncExecInfo(func, pName);

    (MR::MutexHolder< 2 >::sMutex).lock();
    mHolders.push_back(info);
    (MR::MutexHolder< 2 >::sMutex).unlock();
    return info;
}

FunctionAsyncExecutorThread* FunctionAsyncExecutor::addThread() {
    FunctionAsyncExecutorThread* thread = new FunctionAsyncExecutorThread(MR::getStationedHeap());
    mThreads.push_back(thread);
    GameThreads::resume(thread->mThread);
    return thread;
}

FunctionAsyncExecutorThread* FunctionAsyncExecutor::getSuspendThread() {
    for (s32 i = 0; i < mThreads.size(); i++) {
        if (!mThreads[i]->mIsSuspended) {
            return mThreads[i];
        }
    }

    // All busy: one more.
    return addThread();
}
