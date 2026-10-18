/* The RVL SDK's scheduler (OSThread.c and friends) on the native OS core: the console's single-core order of
   events. A higher-priority thread runs as soon as it can (OSResumeThread, a message it waits for), a lower one
   only when the higher ones block, OSSleepTicks lets the others run and wakes through the decrementer, alarms fire
   in deadline order. */
#include <stdio.h>
#include <string.h>

#include <revolution/os.h>

#include "nebula_sdk.h"

static char sOrder[64];
static int sOrderLength;
static OSMessageQueue sQueue;
static OSMessage sQueueSlots[4];
static OSThread sHigh, sLow;
static u8 sHighStack[0x4000] __attribute__((aligned(32)));
static u8 sLowStack[0x4000] __attribute__((aligned(32)));
static int sFailures;

static void note(char c) {
    sOrder[sOrderLength++] = c;
}

static void* high_main(void* param) {
    OSMessage message;
    (void)param;
    note('H');
    OSReceiveMessage(&sQueue, &message, OS_MESSAGE_BLOCK);
    note((char)(uintptr_t)message);
    return 0;
}

static void* low_main(void* param) {
    (void)param;
    note('L');
    return 0;
}

static OSAlarm sAlarms[3];
static char sAlarmOrder[4];
static int sAlarmCount;

static void alarm_handler(OSAlarm* alarm, OSContext* context) {
    (void)context;
    sAlarmOrder[sAlarmCount++] = (char)('a' + (alarm - sAlarms));
}

static void expect(const char* what, const char* got, const char* want) {
    if (strcmp(got, want) != 0) {
        printf("FAIL %s: %s (want %s)\n", what, got, want);
        sFailures++;
    }
}

static void game_main(void) {
    OSInitMessageQueue(&sQueue, sQueueSlots, 4);
    OSCreateThread(&sHigh, high_main, 0, sHighStack + sizeof(sHighStack), sizeof(sHighStack), 10, 0);
    OSCreateThread(&sLow, low_main, 0, sLowStack + sizeof(sLowStack), sizeof(sLowStack), 20, 0);

    OSResumeThread(&sHigh); /* runs at once, blocks on the queue */
    note('m');
    OSResumeThread(&sLow); /* lower: waits */
    OSSendMessage(&sQueue, (OSMessage)(uintptr_t)'h', OS_MESSAGE_BLOCK); /* wakes high, which runs at once */
    note('n');
    OSSleepTicks(OSMillisecondsToTicks(2)); /* low runs while main sleeps */
    note('o');
    sOrder[sOrderLength] = 0;
    expect("thread order", sOrder, "HmhnLo");

    /* Alarms set out of order fire by deadline. */
    for (int i = 0; i < 3; i++) {
        OSCreateAlarm(&sAlarms[i]);
    }
    OSSetAlarm(&sAlarms[0], OSMillisecondsToTicks(6), alarm_handler);
    OSSetAlarm(&sAlarms[1], OSMillisecondsToTicks(2), alarm_handler);
    OSSetAlarm(&sAlarms[2], OSMillisecondsToTicks(4), alarm_handler);
    OSSleepTicks(OSMillisecondsToTicks(10));
    sAlarmOrder[sAlarmCount] = 0;
    expect("alarm order", sAlarmOrder, "bca");

    {
        const OSTime before = OSGetTime();
        OSSleepTicks(OSMillisecondsToTicks(5));
        const OSTime slept = OSGetTime() - before;
        if (slept < OSMillisecondsToTicks(5) || slept > OSMillisecondsToTicks(40)) {
            printf("FAIL OSSleepTicks(5 ms) took %lld ms\n", (long long)OSTicksToMilliseconds(slept));
            sFailures++;
        }
    }

    printf(sFailures ? "%d failures\n" : "all passed\n", sFailures);
    fflush(stdout);
    exit(sFailures != 0);
}

int main(void) {
    nebula_os_boot(game_main);
}
