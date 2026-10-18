#include "JSystem/JAudio2/dsptask.hpp"
#include "JSystem/JAudio2/osdsp.hpp"

void DspInitWork();
void DspHandShake(void* pTask);
int DspStartWork(u32 param_0, void (*pCallback)(u16));

extern int Dsp_Running_Check();
extern void Dsp_Running_Start();

#ifndef TARGET_PC
void DspHandShake(void*) {
    OSReport("DSP InitCallback \n");
    while (DSPCheckMailFromDSP() == 0) {
    }

    OSReport("\201\364JDSP/Boot:: \202\143\202\162\202\157\202\251\202\347\202\314\203\164\203\100\201\133\203\130\203\147\203\201\201\133\203\213(%x)\202\252\223\315\202\253\202\334\202\265\202\275\n" /* ♪JDSP/Boot:: ＤＳＰからのファーストメール(%x)が届きました\n */, DSPReadMailFromDSP());
    OSReport("\201\364JDSP/Boot:: \202\143\202\162\202\157\202\251\202\347\202\314\203\132\203\112\203\223\203\150\203\201\201\133\203\213(%x)\202\252\223\315\202\253\202\334\202\265\202\275\n" /* ♪JDSP/Boot:: ＤＳＰからのセカンドメール(%x)が届きました\n */, DSPCheckMailFromDSP());

    OSReport("\201\364JDSP/Boot:: \202\143\202\162\202\157\202\306\202\314\220\332\221\261\202\311\220\254\214\367\202\265\202\334\202\265\202\275\n" /* ♪JDSP/Boot:: ＤＳＰとの接続に成功しました\n */);
    Dsp_Running_Start();
}
#endif

#ifndef TARGET_PC
// The JAudio DSP program is Nintendo's DSP machine code: Nebula's fork does not carry it (a CodeWarrior build of this
// file takes the table from upstream Petari). Natively the host does the program's work (native/sdk/audio).
extern u16 jdsp[];

static DSPTaskInfo audiotask ATTRIBUTE_ALIGN(32);

static u16 AUDIO_YIELD_BUFFER[4096] ATTRIBUTE_ALIGN(32);
#endif

#ifdef TARGET_PC
// Nebula's host runs the JAudio DSP program (native/sdk/audio): it returns 1 when the words finished a subframe.
extern "C" int __NebulaDSPCommands(const u32* words, u32 count);
void JASAudioThread_postDSPMessage();

void DspBoot(void (*pCallback)(void*)) {
    (void)pCallback;
    DspInitWork();
    Dsp_Running_Start();
}

int DSPSendCommands2(u32* pMessages, u32 param_2, void (*pCallback)(u16)) {
    if (__NebulaDSPCommands(pMessages, param_2)) {
        JASAudioThread_postDSPMessage();
    }
    if (pCallback != NULL) {
        pCallback(0);
    }
    return 1;
}
#else
void DspBoot(void (*pCallback)(void*)) {
    DspInitWork();
    OSReport("Dsp \202\360\203\165\201\133\203\147\202\265\202\334\202\267\n" /* Dsp をブートします\n */);
    audiotask.priority = 0xf0;
    audiotask.iram_mmem_addr = (u16*)((u8*)jdsp + 0x80000000);
    audiotask.iram_length = sizeof(jdsp);
    audiotask.iram_addr = 0;
    audiotask.dram_mem_addr = (u16*)((u8*)AUDIO_YIELD_BUFFER + 0x80000000);
    audiotask.dram_len = sizeof(AUDIO_YIELD_BUFFER);
    audiotask.dram_addr = 0;
    audiotask.dsp_vector = 0;
    audiotask.dsp_res_vector = 0x10;
    audiotask.init_cb = DspHandShake;
    audiotask.res_cb = NULL;
    audiotask.done_cb = NULL;
    audiotask.req_cb = pCallback;
    DSPInit();
    DSPAddPriorTask(&audiotask);
    OSReport("Dsp\203\165\201\133\203\147\202\265\202\334\202\265\202\275\n" /* Dspブートしました\n */);
}

int DSPSendCommands2(u32* pMessages, u32 param_2, void (*pCallback)(u16)) {
    s32 i;
    BOOL interruptFlag;
    s32 startWorkStatus;

    BOOL firstWarning = TRUE;
    while (Dsp_Running_Check() == 0) {
        if (firstWarning) {
            OSReport("Warning:\202\334\202\276\203\165\201\133\203\147\202\265\202\304\202\334\202\271\202\361\n" /* Warning:まだブートしてません\n */);
        }

        firstWarning = FALSE;
    }

    interruptFlag = OSDisableInterrupts();
    if (DSPCheckMailToDSP()) {
        OSReport("\203\107\203\211\201\133::DSP\202\252\203\201\203\142\203\132\201\133\203\127\202\360\216\363\202\257\216\346\202\301\202\304\202\242\202\334\202\271\202\361\n" /* エラー::DSPがメッセージを受け取っていません\n */);
        OSRestoreInterrupts(interruptFlag);
        return -1;
    }

    DSPSendMailToDSP(param_2);
    DSPAssertInt();
    while (DSPCheckMailToDSP() != 0) {
    }

    if (param_2 == 0) {
        param_2 = 1;
    }

    if (pCallback != NULL) {
        startWorkStatus = DspStartWork(pMessages[0], pCallback);
    }

    for (i = 0; i < param_2; i++) {
        DSPSendMailToDSP(pMessages[i]);
        while (DSPCheckMailToDSP() != 0) {
        }
    }

    OSRestoreInterrupts(interruptFlag);
    return startWorkStatus;
}
#endif

typedef struct {
    /* 0x00 */ u16 field_0x0;
    /* 0x02 */ u16 field_0x2;
    /* 0x04 */ void (*field_0x4)(u16);
} TaskWorkStruct;

static TaskWorkStruct taskwork[16];

void DspInitWork() {
    for (int i = 0; i < 16; i++) {
        taskwork[i].field_0x4 = NULL;
    }
}

static u32 taskreadp;

static u32 taskwritep;

int DspStartWork(u32 param_0, void (*pCallback)(u16)) {
    u32 taskWritePrev = taskwritep;
    u32 writeVal = ((taskWritePrev + 1) & 0xf);
    if (writeVal == taskreadp) {
        OSReport("DSP-Task Buffer is full ( %d, %d)\n", taskwritep, taskreadp);
        return 0;
    }

    taskwritep = writeVal;
    taskwork[taskWritePrev].field_0x0 = param_0 >> 0x10;
    taskwork[taskWritePrev].field_0x4 = pCallback;
    return taskWritePrev + 1;
}

void DspFinishWork(u16 param_0) {
    if (param_0 != taskwork[taskreadp].field_0x0) {
        OSReport("Error:: Not finish task\n");
    } else {
        if (taskwork[taskreadp].field_0x4) {
            taskwork[taskreadp].field_0x4(taskreadp);
        }

        taskreadp = (taskreadp + 1) & 0xf;
    }
}
