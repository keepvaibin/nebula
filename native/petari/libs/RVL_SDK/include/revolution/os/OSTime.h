#ifndef OSTIME_H
#define OSTIME_H

#include "revolution/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef u32 OSTick;
typedef s64 OSTime;

typedef struct OSCalendarTime {
    int sec;
    int min;
    int hour;
    int mday;
    int mon;
    int year;
    int wday;
    int yday;
    int msec;
    int usec;
} OSCalendarTime;

OSTime OSGetTime(void);
OSTick OSGetTick(void);
OSTime __OSGetSystemTime(void);
OSTime __OSTimeToSystemTime(OSTime);
void OSTicksToCalendarTime(OSTime, OSCalendarTime*);

#ifdef __MWERKS__
vu32 OS_BUS_CLOCK_SPEED : 0x800000F8;
#else
#ifdef TARGET_PC
// Natively the Wii's bus clock, a constant (the console reads it from low memory).
#define OS_BUS_CLOCK_SPEED 243000000u
#else
vu32 OS_BUS_CLOCK_SPEED;
#endif
#endif

#ifdef __cplusplus
}
#endif

#endif  // OSTIME_H
