#pragma once

#include "Game/System/NebulaAtomic.hpp"

#include "Game/System/GameThread.hpp"
#include "Game/System/GameThreads.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Util/StringUtil.hpp"

class JKRExpHeap;
class JKRUnitHeap;

namespace MR {
    class FunctorBase;
};  // namespace MR

class FunctionAsyncExecInfo {
public:
    FunctionAsyncExecInfo(MR::FunctorBase*, const char*);

    ~FunctionAsyncExecInfo();

    void execute();

    bool isSame(const char* pStr) {
        return MR::strcasecmp(mName, pStr) == 0;
    }

    /* 0x00 */ MR::FunctorBase* mFunc;
    JKRHeap* mStarterHeap;  // the current heap where it was started
    /* 0x08 */ const char* mName;
    /* 0x0C */ NebulaAtomic< bool > mIsEnd;
    /* 0x10 */ GameMessageQueue mQueue;
    /* 0x30 */ void* mMessage;
};

class FunctionAsyncExecutorThread : public GameThread {
public:
    FunctionAsyncExecutorThread(JKRHeap*);

    virtual void* run();

    /* 0x3C */ bool mIsSuspended;
    /* 0x40 */ const char* mName;
};

class FunctionAsyncExecutorOnMainThread {
public:
    FunctionAsyncExecutorOnMainThread(GameThreadRecord*);

    void update();

    /* 0x000 */ bool _0;
    /* 0x004 */ GameMessageQueue mQueue;
    /* 0x024 */ void* mMsgArray[64];
    /* 0x124 */ GameThreadRecord* mThread;
};

class FunctionAsyncExecutor {
public:
    FunctionAsyncExecutor();

    void update();
    void start(const MR::FunctorBase&, const char*);
    bool startOnMainThread(const MR::FunctorBase&, const char*);
    void waitForEnd(const char*);
    bool isEnd(const char*) const;
    FunctionAsyncExecInfo* createAndAddExecInfo(const MR::FunctorBase&, const char*);
    FunctionAsyncExecutorThread* getSuspendThread();

    /* 0x000 */ FunctionAsyncExecutorThread* mThreads[2];
    /* 0x008 */ FunctionAsyncExecutorOnMainThread* mMainThreadExec;
    /* 0x00C */ MR::Vector< MR::FixedArray< FunctionAsyncExecInfo*, 256 > > mHolders;
    /* 0x410 */ JKRUnitHeap* _410;
    /* 0x414 */ JKRExpHeap* _414;
};
