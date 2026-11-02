#pragma once

#include <revolution/types.h>
#include "Game/System/GameThreads.hpp"

class JASCriticalSection {
public:
    JASCriticalSection() : mInterruptState(GameThreads::excludeAudio()) {};
    ~JASCriticalSection() {
        GameThreads::restoreAudio(mInterruptState);
    }
    /* 0x00 */ BOOL mInterruptState;
};
