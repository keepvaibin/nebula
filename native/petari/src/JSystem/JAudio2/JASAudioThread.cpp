#include "JSystem/JAudio2/JASAudioThread.hpp"
#include "Game/System/NebulaKernel.hpp"
#include "JSystem/JAudio2/JASAiCtrl.hpp"
#include "JSystem/JAudio2/JASChannel.hpp"
#include "JSystem/JAudio2/JASDSPChannel.hpp"
#include "JSystem/JAudio2/JASDspInterface.hpp"
#include "JSystem/JAudio2/JASDriverIF.hpp"
#include "JSystem/JAudio2/JASHeapCtrl.hpp"
#include "JSystem/JAudio2/JASProbe.hpp"
#include "JSystem/JAudio2/JASReport.hpp"
#include "JSystem/JKernel/JKRSolidHeap.hpp"
#include <stdint.h>

inline JASAudioThread::JASAudioThread(int stackSize, int msgCount, u32 threadPriority)
    : JKRThread(JASDram, threadPriority, msgCount, stackSize), JASGlobalInstance< JASAudioThread >(true) {
    sbPauseFlag = false;
    sThreadQueue.init();
}

void JASAudioThread::create(s32 threadPriority) {
    const int size = 0x2000;
    JASAudioThread* pAudioThread = new (JASDram, 0) JASAudioThread(threadPriority, 0x10, size);
    JKRHeap* pCurrentHeap = JKRGetSystemHeap();
    pAudioThread->setCurrentHeap(pCurrentHeap);
    pAudioThread->resume();
}

void JASAudioThread::stop() {
    jamMessageBlock((void*)2);
}

void* JASAudioThread::run() {
    JASDriver::initAI(DMACallback);
    JASDsp::initBuffer();
    JASDSPChannel::initAll();

    JASPoolAllocObject_MultiThreaded< JASChannel >::newMemPool(0x48);
    JASDriver::startDMA();

    void* msg;
    while (true) {
        msg = waitMessageBlock();
        switch ((intptr_t)msg) {
        case AUDIOMSG_DMA:
            if (sbPauseFlag) {
                JASDriver::stopDMA();
                sThreadQueue.sleep();
            }

            JASDriver::updateDac();
            JASDriver::updateDacCallback();
            continue;

        case AUDIOMSG_STOP:
            JASDriver::stopDMA();
            NebulaKernel::exit(nullptr);
            continue;
        }
    }
}

void JASAudioThread::DMACallback() {
    JASAudioThread* pAudioThread = getInstance();
    JASProbe::stop(4);
    JASProbe::start(4, "UPDATE-DAC");
    if (!pAudioThread->sendMessage((void*)AUDIOMSG_DMA)) {
    }
}
