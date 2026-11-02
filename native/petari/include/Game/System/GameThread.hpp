#pragma once

#include <revolution/types.h>
#include "Game/System/GameThreads.hpp"

class JKRHeap;

// A game thread: a host thread (GameThreads) with its message queue; subclasses run their work in run().
class GameThread {
public:
    GameThread(u32, int, JKRHeap*);

    virtual ~GameThread();
    virtual void* run();

    void initMessageQueue(JKRHeap*, int);
    void initHeapSpecified(JKRHeap*, u32);
    static void* start(void*);

    JKRHeap* mHeap;
    JKRHeap* mStartCurrentHeap;
    GameThreadRecord* mThread;
    GameMessageQueue mQueue;
    void** mMessageArray;
    u32 mMessageCount;
    u32 mStackSize;
};
