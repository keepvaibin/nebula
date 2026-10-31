#ifndef __OSINTERRUPT_H__
#define __OSINTERRUPT_H__

#include "revolution/os/OSContext.h"
#include "revolution/os/OSException.h"
#include "revolution/os/OSTime.h"
#include "revolution/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef s16 __OSInterrupt;
typedef void (*__OSInterruptHandler)(__OSInterrupt, OSContext*);
typedef u32 OSInterruptMask;

BOOL OSDisableInterrupts(void);
BOOL OSEnableInterrupts(void);
BOOL OSRestoreInterrupts(BOOL) NO_INLINE;

__OSInterruptHandler __OSSetInterruptHandler(__OSInterrupt, __OSInterruptHandler);
__OSInterruptHandler __OSGetInterruptHandler(s32);
void __OSDispatchInterrupt(__OSException, OSContext*);
void __OSInterruptInit(void);
OSInterruptMask __OSMaskInterrupts(OSInterruptMask);
OSInterruptMask __OSUnmaskInterrupts(OSInterruptMask);

void __RAS_OSDisableInterrupts_begin(void);
void __RAS_OSDisableInterrupts_end(void);

extern volatile __OSInterrupt __OSLastInterrupt;
extern volatile u32 __OSLastInterruptSrr0;
extern volatile OSTime __OSLastInterruptTime;

#ifdef __cplusplus
}
#endif

#endif  // 0x_OSINTERRUPT_H__
