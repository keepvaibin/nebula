#pragma once

#include <revolution/types.h>

class JASTaskThread;

namespace JASDvd {
    JASTaskThread* getThreadPointer();
    bool createThread(int msgCount, u32 stackSize);
};  // namespace JASDvd
