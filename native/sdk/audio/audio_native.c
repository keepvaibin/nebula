/* Audio natively (in place of the SDK's AI and ARAM-alternative libraries; the mixer is the host's, which JASDsp
   drives itself).
   - AI: the audio interface's DMA engine. AIInitDMA latches the next block (its samples and length); the host's
     output plays the blocks and, as the console's AI does when a block's DMA ends, calls
     nebula_game_ai_interrupt, delivered as the AI interrupt that runs the game's DMA callback.
   - AR: the Wii's "dummy ARAM" in MEM2 (aralt.c's semantics): ARAM addresses are offsets from the MEM2 arena,
     transfers are copies, and the callback follows each one. */
#include <string.h>
#include <nebula_kernel.h>

#include <revolution/ai.h>
#include <revolution/aralt.h>
#include <revolution/os.h>

#include <nebula_game_api.h>

#include "nebula_sdk.h"

const NebulaHostServices* nebula_host(void);

static AIDCallback sDMACallback;
static BOOL sAIInitialized;

NEBULA_GAME_API void nebula_game_ai_interrupt(void) {
    nebula_raise_event(NEBULA_EVENT_AUDIO);
}

static void ai_interrupt(void) {
    if (sDMACallback != NULL) {
        sDMACallback();
    }
}

void AIInit(u8* stack) {
    (void)stack;
    if (sAIInitialized) {
        return;
    }
    sAIInitialized = TRUE;
    nebula_set_event_handler(NEBULA_EVENT_AUDIO, ai_interrupt);
    nebula_enable_event(NEBULA_EVENT_AUDIO);
}

AIDCallback AIRegisterDMACallback(AIDCallback callback) {
    const int enabled = nebula_interrupts_disable();
    const AIDCallback previous = sDMACallback;
    sDMACallback = callback;
    nebula_interrupts_restore(enabled);
    return previous;
}

void AIInitDMA(uintptr_t start, u32 length) {
    nebula_host()->ai_dma(nebula_host()->user, (const void*)start, length);
}

void AIStartDMA(void) {
    nebula_host()->ai_control(nebula_host()->user, NEBULA_AI_START, 0);
}

void AIStopDMA(void) {
    nebula_host()->ai_control(nebula_host()->user, NEBULA_AI_STOP, 0);
}

/* 0: 32 kHz, 1: 48 kHz (the DSP's output rate). */
void AISetDSPSampleRate(u32 rate) {
    nebula_host()->ai_control(nebula_host()->user, NEBULA_AI_RATE, rate != 0 ? 48000 : 32000);
}

enum { kARBase = 0x4000 };

static ARCallback sARCallback;
static u32 sARSize;
static uintptr_t sARBase;   /* MEM2 arena low: ARAM offset 0 */
static uintptr_t sARTop;    /* the next allocation, as an address */
static BOOL sARInitialized;

u32 ARInit(u32* stack, u32 entries) {
    (void)stack;
    (void)entries;
    if (sARInitialized) {
        return kARBase;
    }
    const int enabled = nebula_interrupts_disable();
    sARCallback = NULL;
    sARInitialized = TRUE;
    nebula_interrupts_restore(enabled);
    const uintptr_t lo = (uintptr_t)OSGetMEM2ArenaLo();
    const uintptr_t hi = (uintptr_t)OSGetMEM2ArenaHi();
    sARBase = lo;
    sARTop = lo + kARBase;
    sARSize = (u32)(hi - lo);
    return kARBase;
}

u32 ARAlloc(u32 amount) {
    const int enabled = nebula_interrupts_disable();
    const uintptr_t top = sARTop;
    sARTop += amount;
    nebula_interrupts_restore(enabled);
    return (u32)(top - sARBase);
}

u32 ARGetBaseAddress(void) {
    return kARBase;
}

u32 ARGetSize(void) {
    return sARSize;
}

void ARStartDMA(u32 type, uintptr_t src, uintptr_t dst, u32 length) {
    nebula_scheduler_disable();
    if (type == 0) {
        memcpy((void*)(sARBase + dst), (const void*)src, length);
        nebula_gpu_coherent((const void*)(sARBase + dst), length);
    } else if (type == 1) {
        memcpy((void*)dst, (const void*)(sARBase + src), length);
        nebula_gpu_coherent((const void*)dst, length);
    }
    nebula_scheduler_enable();
    if (sARCallback != NULL) {
        sARCallback();
    }
}

/* ARQ's queue serves ARQPostRequest, which nothing in the game calls; ARQInit only marks ARQ started. */
void ARQInit(void) {
}
