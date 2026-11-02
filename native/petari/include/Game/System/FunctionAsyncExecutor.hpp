#pragma once

#include "Game/System/NebulaAtomic.hpp"

#include "Game/System/GameThread.hpp"
#include "Game/System/GameThreads.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Util/StringUtil.hpp"

class NebulaArena;
class NebulaArena;

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

    MR::FunctorBase* mFunc;
    NebulaArena* mStarterHeap;  // the current heap where it was started
    const char* mName;
    NebulaAtomic< bool > mIsEnd;
    GameMessageQueue mQueue;
    void* mMessage;
};

class FunctionAsyncExecutorThread : public GameThread {
public:
    FunctionAsyncExecutorThread(NebulaArena*);

    virtual void* run();

    bool mIsSuspended;
    const char* mName;
};

class FunctionAsyncExecutorOnMainThread {
public:
    FunctionAsyncExecutorOnMainThread(GameThreadRecord*);

    void update();

    bool _0;
    GameMessageQueue mQueue;
    void* mMsgArray[64];
    GameThreadRecord* mThread;
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
    FunctionAsyncExecutorThread* addThread();
    FunctionAsyncExecutorThread* getSuspendThread();

    MR::Vector< MR::AssignableArray< FunctionAsyncExecutorThread* > > mThreads;
    FunctionAsyncExecutorOnMainThread* mMainThreadExec;
    MR::Vector< MR::FixedArray< FunctionAsyncExecInfo*, 256 > > mHolders;
    NebulaArena* _410;
    NebulaArena* _414;
};
