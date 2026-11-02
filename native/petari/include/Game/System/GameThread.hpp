#pragma once

#include <revolution/types.h>
#include "Game/System/NebulaKernel.hpp"

class JKRHeap;

// A game thread: a host thread (NebulaKernel) with its message queue; subclasses run their work in run().
class GameThread {
public:
    GameThread(u32, int, int, JKRHeap*);

    virtual ~GameThread();
    virtual void* run();

    void initMessageQueue(JKRHeap*, int);
    void initHeapSpecified(JKRHeap*, u32, int);
    static void* start(void*);

    JKRHeap* mHeap;
    JKRHeap* mStartCurrentHeap;
    NebulaThread* mThread;
    NebulaMessageQueue mQueue;
    void** mMessageArray;
    u32 mMessageCount;
    u32 mStackSize;
};
