#include "Game/System/NebulaFrameClock.hpp"

#include <revolution/os.h>

#include <nebula_game_api.h>
#include "Game/System/NebulaSystemSettings.hpp"

extern "C" {
const NebulaHostServices* nebula_host(void);
void nebula_raise_interrupt(__OSInterrupt interrupt);
void nebula_display_refresh(void);
}

namespace {
    volatile u32 sFrames;
    OSThreadQueue sSleepers;
    NebulaFrameClock::Hook sPreHook;
    NebulaFrameClock::Hook sPostHook;
    // show() sets the next state; it is what the screen shows from the next frame on.
    bool sNextPicture;
    bool sPicture;

    // VI_DM_DEFAULT: 5 minutes of NTSC fields.
    const u32 cTimeToDimming = 18000;
    bool sDimming;
    u32 sIdleFrames;

    void onFrame(__OSInterrupt, OSContext* pContext) {
        OSContext handlerContext;
        sFrames++;
        if (!sDimming) {
            sIdleFrames = 0;
        } else if (sIdleFrames < cTimeToDimming) {
            sIdleFrames++;
        }
        OSClearContext(&handlerContext);
        OSSetCurrentContext(&handlerContext);
        if (sPreHook != nullptr) {
            sPreHook(sFrames);
        }
        sPicture = sNextPicture;
        nebula_host()->vi_frame(nebula_host()->user, 0, sPicture ? 0 : 1, sFrames);
        nebula_display_refresh();
        if (sPostHook != nullptr) {
            OSClearContext(&handlerContext);
            sPostHook(sFrames);
        }
        OSWakeupThread(&sSleepers);
        OSClearContext(&handlerContext);
        OSSetCurrentContext(pContext);
    }
}  // namespace

extern "C" void nebula_frame_clock_reset_idle(void) {
    NebulaFrameClock::resetIdle();
}

extern "C" NEBULA_GAME_API void nebula_game_vi_retrace(void) {
    nebula_raise_interrupt(__OS_INTERRUPT_PI_VI);
}

namespace NebulaFrameClock {
    void init() {
        static bool sStarted;
        if (sStarted) {
            return;
        }
        sStarted = true;
        OSInitThreadQueue(&sSleepers);
        __OSSetInterruptHandler(__OS_INTERRUPT_PI_VI, onFrame);
        __OSUnmaskInterrupts(OS_INTERRUPTMASK_PI_VI);
    }

    u32 count() {
        return sFrames;
    }

    // Interrupts off from reading the count to sleeping: a frame delivered in between would wake no one.
    void waitFrame() {
        const BOOL enabled = OSDisableInterrupts();
        const u32 frames = sFrames;
        do {
            OSSleepThread(&sSleepers);
        } while (frames == sFrames);
        OSRestoreInterrupts(enabled);
    }

    void show(bool picture) {
        sNextPicture = picture;
    }

    // The frame arrives as an interrupt, which the OS core delivers only at the end of an OS call: a plain store
    // cannot be interrupted halfway.
    Hook setPreHook(Hook hook) {
        const Hook previous = sPreHook;
        sPreHook = hook;
        return previous;
    }

    Hook setPostHook(Hook hook) {
        const Hook previous = sPostHook;
        sPostHook = hook;
        return previous;
    }

    bool enableDimming(bool enable) {
        const bool previous = sDimming;
        sDimming = enable && NebulaSystemSettings::screenSaver();
        return previous;
    }

    u32 framesUntilDimming() {
        return cTimeToDimming - sIdleFrames;
    }

    void resetIdle() {
        sIdleFrames = 0;
    }
}  // namespace NebulaFrameClock
