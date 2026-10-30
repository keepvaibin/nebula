#pragma once

#include <revolution/os/OSInterrupt.h>
#include "Game/System/NebulaKernel.hpp"

class JASCriticalSection {
public:
    JASCriticalSection() : mInterruptState(NebulaKernel::disableInterrupts()) {};
    ~JASCriticalSection() {
        NebulaKernel::restoreInterrupts(mInterruptState);
    }
    /* 0x00 */ BOOL mInterruptState;
};
