#pragma once


class JASTaskThread;

namespace JASLoad {
    JASTaskThread* getThreadPointer();
    bool createThread(int msgCount, u32 stackSize);
};  // namespace JASLoad
