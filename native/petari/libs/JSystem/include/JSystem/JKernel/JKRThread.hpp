#pragma once

#include "JSystem/JKernel/JKRDisposer.hpp"
#include "Game/System/NebulaKernel.hpp"
#include "JSystem/JKernel/JKRHeap.hpp"

#include <revolution/os.h>

class JKRTask {
public:
    static JSUList< JKRTask > sTaskList;
    static u8 sEndMesgQueue[32];
};

class JKRThread : public JKRDisposer {
public:
    class TLoad {
    public:
        TLoad();
        void clear();

        u8 _0;
        u32 _4;
        u32 _8;
        u32 _C;
        u32 _10;
    };

    JKRThread(u32 stack_size, int message_count, int param_3);
    JKRThread(JKRHeap* heap, u32 stack_size, int message_count, int param_4);
    JKRThread(NebulaThread* thread, int message_count);

    virtual ~JKRThread();
    virtual void* run() {
        return nullptr;
    }

    void setCommon_mesgQueue(JKRHeap* heap, int message_count);
    void setCommon_heapSpecified(JKRHeap* heap, u32 stack_size, int param_3);

    NebulaThread* getThreadRecord() const {
        return mThread;
    }
    void* getStack() const {
        return mStackMemory;
    }
    TLoad* getLoadInfo() {
        return &mLoadInfo;
    }
    JKRHeap* getCurrentHeap() const {
        return mCurrentHeap;
    }
    s32 getCurrentHeapError() const {
        return mCurrentHeapError;
    }

    void setCurrentHeap(JKRHeap* heap) {
        mCurrentHeap = heap ? heap : JKRHeap::getCurrentHeap();
    }

    void resume() {
        NebulaKernel::resume(mThread);
    }
    BOOL sendMessage(void* message) {
        return mMessageQueue.send(message, false);
    }
    void sendMessageBlock(void* message) {
        mMessageQueue.send(message, true);
    }
    void* waitMessage() {
        void* message;
        mMessageQueue.receive(&message, false);
        return message;
    }

    void* waitMessage(BOOL* received) {
        void* message;
        *received = mMessageQueue.receive(&message, false);
        return message;
    }
    void* waitMessageBlock() {
        void* message;
        mMessageQueue.receive(&message, true);
        return message;
    }
    void jamMessageBlock(void* message) {
        mMessageQueue.jam(message, true);
    }

    static void* start(void* pData);

    static JKRThread* searchThread(NebulaThread*);

    /* 0x18 */ JSULink< JKRThread > mThreadListLink;
    /* 0x28 */ JKRHeap* mHeap;
    /* 0x2C */ NebulaThread* mThread;
    /* 0x30 */ NebulaMessageQueue mMessageQueue;
    /* 0x50 */ void** mMesgBuffer;
    /* 0x54 */ s32 mMessageCount;
    /* 0x58 */ void* mStackMemory;
    /* 0x5C */ u32 mStackSize;
    /* 0x60 */ TLoad mLoadInfo;
    /* 0x74 */ JKRHeap* mCurrentHeap;
    /* 0x78 */ s32 mCurrentHeapError;

    static JSUList< JKRThread > sThreadList;
};

inline JKRThread::TLoad::TLoad() {
    clear();
    _0 = 0;
    _10 = 0;
}

inline void JKRThread::TLoad::clear() {
    _8 = 0;
    _4 = 0;
    _C = 0;
}

class JKRThreadSwitch {
public:
    JKRThread* enter(JKRThread*, int);

    static JKRThreadSwitch* getManager() {
        return sManager;
    }

    static JKRThreadSwitch* sManager;
    static u32 sTotalCount;
    static u64 sTotalStart;
};
