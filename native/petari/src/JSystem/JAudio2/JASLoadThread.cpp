#include "JSystem/JAudio2/JASLoadThread.hpp"
#include "JSystem/JAudio2/JASTaskThread.hpp"
#include "Game/System/NebulaArena.hpp"

namespace JASLoad {
    static JASTaskThread* sThread;
};  // namespace JASLoad

JASTaskThread* JASLoad::getThreadPointer() {
    return JASLoad::sThread;
}

bool JASLoad::createThread(int msgCount, u32 stackSize) {
    if (sThread != NULL) {
        return false;
    }

    sThread = new (JASDram, 0) JASTaskThread(msgCount, stackSize);


    GameThreads::resume(sThread->mThread);
    return true;
}
