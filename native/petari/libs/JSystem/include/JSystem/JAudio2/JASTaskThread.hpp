#pragma once

#include "JSystem/JAudio2/JASHeapCtrl.hpp"
#include "Game/System/GameThreads.hpp"
#include "Game/System/GameThread.hpp"

typedef void (*JASThreadCallback)(void*);

class JASTaskThread : public GameThread {
private:
    struct JASThreadCallStack {
        /* 0x00 */ JASThreadCallback callback_;
        /* 0x04 */ u32 msgType_;
        /* 0x08 */ union {
            u8 buffer[0x400];
            void* bufferPtr;
        } msg;
    };

public:
    typedef JASMemChunkPool< 1024, JASThreadingModel::ObjectLevelLockable > ThreadMemPool;

    JASTaskThread(int msgCount, u32 stackSize);
    void* allocCallStack(JASThreadCallback callback, void* msg);
    void* allocCallStack(JASThreadCallback callback, const void* msg, u32 msgSize);
    int sendCmdMsg(JASThreadCallback callback, void* msg);
    int sendCmdMsg(JASThreadCallback callback, const void* msg, u32 msgSize);

    virtual ~JASTaskThread();
    virtual void* run();

};
