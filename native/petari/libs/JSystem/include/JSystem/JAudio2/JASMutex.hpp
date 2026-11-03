#pragma once

#include "Game/System/GameThreads.hpp"
#include "Game/System/GameThreads.hpp"

class JASMutexLock {
public:
    inline JASMutexLock(GameMutex* mutex) {
        mMutex = mutex;
        mMutex->lock();
    }
    inline ~JASMutexLock() {
        mMutex->unlock();
    }

    GameMutex* mMutex;
};
