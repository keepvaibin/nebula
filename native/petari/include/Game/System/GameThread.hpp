#pragma once

#include "Game/System/GameThreads.hpp"

class NebulaArena;

// A game thread: a host thread (GameThreads) with its message queue; subclasses run their work in run().
class GameThread {
public:
    GameThread(u32, int, NebulaArena*);

    virtual ~GameThread();
    virtual void* run();

    void initMessageQueue(NebulaArena*, int);
    void initHeapSpecified(NebulaArena*, u32);
    static void* start(void*);

    NebulaArena* mHeap;
    NebulaArena* mStartCurrentHeap;
    GameThreadRecord* mThread;
    GameMessageQueue mQueue;
    void** mMessageArray;
    u32 mMessageCount;
    u32 mStackSize;
};
