#include "JSystem/JAudio2/JASLoadThread.hpp"
#include "JSystem/JAudio2/JASTaskThread.hpp"
#include "JSystem/JKernel/JKRSolidHeap.hpp"

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

    JKRHeap* pCurrentHeap = JKRGetSystemHeap();

    sThread->setCurrentHeap(pCurrentHeap);
    sThread->resume();
    return true;
}
