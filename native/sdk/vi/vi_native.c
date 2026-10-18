/* Video natively (in place of the SDK's vi.c). The host owns the display clock: it calls nebula_game_vi_retrace
   (any thread) once per field of the 59.94 Hz NTSC timing, or at the pace its presentation needs, and the module
   delivers it as the VI interrupt. The handler runs the SDK's order: the count, the pre-retrace callback, the flushed
   frame buffer and black state taking effect, the frame to show going to the host (vi_frame), the post-retrace
   callback, and VIWaitForRetrace's waiters waking. */
#include <revolution/gx.h>
#include <revolution/os.h>
#include <revolution/sc.h>
#include <revolution/vi.h>

#include <nebula_game_api.h>

#include "nebula_sdk.h"

const NebulaHostServices* nebula_host(void);

static volatile u32 sRetraceCount;
static OSThreadQueue sRetraceQueue;
static VIRetraceCallback sPreCallback;
static VIRetraceCallback sPostCallback;

/* Set by VISetNextFrameBuffer/VISetBlack, latched by VIFlush, shown from the next retrace. */
static void* sPendingFrameBuffer;
static BOOL sPendingBlack = TRUE;
static void* sNextFrameBuffer;
static BOOL sNextBlack = TRUE;
static void* sCurrentFrameBuffer;
static BOOL sCurrentBlack = TRUE;
static BOOL sFlushed;

/* Screen dimming, as the console's VI counts it: retraces since the remotes' last input (WPAD calls __VIResetRFIdle)
   reach the dimming time while dimming is enabled, which needs SYSCONF's screen saver on. The game then lowers its
   volume (AudSystem::screenSaverProcess). A PC display dims itself: the picture is not darkened. */
enum { kTimeToDimming = 18000 }; /* VI_DM_DEFAULT: 5 minutes of NTSC fields */
static BOOL sDimming;
static u32 sIdleRetraces;

NEBULA_GAME_API void nebula_game_vi_retrace(void) {
    nebula_raise_interrupt(__OS_INTERRUPT_PI_VI);
}

static void retrace_interrupt(__OSInterrupt interrupt, OSContext* context) {
    (void)interrupt;
    OSContext exceptionContext;
    sRetraceCount++;
    if (!sDimming) {
        sIdleRetraces = 0;
    } else if (sIdleRetraces < kTimeToDimming) {
        sIdleRetraces++;
    }
    OSClearContext(&exceptionContext);
    OSSetCurrentContext(&exceptionContext);
    if (sPreCallback != NULL) {
        sPreCallback(sRetraceCount);
    }
    if (sFlushed) {
        sCurrentFrameBuffer = sNextFrameBuffer;
        sCurrentBlack = sNextBlack;
        sFlushed = FALSE;
    }
    nebula_host()->vi_frame(nebula_host()->user, sCurrentFrameBuffer != NULL ? OSCachedToPhysical(sCurrentFrameBuffer) : 0,
                            sCurrentBlack, sRetraceCount);
    nebula_display_refresh();
    if (sPostCallback != NULL) {
        OSClearContext(&exceptionContext);
        sPostCallback(sRetraceCount);
    }
    OSWakeupThread(&sRetraceQueue);
    OSClearContext(&exceptionContext);
    OSSetCurrentContext(context);
}

void VIInit(void) {
    static BOOL initialized;
    if (initialized) {
        return;
    }
    initialized = TRUE;
    OSInitThreadQueue(&sRetraceQueue);
    __OSSetInterruptHandler(__OS_INTERRUPT_PI_VI, retrace_interrupt);
    __OSUnmaskInterrupts(OS_INTERRUPTMASK_PI_VI);
}

/* The render mode reaches the renderer through the GX copy commands; nothing to program natively. */
void VIConfigure(const GXRenderModeObj* rm) {
    (void)rm;
}

void VIConfigurePan(u16 x, u16 y, u16 width, u16 height) {
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}

void VIFlush(void) {
    const BOOL enabled = OSDisableInterrupts();
    sNextFrameBuffer = sPendingFrameBuffer;
    sNextBlack = sPendingBlack;
    sFlushed = TRUE;
    OSRestoreInterrupts(enabled);
}

void VIWaitForRetrace(void) {
    const BOOL enabled = OSDisableInterrupts();
    const u32 count = sRetraceCount;
    do {
        OSSleepThread(&sRetraceQueue);
    } while (count == sRetraceCount);
    OSRestoreInterrupts(enabled);
}

void VISetNextFrameBuffer(void* fb) {
    const BOOL enabled = OSDisableInterrupts();
    sPendingFrameBuffer = fb;
    OSRestoreInterrupts(enabled);
}

void* VIGetNextFrameBuffer(void) {
    return sNextFrameBuffer;
}

void* VIGetCurrentFrameBuffer(void) {
    return sCurrentFrameBuffer;
}

void VISetBlack(BOOL black) {
    const BOOL enabled = OSDisableInterrupts();
    sPendingBlack = black;
    OSRestoreInterrupts(enabled);
}

VIRetraceCallback VISetPreRetraceCallback(VIRetraceCallback callback) {
    const BOOL enabled = OSDisableInterrupts();
    const VIRetraceCallback previous = sPreCallback;
    sPreCallback = callback;
    OSRestoreInterrupts(enabled);
    return previous;
}

VIRetraceCallback VISetPostRetraceCallback(VIRetraceCallback callback) {
    const BOOL enabled = OSDisableInterrupts();
    const VIRetraceCallback previous = sPostCallback;
    sPostCallback = callback;
    OSRestoreInterrupts(enabled);
    return previous;
}

u32 VIGetRetraceCount(void) {
    return sRetraceCount;
}

/* RMGE01: NTSC timing; a digital (component-class) connection, so the game offers 480p. */
u32 VIGetTvFormat(void) {
    return VI_NTSC;
}

u32 VIGetDTVStatus(void) {
    return 1;
}

BOOL VIEnableDimming(BOOL enable) {
    const BOOL previous = sDimming;
    sDimming = enable && SCGetScreenSaverMode() != 0;
    return previous;
}

/* Retraces left before the screen dims; 0 once it has. */
u32 VIGetDimmingCount(void) {
    return kTimeToDimming - sIdleRetraces;
}

BOOL VIResetDimmingCount(void) {
    sIdleRetraces = 0;
    return TRUE;
}

BOOL __VIResetRFIdle(void) {
    sIdleRetraces = 0;
    return TRUE;
}

void VISetTrapFilter(u8 filter) {
    (void)filter;
}
