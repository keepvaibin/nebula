#include "JSystem/JAudio2/JASTaskThread.hpp"
#include "Game/System/GameThreads.hpp"
#include <stddef.h>
#include "JSystem/JAudio2/JASCalc.hpp"
#include "Game/System/GameThreads.hpp"
#include "Game/System/NebulaArena.hpp"

JASTaskThread::JASTaskThread(int msgCount, u32 stackSize) : GameThread(stackSize, msgCount, JASDram) {
}

JASTaskThread::~JASTaskThread() {
    void* msg;
    while (true) {
        const BOOL received = mQueue.receive(&msg, false);
        if (!received) {
            return;
        }
        JASKernel::getCommandHeap()->free(msg);
    }
}

void* JASTaskThread::allocCallStack(JASThreadCallback callback, const void* msg, u32 msgSize) {
    u32 size = offsetof(JASThreadCallStack, msg) + msgSize;
    JASThreadCallStack* callStack = (JASThreadCallStack*)JASKernel::getCommandHeap()->alloc(size);
    if (callStack == NULL) {
        return NULL;
    }

    callStack->msgType_ = 1;
    JASCalc::bcopy(msg, callStack->msg.buffer, msgSize);
    callStack->callback_ = callback;
    return callStack;
}

void* JASTaskThread::allocCallStack(JASThreadCallback callback, void* msg) {
    JASThreadCallStack* callStack;
    callStack = (JASThreadCallStack*)JASKernel::getCommandHeap()->alloc(offsetof(JASThreadCallStack, msg) + sizeof(void*));
    if (callStack == NULL) {
        return NULL;
    }

    callStack->msgType_ = 0;
    callStack->msg.bufferPtr = msg;
    callStack->callback_ = callback;
    return callStack;
}

int JASTaskThread::sendCmdMsg(JASThreadCallback callback, const void* msg, u32 msgSize) {
    void* callstack;

    callstack = allocCallStack(callback, msg, msgSize);
    if (callstack == NULL) {
        return 0;
    }

    BOOL iVar2 = mQueue.send(callstack, false);
    if (!iVar2) {
        JASKernel::getCommandHeap()->free(callstack);
    }
    return iVar2;
}

int JASTaskThread::sendCmdMsg(JASThreadCallback callback, void* msg) {
    void* callstack;

    callstack = allocCallStack(callback, msg);
    if (callstack == NULL) {
        return 0;
    }

    BOOL iVar2 = mQueue.send(callstack, false);
    if (!iVar2) {
        JASKernel::getCommandHeap()->free(callstack);
    }
    return iVar2;
}

void* JASTaskThread::run() {
    GameThreads::leaveWorld();
    JASThreadCallStack* callstack;
    do {
        void* msg;
        mQueue.receive(&msg, true);
        callstack = static_cast< JASThreadCallStack* >(msg);

        if (callstack->msgType_) {
            callstack->callback_(callstack->msg.buffer);
        } else {
            callstack->callback_(callstack->msg.bufferPtr);
        }

        JASKernel::getCommandHeap()->free(callstack);
    } while (true);
}
