#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>
#include "Game/System/GameThreads.hpp"

class NebulaArena;

namespace MR {
    template < int T >
    class MutexHolder {
    public:
        static GameMutex sMutex;
    };

    template < int T >
    GameMutex MutexHolder< T >::sMutex;
};  // namespace MR
