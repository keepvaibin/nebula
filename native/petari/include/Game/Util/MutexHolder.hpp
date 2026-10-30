#pragma once

#include <revolution/gx.h>
#include <revolution/mtx.h>
#include <revolution/os.h>
#include "Game/System/NebulaKernel.hpp"

class JKRHeap;

namespace MR {
    template < int T >
    class MutexHolder {
    public:
        static NebulaMutex sMutex;
    };

    template < int T >
    NebulaMutex MutexHolder< T >::sMutex;
};  // namespace MR
