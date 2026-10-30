/* Audio natively (in place of the SDK's AI library; the mixer is the host's, which JASDsp
   drives itself).
   - AI: the audio interface's DMA engine. AIInitDMA latches the next block (its samples and length); the host's
     output plays the blocks and, as the console's AI does when a block's DMA ends, calls
     nebula_game_ai_interrupt, delivered as the kernel's audio event that runs the game's DMA callback.
   The auxiliary memory (the console's ARAM) is the game's own (NebulaAuxMemory). */
#include <nebula_kernel.h>

#include <revolution/ai.h>
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

