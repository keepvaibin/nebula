#pragma once

#include <NebulaGpu.h>
#include <revolution/mtx.h>
#include <revolution/os.h>
#include "Game/System/GameThreads.hpp"

class JKRHeap;

namespace MR {
    template < int T >
    class MutexHolder {
    public:
        static GameMutex sMutex;
    };

    template < int T >
    GameMutex MutexHolder< T >::sMutex;
};  // namespace MR
