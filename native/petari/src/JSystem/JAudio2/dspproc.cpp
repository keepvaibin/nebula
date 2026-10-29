#include "JSystem/JAudio2/dspproc.hpp"
#include <JSystem/JUtility/JUTNativeLog.hpp>
#include "JSystem/JAudio2/JASDspInterface.hpp"
#include "JSystem/JAudio2/dsptask.hpp"
#include <revolution/os.h>

// The DSP's view of a main-memory address: the console passed cached addresses; natively the physical address the
// host's DSP program resolves.
#ifdef TARGET_PC
#define DSP_ADDR(a) ((u32)OSCachedToPhysical((const void*)(a)))
#else
#define DSP_ADDR(a) ((u32)(a))
#endif

void DSPReleaseHalt2(u32 msg) {
    u32 msgs[2];
    u16 dspMap = DSP_CreateMap2(msg);
    msgs[0] = (msg << 16) | dspMap;

    DSPSendCommands2(msgs, 0, NULL);
}

static volatile BOOL flag;
static volatile BOOL d_waitflag;

static void setup_callback(u16 param_0) {
    JUTNativeLog("Finish %d\n", param_0);
    flag = FALSE;
}

void DsetupTable(u32 param_0, uintptr_t param_1, uintptr_t param_2, uintptr_t param_3, uintptr_t param_4) {
    u32 table[5];
    table[0] = (param_0 & 0xFFFF) | 0x81000000;
    table[1] = DSP_ADDR(param_1);
    table[2] = DSP_ADDR(param_2);
    table[3] = DSP_ADDR(param_3);
    table[4] = DSP_ADDR(param_4);
    JUTNativeLog("Table Setup\n");

    flag = TRUE;
    DSPSendCommands2(table, 5, setup_callback);
    do {
    } while (flag);
}

static u16 DSP_MIXERLEVEL = 0x4000;

void DsetMixerLevel(f32 level) {
    DSP_MIXERLEVEL = (s32)(4096.0f * level);
}

void DsyncFrame2ch(u32 param_0, uintptr_t param_1, uintptr_t param_2) {
    u32 msgs[5];
    msgs[0] = (param_0 & 0xff) << 0x10 | 0x82000000 | DSP_MIXERLEVEL;
    msgs[1] = DSP_ADDR(param_1);
    msgs[2] = DSP_ADDR(param_2);
    msgs[3] = 0;
    msgs[4] = 0;
    DSPSendCommands2(msgs, 5, 0);
}

void DsyncFrame4ch(u32 param_0, uintptr_t param_1, uintptr_t param_2, uintptr_t param_3, uintptr_t param_4) {
    u32 msgs[5];
    msgs[0] = (param_0 & 0xff) << 0x10 | 0x82000000 | DSP_MIXERLEVEL;
    msgs[1] = DSP_ADDR(param_1);
    msgs[2] = DSP_ADDR(param_2);
    msgs[3] = DSP_ADDR(param_3);
    msgs[4] = DSP_ADDR(param_4);
    DSPSendCommands2(msgs, 5, 0);
}

static void dummy_callback(u16 param_0) {
    d_waitflag = FALSE;
    JUTNativeLog("D-Wait end\n");
}

void DsetVARAM(uintptr_t param_0) {
    u32 msgs[2];
    msgs[0] = 0x8E000000;
    msgs[1] = DSP_ADDR(param_0);

    d_waitflag = TRUE;
    DSPSendCommands2(msgs, 2, dummy_callback);
    do {
    } while (d_waitflag);
}
