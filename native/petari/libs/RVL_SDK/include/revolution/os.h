#ifndef OS_H
#define OS_H

#include <stdarg.h>
#include "revolution/types.h"

#ifdef __cplusplus
extern "C" {
#endif


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


#ifndef ASSERT
#define ASSERT(exp) ((void)0)
#endif

#ifndef ASSERTMSG
#define ASSERTMSG(exp, msg) ((void)0)
#endif

#include <NebulaGpu.h>
#include "revolution/os/OSContext.h"

#ifdef __cplusplus
}
#endif

#endif  // OS_H
