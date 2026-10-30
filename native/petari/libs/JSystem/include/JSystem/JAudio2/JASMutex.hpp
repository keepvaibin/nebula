#pragma once

#include "Game/System/NebulaKernel.hpp"
#include "Game/System/NebulaKernel.hpp"

class JASMutexLock {
public:
    inline JASMutexLock(NebulaMutex* mutex) {
        mMutex = mutex;
        mMutex->lock();
    }
    inline ~JASMutexLock() {
        mMutex->unlock();
    }

    /* 0x0 */ NebulaMutex* mMutex;
};
