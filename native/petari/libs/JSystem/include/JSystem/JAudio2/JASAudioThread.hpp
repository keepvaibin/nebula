#pragma once

#include "JSystem/JAudio2/JASGadget.hpp"
#include "Game/System/NebulaKernel.hpp"
#include "JSystem/JAudio2/JASGlobal.hpp"
#include "JSystem/JKernel/JKRThread.hpp"

enum JASAudioMessage {
    AUDIOMSG_DMA = 0,  // the host's audio clock: mix the next frame
    AUDIOMSG_STOP = 2,
};

struct JASAudioThread : public JKRThread, public JASGlobalInstance< JASAudioThread > {
    JASAudioThread(int, int, u32) NO_INLINE;
    static void create(s32);
    void stop();
    virtual void* run();
    static void DMACallback();

    static JASAudioThread* getThreadPointer() {
        return getInstance();
    }

    /* 0x7C */ NebulaThreadQueue sThreadQueue;
    /* 0x84 */ bool sbPauseFlag;  // type unsure

};
