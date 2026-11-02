#ifndef OS_H
#define OS_H

#include <cstdarg>
#include "revolution/types.h"

#ifdef __cplusplus
extern "C" {
#endif


// The console reads its bus clock from low memory; natively it is the Wii's constant (243 MHz).
#ifndef OS_CORE_CLOCK
#define OS_CORE_CLOCK 729000000u
#endif
#ifndef OS_BUS_CLOCK
#define OS_BUS_CLOCK 243000000u
#endif
#ifndef OS_TIMER_CLOCK
#define OS_TIMER_CLOCK (OS_BUS_CLOCK / 4)
#endif

#define OSTicksToMilliseconds(ticks) ((ticks) / (OS_TIMER_CLOCK / 1000))
#define OSMillisecondsToTicks(msec) ((msec) * (OS_TIMER_CLOCK / 1000))
#define OSMicrosecondsToTicks(usec) (((usec) * (OS_TIMER_CLOCK / 125000)) / 8)

#ifdef __cplusplus
extern "C" {
#endif
// A wait on another thread's work (a busy loop the console's interrupts preempt): runs what is pending, or sleeps until
// a host thread raises something.
void __NebulaPoll(void);
// A stable 32-bit name for a game object: its offset in the game's memory.
#ifdef __cplusplus
}
#endif

void* OSGetMEM1ArenaLo(void);

typedef struct OSIOSRev {
    u8 reserved;
    u8 major;
    u8 minor;
    u8 micro;
    u8 month;
    u8 date;
    u16 year;
} OSIOSRev;

extern void __RAS_OSDisableInterrupts_begin(void);
extern void __RAS_OSDisableInterrupts_end(void);

extern s64 __OSStartTime;

#ifndef ASSERT
#define ASSERT(exp) ((void)0)
#endif

#ifndef ASSERTMSG
#define ASSERTMSG(exp, msg) ((void)0)
#endif

#include <NebulaGpu.h>
#include "revolution/os/OSContext.h"
#include "revolution/os/OSThread.h"
#include "revolution/os/OSTime.h"

#ifdef __cplusplus
}
#endif

#endif  // OS_H
