#pragma once

#include <revolution.h>
#include "Game/System/NebulaKernel.hpp"

class JKRHeap;

class OSThreadWrapper {
public:
    OSThreadWrapper(u32, int, int, JKRHeap*);

    virtual ~OSThreadWrapper();
    virtual void* run();

    void initMessageQueue(JKRHeap*, int);
    void initHeapSpecified(JKRHeap*, u32, int);
    static void* start(void*);

    /* 0x00 */ JKRHeap* mHeap;
    /* 0x04 */ NebulaThread* mThread;
    /* 0x08 */ NebulaMessageQueue mQueue;
    /* 0x2C */ void** mMessageArray;
    /* 0x30 */ u32 mMessageCount;
    /* 0x34 */ u8* mStack;
    /* 0x38 */ u32 mStackSize;
};
